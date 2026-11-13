// sihos16 — komponent ESPHome: radio CC1101 + dekoder wM-Bus.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "sihos16.h"

#include <cinttypes>

#include "esphome/core/log.h"
#include "wmb_decoder.h"

namespace esphome::sihos16 {

static const char *const TAG = "sihos16";

static constexpr UBaseType_t QUEUE_DEPTH = 8;
static constexpr uint32_t STACK = 4096;
static constexpr UBaseType_t PRIORITY = 3;  // nad pętlą ESPHome (1): długa operacja w pętli nie gubi ramek
static constexpr uint32_t STATS_EVERY_MS = 60000;

void Sihos16::setup() {
  this->gdo0_->setup();
  this->gdo0_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLDOWN);
  this->gdo2_->setup();
  this->gdo2_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLDOWN);
  this->spi_setup();

  if (!this->radio_.begin(this, this->gdo0_, this->gdo2_, this->mhz_)) {
    this->status_set_error(LOG_STR("CC1101 nie odpowiada"));
    this->mark_failed();
    return;
  }

  this->queue_ = xQueueCreate(QUEUE_DEPTH, sizeof(RadioFrame));
  BaseType_t ok = xTaskCreatePinnedToCore(Sihos16::radio_task_, "sihos16_radio", STACK, this, PRIORITY,
                                          &this->task_, portNUM_PROCESSORS > 1 ? 1 : 0);
  if (this->queue_ == nullptr || ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udało się uruchomić zadania radia");
    this->mark_failed();
    return;
  }
  // GDO0: FIFO zapełnione do progu, GDO2: synchronizacja. Oba budzą zadanie od razu.
  this->gdo0_->attach_interrupt(Sihos16::gdo_isr_, this, gpio::INTERRUPT_RISING_EDGE);
  this->gdo2_->attach_interrupt(Sihos16::gdo_isr_, this, gpio::INTERRUPT_RISING_EDGE);
}

void IRAM_ATTR Sihos16::gdo_isr_(Sihos16 *self) {
  BaseType_t woken = pdFALSE;
  if (self->task_ != nullptr) vTaskNotifyGiveFromISR(self->task_, &woken);
  portYIELD_FROM_ISR(woken);
}

void Sihos16::radio_task_(void *arg) {
  auto *self = static_cast<Sihos16 *>(arg);
  static RadioFrame f;  // jedno zadanie radia — bufor poza stosem
  for (;;) {
    if (self->radio_.poll(f)) {
      if (xQueueSend(self->queue_, &f, 0) != pdTRUE) self->radio_.stats.dropped++;
      continue;
    }
    // W trakcie ramki sprawdzamy co 1 ms; w spoczynku czekamy na przerwanie (z zapasem 2 ms).
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(self->radio_.busy() ? 1 : 2));
  }
}

void Sihos16::loop() {
  if (this->queue_ == nullptr) return;
  // Wszystkie ramki z kolejki w jednym przebiegu pętli.
  while (xQueueReceive(this->queue_, &this->frame_, 0) == pdTRUE) this->handle_frame_(this->frame_);

  uint32_t now = millis();
  if (now - this->last_stats_ms_ >= STATS_EVERY_MS) {
    this->last_stats_ms_ = now;
    this->log_stats_();
  }
}

void Sihos16::handle_frame_(const RadioFrame &f) {
  using namespace sihos::wmb;
  Result r = decode(f.data, f.len, nullptr, "auto");
  ESP_LOGI(TAG, "%c%c %4d dBm  %s %08" PRIx32 "  typ %02X wersja %02X (%s)  %u B  sterownik %s: %s, pól %u", f.mode,
           f.format, f.rssi, mfct_str(r.addr.mfct).c_str(), r.addr.id, r.addr.type, r.addr.version,
           media_str(r.media), static_cast<unsigned>(f.len), r.driver, status_str(r.status), static_cast<unsigned>(r.fields.size()));
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG
  char hex[2 * MAX_FRAME + 1];
  for (size_t i = 0; i < f.len; i++) snprintf(hex + 2 * i, 3, "%02x", f.data[i]);
  ESP_LOGD(TAG, "  ramka %s", hex);
  for (const Field &fl : r.fields) {
    if (fl.is_text) ESP_LOGD(TAG, "  %s = %s", fl.label.c_str(), fl.text.c_str());
    else ESP_LOGD(TAG, "  %s = %.3f %s", fl.label.c_str(), fl.value, fl.unit);
  }
#endif
}

void Sihos16::log_stats_() {
  const RadioStats &s = this->radio_.stats;
  ESP_LOGI(TAG, "Radio: ramki %u, błędne CRC %u, błędy 3-of-6 %u, urwane %u, przepełnienia FIFO %u, utracone %u",
           (unsigned) s.frames, (unsigned) s.crc_errors, (unsigned) s.code_errors, (unsigned) s.truncated,
           (unsigned) s.overflows, (unsigned) s.dropped);
}

void Sihos16::dump_config() {
  ESP_LOGCONFIG(TAG, "SIHOS16 wM-Bus:");
  LOG_PIN("  CS: ", this->cs_);
  LOG_PIN("  GDO0: ", this->gdo0_);
  LOG_PIN("  GDO2: ", this->gdo2_);
  ESP_LOGCONFIG(TAG, "  Częstotliwość: %.3f MHz", this->mhz_);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  CC1101 nie wykryty");
  } else {
    ESP_LOGCONFIG(TAG, "  CC1101 wersja 0x%02X", this->radio_.version());
  }
}

}  // namespace esphome::sihos16
