// sihos16 — komponent ESPHome: radio CC1101 + dekoder wM-Bus.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "radio_cc1101.h"

namespace esphome::sihos16 {

class Sihos16 : public Component, public SpiDev {
 public:
  void set_gdo0_pin(InternalGPIOPin *p) { this->gdo0_ = p; }
  void set_gdo2_pin(InternalGPIOPin *p) { this->gdo2_ = p; }
  void set_frequency(float mhz) { this->mhz_ = mhz; }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  static void radio_task_(void *arg);
  static void IRAM_ATTR gdo_isr_(Sihos16 *self);
  void handle_frame_(const RadioFrame &f);
  void log_stats_();

  InternalGPIOPin *gdo0_{nullptr};
  InternalGPIOPin *gdo2_{nullptr};
  float mhz_{868.95f};

  Cc1101 radio_;
  QueueHandle_t queue_{nullptr};
  TaskHandle_t task_{nullptr};
  RadioFrame frame_;  // bufor pętli głównej (duży — nie na stosie)
  uint32_t last_stats_ms_{0};
};

}  // namespace esphome::sihos16
