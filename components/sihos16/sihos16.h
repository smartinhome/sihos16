// sihos16 — komponent ESPHome: radio CC1101, dekoder wM-Bus, liczniki z pamięci modułu jako urządzenia HA.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include <string>
#include <vector>

#include "esphome/components/web_server_base/web_server_base.h"
#ifdef USE_LIGHT
#include "esphome/components/light/light_state.h"
#endif
#ifdef USE_OUTPUT
#include "esphome/components/output/binary_output.h"
#endif
#include "esphome/core/component.h"
#include "esphome/core/device.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "meters.h"
#include "radio_cc1101.h"
#include "wmb_types.h"

namespace esphome::sihos16 {

constexpr int MAX_HEARD = 32;  // liczniki „w okolicy” pamiętane do podglądu

struct MeterState {
  uint32_t last_ms{0};
  uint32_t frames{0};
  int8_t rssi{0};
  sihos::wmb::Status status{sihos::wmb::Status::OK};
  std::vector<uint8_t> frame;  // ostatnia ramka (podgląd, ponowne dekodowanie)
  sihos::wmb::Result result;
};

struct Heard {
  sihos::wmb::Address addr;
  uint32_t last_ms{0};
  uint32_t count{0};
  int8_t rssi{0};
  sihos::wmb::Status status{sihos::wmb::Status::OK};
  sihos::wmb::Media media{sihos::wmb::Media::OTHER};
  const char *driver{"oms"};
  std::vector<uint8_t> frame;
};

// Ustawienia diod (NVS). Zmiana ze strony działa od razu, bez restartu.
struct LedConfig {
  uint8_t rgb_on{1};       // dioda RGB pokazuje stan modułu
  uint8_t brightness{40};  // 1–100 %
  uint8_t blink{1};        // dioda ramek: 0 wyłączona, 1 ramki moich liczników, 2 wszystkie ramki
  uint8_t version{1};
};

struct WifiNet {
  std::string ssid;
  int8_t rssi;
  bool lock;
};

class Sihos16 : public Component, public SpiDev, public AsyncWebHandler {
 public:
  // --- konfiguracja z YAML / codegen ---
  void set_gdo0_pin(InternalGPIOPin *p) { this->gdo0_ = p; }
  void set_gdo2_pin(InternalGPIOPin *p) { this->gdo2_ = p; }
  void set_frequency(float mhz) { this->mhz_ = mhz; }
  void set_web_base(web_server_base::WebServerBase *b) { this->web_base_ = b; }
  void set_device(int slot, Device *d) { this->devices_[slot] = d; }
  void set_num_entity(int slot, int i, SlotSensor *s) { this->nums_[slot][i] = s; }
  void set_text_entity(int slot, int i, SlotText *t) { this->texts_[slot][i] = t; }
  void add_unit(const char *u, uint8_t idx) { this->units_.push_back({u, idx}); }
  void add_device_class(const char *dc, uint8_t idx) { this->dcs_.push_back({dc, idx}); }
#ifdef USE_LIGHT
  void set_rgb_led(light::LightState *l) { this->rgb_ = l; }
#endif
#ifdef USE_OUTPUT
  void set_status_led(output::BinaryOutput *o) { this->status_led_ = o; }
#endif
  void set_reset_pin(InternalGPIOPin *p) { this->reset_pin_ = p; }

  // --- Component ---
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // --- AsyncWebHandler: strona i API pod /sihos16 ---
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  struct NameIdx {
    const char *name;
    uint8_t idx;
  };

  // radio
  static void radio_task_(void *arg);
  static void IRAM_ATTR gdo_isr_(Sihos16 *self);
  void handle_frame_(const RadioFrame &f);
  void log_stats_();

  // liczniki
  void load_meters_();
  void bind_entities_();
  void publish_(int slot);
  int find_meter_(const sihos::wmb::Address &a) const;
  uint8_t unit_idx_(const char *unit) const;
  uint8_t dc_idx_(const char *dc) const;

  // web
  void send_state_(AsyncWebServerRequest *req);
  void send_preview_(AsyncWebServerRequest *req);
  void handle_save_(AsyncWebServerRequest *req);
  void handle_delete_(AsyncWebServerRequest *req);
  void send_system_(AsyncWebServerRequest *req);
  void handle_wifi_(AsyncWebServerRequest *req);
  void handle_leds_(AsyncWebServerRequest *req);
  void factory_reset_();

  // diody i przycisk
  void update_leds_(uint32_t now);
  void blink_(uint32_t now);
  void check_reset_button_(uint32_t now);
  void refresh_wifi_cache_();

  InternalGPIOPin *gdo0_{nullptr};
  InternalGPIOPin *gdo2_{nullptr};
  float mhz_{868.95f};
  web_server_base::WebServerBase *web_base_{nullptr};
  bool web_started_{false};

  Cc1101 radio_;
  QueueHandle_t queue_{nullptr};
  TaskHandle_t task_{nullptr};
  RadioFrame frame_;
  uint32_t last_stats_ms_{0};

  Device *devices_[MAX_METERS]{};
  SlotSensor *nums_[MAX_METERS][MAX_NUM_FIELDS]{};
  SlotText *texts_[MAX_METERS][MAX_TEXT_FIELDS]{};
  int8_t num_field_[MAX_METERS][MAX_NUM_FIELDS];    // indeks pola w MeterConfig::fields, -1 = wolna
  int8_t text_field_[MAX_METERS][MAX_TEXT_FIELDS];
  std::vector<NameIdx> units_;
  std::vector<NameIdx> dcs_;

  MeterConfig cfg_[MAX_METERS]{};
  ESPPreferenceObject prefs_[MAX_METERS];
  MeterState state_[MAX_METERS];
  std::vector<Heard> heard_;
  Mutex lock_;  // stan liczników i lista „w okolicy” są czytane przez serwer WWW z innego zadania

  // Zmiany ze strony wykonuje pętla główna (zapis NVS, restart).
  bool pending_save_{false};
  int pending_slot_{-1};
  MeterConfig pending_cfg_{};
  uint32_t reboot_at_{0};

#ifdef USE_LIGHT
  light::LightState *rgb_{nullptr};
#endif
#ifdef USE_OUTPUT
  output::BinaryOutput *status_led_{nullptr};
#endif
  InternalGPIOPin *reset_pin_{nullptr};
  LedConfig leds_;
  ESPPreferenceObject led_pref_;
  bool pending_leds_{false};
  uint32_t led_color_{0xFFFFFFFF};  // ostatnio ustawiony kolor RGB (0xRRGGBB, bit 24 = wyłączona)
  uint32_t last_led_ms_{0};
  uint32_t blink_off_at_{0};
  uint32_t reset_pressed_ms_{0};
  bool resetting_{false};
  std::vector<WifiNet> wifi_cache_;
  uint32_t wifi_cache_ms_{0};
  // Stan WiFi odczytany w pętli głównej (strona czyta go z zadania serwera WWW).
  bool wifi_connected_{false};
  bool wifi_ap_{false};
  std::string wifi_ssid_;
  std::string wifi_ip_;
  int8_t wifi_rssi_{0};
};

}  // namespace esphome::sihos16
