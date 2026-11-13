// sihos16 — konfiguracja liczników (pamięć NVS) i ich encje w Home Assistant.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include <cstdint>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"

namespace esphome::sihos16 {

constexpr int MAX_METERS = 8;        // urządzeń w HA
constexpr int MAX_NUM_FIELDS = 16;   // encji liczbowych na licznik
constexpr int MAX_TEXT_FIELDS = 4;   // encji tekstowych na licznik
constexpr int MAX_FIELDS = MAX_NUM_FIELDS + MAX_TEXT_FIELDS;

// Pole wybrane do Home Assistant. Nazwa i jednostka zapamiętane, żeby encje
// powstały przy starcie, zanim przyjdzie pierwsza ramka.
struct SelField {
  char key[28];
  char label[40];
  char unit[8];
  uint8_t is_text;
};

// Jeden licznik w pamięci modułu (blob NVS). Zmiana układu = zmiana wersji klucza.
struct MeterConfig {
  uint8_t used;
  uint8_t has_key;
  uint8_t media;      // sihos::wmb::Media z ostatniego podglądu
  uint8_t nfields;
  uint32_t id;
  uint16_t mfct;      // 0 = dowolny producent
  uint16_t reserved;
  char driver[12];    // "auto" albo nazwa sterownika
  uint8_t key[16];
  char name[32];      // nazwa urządzenia w HA
  SelField fields[MAX_FIELDS];
};

// Encje z puli tworzonej przy kompilacji; nazwę, jednostkę i klasę dostają przy starcie.
class SlotSensor : public sensor::Sensor {
 public:
  void bind(const char *name, uint8_t uom_idx, uint8_t dc_idx, bool diagnostic) {
    uint32_t fields = static_cast<uint32_t>(dc_idx) | (static_cast<uint32_t>(uom_idx) << 8) |
                      (diagnostic ? (2u << 26) : 0u);
    this->configure_entity_(name, 0, fields);  // nazwa liczona w trakcie: nowy klucz encji
  }
};

class SlotText : public text_sensor::TextSensor {
 public:
  void bind(const char *name, bool diagnostic) { this->configure_entity_(name, 0, diagnostic ? (2u << 26) : 0u); }
};

}  // namespace esphome::sihos16
