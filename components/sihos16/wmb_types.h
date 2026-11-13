// sihos16 — lekki dekoder wM-Bus (EN 13757 / OMS).
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sihos::wmb {

enum class Status : uint8_t {
  OK,
  TOO_SHORT,        // ramka krótsza niż nagłówek albo niż deklaruje L
  NO_KEY,           // dane zaszyfrowane, brak klucza
  BAD_KEY,          // po odszyfrowaniu brak 2F2F: zły klucz
  UNSUPPORTED_CI,   // nieobsługiwana warstwa transportowa
  UNSUPPORTED_SEC,  // nieobsługiwany tryb szyfrowania
  BAD_RECORDS,      // rekordy DIF/VIF urwane w połowie
};

const char *status_str(Status s);

// Nośnik z bajtu typu urządzenia (EN 13757-3, tabela 3).
enum class Media : uint8_t {
  OTHER, OIL, ELECTRICITY, GAS, HEAT, STEAM, WARM_WATER, WATER, HCA, COMPRESSED_AIR,
  COOLING, HEAT_COOLING, BUS, UNKNOWN_MEDIA, HOT_WATER, COLD_WATER, DUAL_WATER,
  PRESSURE, AD_CONVERTER, SMOKE, ROOM_SENSOR, GAS_DETECTOR, BREAKER, VALVE, CUSTOMER_UNIT,
  WASTE_WATER, GARBAGE, RADIO_CONVERTER,
};

Media media_from_type(uint8_t type);
const char *media_str(Media m);

struct Address {
  uint16_t mfct{0};     // kod producenta (3 litery spakowane po 5 bitów)
  uint32_t id{0};       // numer z ramki, czytany jak liczba heksadecymalna (BCD)
  uint8_t version{0};
  uint8_t type{0};
};

std::string mfct_str(uint16_t mfct);  // "APA", "KPL", ...
uint16_t mfct_code(const char *three_letters);

// Jedno pole wyniku. `key` jest stałym identyfikatorem (konfiguracja, encje HA),
// `label` polską nazwą dla człowieka, `unit` jednostką wartości.
struct Field {
  std::string key;
  std::string label;
  const char *unit{""};
  double value{0};
  std::string text;   // gdy pole jest tekstem (data, numer, flagi)
  bool is_text{false};
};

struct Result {
  Status status{Status::OK};
  Address addr;
  Media media{Media::OTHER};
  const char *driver{"oms"};
  uint8_t access_no{0};
  uint8_t tpl_status{0};   // bajt statusu z nagłówka TPL (bity błędów licznika)
  uint8_t security{0};     // tryb szyfrowania 0/5/7
  std::vector<Field> fields;

  const Field *find(const std::string &key) const {
    for (const auto &f : fields)
      if (f.key == key) return &f;
    return nullptr;
  }
};

}  // namespace sihos::wmb
