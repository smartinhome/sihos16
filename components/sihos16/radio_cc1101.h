// sihos16 — odbiornik wM-Bus na CC1101 (tryby T1 i C1, formaty A i B).
// Maszyna stanów odbioru przeniesiona z smartinhome/esphome-components (sprawdzona na SIH Reader),
// uporządkowana: RSSI mierzone przy synchronizacji, CRC sprawdzane w radiu, liczniki błędów.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include <cstdint>

#include "esphome/components/spi/spi.h"
#include "esphome/core/hal.h"
#include "wmb_link.h"

namespace esphome::sihos16 {

using SpiDev = spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                              spi::DATA_RATE_2MHZ>;

// Ramka gotowa dla dekodera: bez preambuły i bez CRC, L = długość bez CRC.
struct RadioFrame {
  uint16_t len;
  int8_t rssi;
  char mode;    // 'T' albo 'C'
  char format;  // 'A' albo 'B'
  uint8_t data[sihos::wmb::MAX_FRAME];
};

struct RadioStats {
  uint32_t frames{0};      // poprawne ramki
  uint32_t crc_errors{0};  // odebrane, ale z błędnym CRC (zwykle szum lub kolizja)
  uint32_t code_errors{0}; // niedozwolony symbol 3-of-6
  uint32_t truncated{0};   // ramka urwana przed końcem
  uint32_t overflows{0};   // przepełnienie FIFO
  uint32_t dropped{0};     // kolejka pełna (wypełnia komponent)
};

class Cc1101 {
 public:
  // false gdy układ nie odpowiada po SPI.
  bool begin(SpiDev *spi, InternalGPIOPin *gdo0, InternalGPIOPin *gdo2, float mhz);
  // Jeden krok odbioru. true gdy w `out` jest gotowa ramka.
  bool poll(RadioFrame &out);
  // Czy warto od razu wywołać poll ponownie (trwa odbiór ramki).
  bool busy() const { return this->state_ != State::WAIT_SYNC; }

  uint8_t version() const { return this->version_; }
  RadioStats stats;

 protected:
  enum class State : uint8_t { INIT, WAIT_SYNC, HEADER, DATA };

  uint8_t strobe_(uint8_t cmd);
  uint8_t read_reg_(uint8_t addr);
  uint8_t read_status_(uint8_t addr);
  void write_reg_(uint8_t addr, uint8_t value);
  void read_fifo_(uint8_t *buf, size_t n);
  int8_t rssi_dbm_();
  void idle_();
  void start_rx_();
  bool read_header_();
  bool read_data_();
  bool finish_(RadioFrame &out);

  SpiDev *spi_{nullptr};
  InternalGPIOPin *gdo0_{nullptr};
  InternalGPIOPin *gdo2_{nullptr};
  uint8_t version_{0};

  State state_{State::INIT};
  uint8_t raw_[600];       // bajty po słowie synchronizacji (tryb T: zakodowane 3-of-6)
  size_t received_{0};
  size_t expected_{0};
  size_t offset_{0};       // preambuła trybu C (54 CD / 54 3D) przed polem L
  char mode_{'T'};
  char format_{'A'};
  int8_t rssi_{0};
  uint32_t sync_ms_{0};
};

}  // namespace esphome::sihos16
