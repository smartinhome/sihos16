// sihos16 — parser rekordów danych DIF/VIF (EN 13757-3) i ich znaczenie.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sihos::wmb {

enum class Func : uint8_t { INST, MAX, MIN, ERR };

// Wielkość fizyczna rekordu. Wartość rekordu jest zawsze w jednostce bazowej podanej w komentarzu.
enum class Q : uint8_t {
  NONE,
  ENERGY_WH,        // Wh
  ENERGY_J,         // J
  VOLUME,           // m3
  MASS,             // kg
  ON_TIME,          // s
  OP_TIME,          // s
  POWER,            // W
  POWER_JH,         // J/h
  VOLUME_FLOW,      // m3/h
  MASS_FLOW,        // kg/h
  FLOW_TEMP,        // °C
  RETURN_TEMP,      // °C
  TEMP_DIFF,        // K
  EXT_TEMP,         // °C
  PRESSURE,         // bar
  DATE,             // tekst
  DATETIME,         // tekst
  HCA,              // jednostki podzielnika
  AVG_DURATION,     // s
  ACT_DURATION,     // s
  FABRICATION_NO,   // tekst
  ENHANCED_ID,      // tekst
  BUS_ADDRESS,
  VOLTAGE,          // V
  CURRENT,          // A
  REACTIVE_ENERGY,  // varh
  REACTIVE_POWER,   // var
  ERROR_FLAGS,      // tekst (hex)
  ACCESS_NO,
  MODEL_VERSION,
  HW_VERSION,
  FW_VERSION,
  SW_VERSION,
  CUSTOMER_LOCATION,
  CUSTOMER,
  DIMENSIONLESS,
  RSSI,             // dBm
  BATTERY_DAYS,     // dni do końca baterii
  BATTERY_CHANGE,   // tekst (data)
  BATTERY_TIME,     // s pracy na baterii
  STORAGE_INTERVAL, // s
  SINCE_READOUT,    // s
  RESET_COUNTER,
  CUMULATION_COUNTER,
  DIGITAL_INPUT,
  DIGITAL_OUTPUT,
  TEXT,             // VIF tekstowy 0x7C
  MFCT,             // VIF producenta 0x7F/0xFF
  UNKNOWN,
};

struct Record {
  uint16_t offset{0};       // położenie DIF w danych
  std::string difvif;       // bajty DIF..VIFE jako hex, np. "0BABC8FC10" (do dopasowań sterowników)
  uint8_t dif{0};
  uint8_t data_type{0};     // dolne 4 bity DIF
  Func func{Func::INST};
  uint32_t storage{0};
  uint16_t tariff{0};
  uint16_t subunit{0};
  Q q{Q::NONE};

  // Modyfikatory z VIFE łączonych
  bool backward{false};     // 0x3C: tylko ujemne przyrosty (energia oddana / przepływ wsteczny)
  bool forward{false};      // 0x3B: tylko dodatnie przyrosty
  bool upper_limit{false};  // 0x48: górny limit
  bool lower_limit{false};  // 0x40: dolny limit
  uint8_t phase{0};         // 0x7C 0x01..0x03 lub 0xFC 0x01..0x03: faza L1..L3
  bool vife_unknown{false}; // VIFE, którego nie rozumiemy — pole i tak trafia do wyniku

  std::vector<uint8_t> data;  // surowe bajty danych
  bool numeric{false};
  bool valid{true};           // false: BCD z cyframi A–F (licznik zgłasza "brak wartości") albo flaga IV daty
  double value{0};            // w jednostce bazowej wielkości `q`
  std::string text;           // daty, numery, teksty
};

struct Parsed {
  std::vector<Record> records;
  std::vector<uint8_t> mfct_data;  // dane po DIF 0x0F/0x1F
  bool truncated{false};
};

// Parsuje rekordy z odszyfrowanych danych (bez nagłówka TPL). Wypełniacze 0x2F są pomijane.
Parsed parse_records(const uint8_t *p, size_t len);

// Wartość liczbowa danych wg typu z DIF; false gdy typ nie jest liczbą.
bool decode_number(uint8_t data_type, const std::vector<uint8_t> &d, double &out, bool &valid);

// Data typu G (2 bajty), F (4 bajty) lub I (6 bajtów) jako "RRRR-MM-DD[ GG:MM[:SS]]".
bool decode_datetime(const std::vector<uint8_t> &d, std::string &out, bool &valid);

}  // namespace sihos::wmb
