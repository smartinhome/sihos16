// sihos16 — odbiornik wM-Bus na CC1101.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "radio_cc1101.h"

#include <algorithm>
#include <cstring>

#include "esphome/core/log.h"

namespace esphome::sihos16 {

using namespace sihos::wmb;

static const char *const TAG = "sihos16.radio";

namespace {

// Rejestry i polecenia CC1101 (karta katalogowa TI, rozdz. 29).
constexpr uint8_t IOCFG2 = 0x00, IOCFG0 = 0x02, FIFOTHR = 0x03, PKTLEN = 0x06, PKTCTRL0 = 0x08;
constexpr uint8_t FREQ2 = 0x0D, FREQ1 = 0x0E, FREQ0 = 0x0F;
constexpr uint8_t SRES = 0x30, SCAL = 0x33, SRX = 0x34, SIDLE = 0x36, SFRX = 0x3A;
constexpr uint8_t PARTNUM = 0x30, VERSION = 0x31, RSSI = 0x34, MARCSTATE = 0x35, RXBYTES = 0x3B;
constexpr uint8_t FIFO = 0x3F;
constexpr uint8_t READ = 0x80, BURST = 0x40;
constexpr uint8_t MARC_IDLE = 0x01, MARC_RX = 0x0D, MARC_RX_END = 0x0E, MARC_RX_OVERFLOW = 0x11;

constexpr uint8_t FIFO_THRESHOLD = 0x0A;  // GDO0 w górę przy 44 bajtach w FIFO
constexpr uint32_t HEADER_TIMEOUT_MS = 150;
constexpr uint32_t FRAME_TIMEOUT_MS = 500;  // najdłuższa ramka T to ok. 35 ms

// Ustawienia radia dla wM-Bus 868,95 MHz, 100 kb/s (T1/C1). Słowo synchronizacji 0x543D.
constexpr uint8_t RF_SETTINGS[][2] = {
    {0x00, 0x06}, {0x01, 0x2E}, {0x02, 0x00}, {0x03, 0x0A}, {0x04, 0x54}, {0x05, 0x3D}, {0x06, 0xFF},
    {0x07, 0x00}, {0x08, 0x00}, {0x09, 0x00}, {0x0A, 0x00}, {0x0B, 0x08}, {0x0C, 0x00}, {0x0D, 0x21},
    {0x0E, 0x6B}, {0x0F, 0xD0}, {0x10, 0x5C}, {0x11, 0x04}, {0x12, 0x06}, {0x13, 0x22}, {0x14, 0xF8},
    {0x15, 0x44}, {0x16, 0x07}, {0x17, 0x00}, {0x18, 0x18}, {0x19, 0x2E}, {0x1A, 0xBF}, {0x1B, 0x43},
    {0x1C, 0x09}, {0x1D, 0xB5}, {0x1E, 0x87}, {0x1F, 0x6B}, {0x20, 0xFB}, {0x21, 0xB6}, {0x22, 0x10},
    {0x23, 0xEA}, {0x24, 0x2A}, {0x25, 0x00}, {0x26, 0x1F}, {0x27, 0x41}, {0x28, 0x00}, {0x29, 0x59},
    {0x2A, 0x7F}, {0x2B, 0x3F}, {0x2C, 0x81}, {0x2D, 0x35}, {0x2E, 0x09},
};

}  // namespace

// ---------- SPI ----------
// Bajt statusu 0xFF oznacza, że układ nie jest gotowy (CHIP_RDYn) — ponawiamy kilka razy.

uint8_t Cc1101::strobe_(uint8_t cmd) {
  uint8_t st = 0xFF;
  for (int i = 0; i < 5 && st == 0xFF; i++) {
    this->spi_->enable();
    st = this->spi_->transfer_byte(cmd);
    this->spi_->disable();
    if (st == 0xFF) delayMicroseconds(50);
  }
  return st;
}

uint8_t Cc1101::read_reg_(uint8_t addr) {
  for (int i = 0; i < 5; i++) {
    this->spi_->enable();
    uint8_t st = this->spi_->transfer_byte(addr | READ);
    uint8_t v = this->spi_->transfer_byte(0);
    this->spi_->disable();
    if (st != 0xFF) return v;
    delayMicroseconds(50);
  }
  return 0xFF;
}

uint8_t Cc1101::read_status_(uint8_t addr) { return this->read_reg_(addr | BURST); }

void Cc1101::write_reg_(uint8_t addr, uint8_t value) {
  this->spi_->enable();
  this->spi_->transfer_byte(addr);
  this->spi_->transfer_byte(value);
  this->spi_->disable();
}

void Cc1101::read_fifo_(uint8_t *buf, size_t n) {
  if (n == 0) return;
  this->spi_->enable();
  this->spi_->transfer_byte(FIFO | READ | BURST);
  this->spi_->read_array(buf, n);
  this->spi_->disable();
}

int8_t Cc1101::rssi_dbm_() {
  int raw = this->read_status_(RSSI);
  if (raw >= 128) raw -= 256;
  return static_cast<int8_t>(raw / 2 - 74);
}

// ---------- uruchomienie ----------

bool Cc1101::begin(SpiDev *spi, InternalGPIOPin *gdo0, InternalGPIOPin *gdo2, float mhz) {
  this->spi_ = spi;
  this->gdo0_ = gdo0;
  this->gdo2_ = gdo2;
  this->strobe_(SRES);
  delay(10);
  uint8_t part = this->read_status_(PARTNUM);
  this->version_ = this->read_status_(VERSION);
  if (this->version_ == 0x00 || this->version_ == 0xFF) {
    ESP_LOGE(TAG, "CC1101 nie odpowiada (VERSION=0x%02X). Sprawdź SPI: CS, MOSI, MISO, SCK, zasilanie 3,3 V.",
             this->version_);
    return false;
  }
  if (part != 0x00) ESP_LOGW(TAG, "Nietypowy PARTNUM 0x%02X — to może nie być CC1101", part);

  for (const auto &rv : RF_SETTINGS) this->write_reg_(rv[0], rv[1]);
  uint32_t f = static_cast<uint32_t>(mhz * 65536.0f / 26.0f + 0.5f);
  this->write_reg_(FREQ2, (f >> 16) & 0xFF);
  this->write_reg_(FREQ1, (f >> 8) & 0xFF);
  this->write_reg_(FREQ0, f & 0xFF);
  if (this->read_reg_(IOCFG2) != 0x06 || this->read_reg_(IOCFG0) != 0x00)
    ESP_LOGW(TAG, "Rejestry CC1101 nie zapisały się poprawnie — SPI niestabilne?");

  this->strobe_(SCAL);
  delay(4);
  this->state_ = State::INIT;
  return true;
}

void Cc1101::idle_() {
  this->strobe_(SIDLE);
  for (int i = 0; i < 10 && this->read_status_(MARCSTATE) != MARC_IDLE; i++) delay(1);
}

void Cc1101::start_rx_() {
  this->idle_();
  this->strobe_(SFRX);
  this->write_reg_(FIFOTHR, FIFO_THRESHOLD);
  this->write_reg_(PKTCTRL0, 0x02);  // długość nieznana do czasu odczytu pola L
  this->received_ = 0;
  this->expected_ = 0;
  this->offset_ = 0;
  this->strobe_(SRX);
  for (int i = 0; i < 50; i++) {
    uint8_t m = this->read_status_(MARCSTATE);
    if (m == MARC_RX) break;
    if (m == MARC_RX_OVERFLOW) {
      this->strobe_(SFRX);
      this->strobe_(SRX);
    } else if (m == 0x00) {  // SLEEP: wybudzenie
      this->strobe_(SIDLE);
      this->strobe_(SRX);
    }
    delay(1);
  }
  this->state_ = State::WAIT_SYNC;
}

// ---------- odbiór ----------

bool Cc1101::poll(RadioFrame &out) {
  switch (this->state_) {
    case State::INIT:
      this->start_rx_();
      return false;

    case State::WAIT_SYNC: {
      if (this->gdo2_->digital_read()) {  // GDO2 = słowo synchronizacji odebrane
        this->rssi_ = this->rssi_dbm_();  // RSSI teraz, póki trwa nadawanie
        this->sync_ms_ = millis();
        this->state_ = State::HEADER;
        return false;
      }
      uint8_t rb = this->read_status_(RXBYTES);
      if (rb != 0xFF && ((rb & 0x80) || (rb & 0x7F) >= 24)) {  // śmieci bez synchronizacji
        if (rb & 0x80) this->stats.overflows++;
        this->start_rx_();
      }
      return false;
    }

    case State::HEADER:
      if (millis() - this->sync_ms_ > HEADER_TIMEOUT_MS) {
        this->stats.truncated++;
        this->start_rx_();
        return false;
      }
      if (!this->read_header_()) return false;
      this->state_ = State::DATA;
      [[fallthrough]];

    case State::DATA:
      if (millis() - this->sync_ms_ > FRAME_TIMEOUT_MS) {
        this->stats.truncated++;
        this->start_rx_();
        return false;
      }
      if (!this->read_data_()) return false;
      {
        bool ok = this->finish_(out);
        this->start_rx_();
        return ok;
      }
  }
  return false;
}

// Pierwsze 4 bajty mówią, czy to tryb T (3-of-6) czy C (preambuła 54 CD/3D), i ile bajtów czekać.
bool Cc1101::read_header_() {
  uint8_t rb = this->read_status_(RXBYTES);
  if (rb == 0xFF) return false;
  if (rb & 0x80) {
    this->stats.overflows++;
    this->state_ = State::INIT;
    return false;
  }
  if ((rb & 0x7F) < 4) return false;
  this->read_fifo_(this->raw_, 4);
  this->received_ = 4;

  uint8_t l;
  uint8_t dec[2];
  if (this->raw_[0] == 0x54 && (this->raw_[1] == 0xCD || this->raw_[1] == 0x3D)) {
    this->mode_ = 'C';
    this->format_ = this->raw_[1] == 0xCD ? 'A' : 'B';
    this->offset_ = 2;
    l = this->raw_[2];
    this->expected_ = 2 + (this->format_ == 'A' ? format_a_size(l) : 1u + l);
  } else if (decode_3of6(this->raw_, 3, dec, 1) == 1 && dec[0] >= 10) {
    this->mode_ = 'T';
    this->format_ = 'A';
    this->offset_ = 0;
    l = dec[0];
    this->expected_ = encoded_3of6_size(format_a_size(l));
  } else {  // tryb C bez preambuły w FIFO
    this->mode_ = 'C';
    this->format_ = 'A';
    this->offset_ = 0;
    l = this->raw_[0];
    this->expected_ = format_a_size(l);
  }
  if (l < 9 || this->expected_ > sizeof(this->raw_)) {
    this->state_ = State::INIT;
    return false;
  }

  // Znana długość: radio samo zakończy odbiór po ostatnim bajcie.
  if (this->expected_ < 256) {
    this->write_reg_(PKTLEN, static_cast<uint8_t>(this->expected_));
    this->write_reg_(PKTCTRL0, 0x00);
  }
  return true;
}

bool Cc1101::read_data_() {
  size_t remaining = this->expected_ - this->received_;
  uint8_t rb = this->read_status_(RXBYTES);
  if (rb == 0xFF) return false;
  if (rb & 0x80) {
    this->stats.overflows++;
    this->state_ = State::INIT;
    return false;
  }
  size_t in_fifo = rb & 0x7F;
  bool ended = !this->gdo2_->digital_read();  // GDO2 opada na końcu pakietu
  if (!ended) {
    uint8_t m = this->read_status_(MARCSTATE) & 0x1F;
    ended = m == MARC_IDLE || m == MARC_RX_END;
  }
  // W trakcie odbioru zostawiamy ostatni bajt w FIFO (errata CC1101), chyba że to już koniec ramki.
  size_t n = std::min(in_fifo, remaining);
  if (!ended && n == in_fifo && n < remaining && n > 0) n--;
  if (n > 0) {
    this->read_fifo_(this->raw_ + this->received_, n);
    this->received_ += n;
  }
  if (this->received_ >= this->expected_) return true;
  if (ended) {
    // Ostatnie bajty mogą jeszcze spływać do FIFO po przejściu w IDLE.
    for (int i = 0; i < 5 && this->received_ < this->expected_; i++) {
      size_t more = std::min<size_t>(this->read_status_(RXBYTES) & 0x7F, this->expected_ - this->received_);
      if (more) {
        this->read_fifo_(this->raw_ + this->received_, more);
        this->received_ += more;
      } else {
        delayMicroseconds(100);
      }
    }
    if (this->received_ >= this->expected_) return true;
    this->stats.truncated++;
    this->state_ = State::INIT;
  }
  return false;
}

bool Cc1101::finish_(RadioFrame &out) {
  size_t len;
  if (this->mode_ == 'T') {
    uint8_t l_dec[1];
    decode_3of6(this->raw_, 3, l_dec, 1);
    size_t want = format_a_size(l_dec[0]);
    if (want > sizeof(this->raw_) || decode_3of6(this->raw_, this->received_, this->raw_, want) < want) {
      this->stats.code_errors++;
      return false;
    }
    len = want;
    if (!strip_crc(this->raw_, len, FrameFormat::A)) {
      this->stats.crc_errors++;
      return false;
    }
  } else {
    len = this->received_ - this->offset_;
    memmove(this->raw_, this->raw_ + this->offset_, len);
    if (!strip_crc(this->raw_, len, this->format_ == 'A' ? FrameFormat::A : FrameFormat::B)) {
      this->stats.crc_errors++;
      return false;
    }
  }
  if (len > sizeof(out.data)) {
    this->stats.crc_errors++;
    return false;
  }
  memcpy(out.data, this->raw_, len);
  out.len = static_cast<uint16_t>(len);
  out.rssi = this->rssi_;
  out.mode = this->mode_;
  out.format = this->format_;
  this->stats.frames++;
  return true;
}

}  // namespace esphome::sihos16
