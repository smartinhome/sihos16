// sihos16 — zamiana rekordów DIF/VIF na pola z polską nazwą i jednostką.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "wmb_naming.h"

#include <cstdio>

namespace sihos::wmb {

namespace {

struct Out {
  const char *key;
  const char *label;
  const char *unit;
  double factor;  // z jednostki bazowej do wyjściowej
};

bool is_water(Media m) {
  return m == Media::WATER || m == Media::WARM_WATER || m == Media::HOT_WATER || m == Media::COLD_WATER ||
         m == Media::DUAL_WATER || m == Media::WASTE_WATER;
}

Out base_for(const Record &r, Media m) {
  const bool el = m == Media::ELECTRICITY;
  const bool back = r.backward;
  switch (r.q) {
    case Q::ENERGY_WH:
      if (el) return {"energy", back ? "Energia oddana" : "Energia pobrana", "kWh", 1e-3};
      return {"energy", back ? "Energia (wsteczna)" : "Energia", "kWh", 1e-3};
    case Q::ENERGY_J:
      return {"energy", back ? "Energia (wsteczna)" : "Energia", "GJ", 1e-9};
    case Q::VOLUME:
      if (back) return {"volume", "Przepływ wsteczny", "m³", 1};
      if (is_water(m)) return {"volume", "Zużycie wody", "m³", 1};
      if (m == Media::GAS) return {"volume", "Zużycie gazu", "m³", 1};
      return {"volume", "Objętość", "m³", 1};
    case Q::MASS: return {"mass", "Masa", "kg", 1};
    case Q::ON_TIME: return {"on_time", "Czas pracy", "h", 1.0 / 3600};
    case Q::OP_TIME: return {"operating_time", "Czas działania", "h", 1.0 / 3600};
    case Q::POWER:
      if (el) return {"power", back ? "Moc oddawana" : "Moc pobierana", "W", 1};
      return {"power", back ? "Moc (wsteczna)" : "Moc", "W", 1};
    case Q::POWER_JH: return {"power", "Moc", "W", 1.0 / 3600};
    case Q::VOLUME_FLOW: return {"flow", back ? "Przepływ wsteczny chwilowy" : "Przepływ", "m³/h", 1};
    case Q::MASS_FLOW: return {"mass_flow", "Przepływ masowy", "kg/h", 1};
    case Q::FLOW_TEMP: return {"flow_temperature", "Temperatura zasilania", "°C", 1};
    case Q::RETURN_TEMP: return {"return_temperature", "Temperatura powrotu", "°C", 1};
    case Q::TEMP_DIFF: return {"temperature_difference", "Różnica temperatur", "K", 1};
    case Q::EXT_TEMP:
      return {"temperature", m == Media::HCA ? "Temperatura otoczenia" : "Temperatura", "°C", 1};
    case Q::PRESSURE: return {"pressure", "Ciśnienie", "bar", 1};
    case Q::DATE: return {"date", "Data", "", 1};
    case Q::DATETIME: return {"datetime", "Data i czas licznika", "", 1};
    case Q::HCA: return {"hca", "Wskazanie podzielnika", "", 1};
    case Q::AVG_DURATION: return {"averaging_duration", "Czas uśredniania", "s", 1};
    case Q::ACT_DURATION: return {"actuality_duration", "Wiek odczytu", "s", 1};
    case Q::FABRICATION_NO: return {"fabrication_no", "Numer fabryczny", "", 1};
    case Q::ENHANCED_ID: return {"enhanced_id", "Identyfikator rozszerzony", "", 1};
    case Q::BUS_ADDRESS: return {"bus_address", "Adres magistrali", "", 1};
    case Q::VOLTAGE: return {"voltage", "Napięcie", "V", 1};
    case Q::CURRENT: return {"current", "Prąd", "A", 1};
    case Q::REACTIVE_ENERGY:
      return {"reactive_energy", back ? "Energia bierna (C)" : "Energia bierna (L)", "kvarh", 1e-3};
    case Q::REACTIVE_POWER: return {"reactive_power", back ? "Moc bierna (C)" : "Moc bierna (L)", "var", 1};
    case Q::ERROR_FLAGS: return {"error_flags", "Flagi błędów", "", 1};
    case Q::ACCESS_NO: return {"access_no", "Numer dostępu", "", 1};
    case Q::MODEL_VERSION: return {"model_version", "Model", "", 1};
    case Q::HW_VERSION: return {"hw_version", "Wersja sprzętu", "", 1};
    case Q::FW_VERSION: return {"fw_version", "Wersja oprogramowania", "", 1};
    case Q::SW_VERSION: return {"sw_version", "Wersja programu", "", 1};
    case Q::CUSTOMER_LOCATION: return {"customer_location", "Lokalizacja odbiorcy", "", 1};
    case Q::CUSTOMER: return {"customer", "Odbiorca", "", 1};
    case Q::DIMENSIONLESS: return {"value", "Wartość", "", 1};
    case Q::RSSI: return {"meter_rssi", "Sygnał odbierany przez licznik", "dBm", 1};
    case Q::BATTERY_DAYS: return {"battery_days", "Pozostały czas baterii", "d", 1};
    case Q::BATTERY_CHANGE: return {"battery_change", "Data wymiany baterii", "", 1};
    case Q::BATTERY_TIME: return {"battery_time", "Czas pracy na baterii", "h", 1.0 / 3600};
    case Q::STORAGE_INTERVAL: return {"storage_interval", "Okres zapisu", "s", 1};
    case Q::SINCE_READOUT: return {"since_readout", "Czas od odczytu", "s", 1};
    case Q::RESET_COUNTER: return {"reset_counter", "Licznik resetów", "", 1};
    case Q::CUMULATION_COUNTER: return {"cumulation_counter", "Licznik sumowań", "", 1};
    case Q::DIGITAL_INPUT: return {"digital_input", "Wejście cyfrowe", "", 1};
    case Q::DIGITAL_OUTPUT: return {"digital_output", "Wyjście cyfrowe", "", 1};
    default: return {nullptr, nullptr, "", 1};
  }
}

void append(std::string &s, const char *fmt, unsigned v) {
  char b[16];
  snprintf(b, sizeof b, fmt, v);
  s += b;
}

}  // namespace

Field field_from_record(const Record &r, Media media) {
  Field f;
  Out o = base_for(r, media);
  if (!o.key) {
    // Nieznany rekord: zostaje dostępny pod surowym kluczem DIF/VIF.
    f.key = "raw_" + r.difvif;
    f.label = "Rekord " + r.difvif;
    o.factor = 1;
  } else {
    f.key = o.key;
    f.label = o.label;
    f.unit = o.unit;
    // Kierunek dodawany do klucza zawsze (etykieta już go uwzględnia).
    if (r.backward) f.key += "_backward";
    if (r.forward) f.key += "_forward", f.label += " (tylko dodatnia)";
    if (r.upper_limit) f.key += "_limit", f.label += " – limit";
    if (r.lower_limit) f.key += "_lower_limit", f.label += " – limit dolny";
    if (r.vife_unknown) f.key += "_x" + r.difvif.substr(r.difvif.size() > 4 ? r.difvif.size() - 4 : 0);
  }
  switch (r.func) {
    case Func::MAX: f.key += "_max", f.label += " maks."; break;
    case Func::MIN: f.key += "_min", f.label += " min."; break;
    case Func::ERR: f.key += "_err", f.label += " (stan błędu)"; break;
    default: break;
  }
  if (r.phase) append(f.key, "_l%u", r.phase), append(f.label, " L%u", r.phase);
  if (r.tariff) append(f.key, "_t%u", r.tariff), append(f.label, " T%u", r.tariff);
  if (r.storage) append(f.key, "_s%u", r.storage), append(f.label, " (zapis %u)", r.storage);
  if (r.subunit) append(f.key, "_u%u", r.subunit), append(f.label, " (moduł %u)", r.subunit);

  // Jednostka w kluczu, żeby np. energia w kWh i GJ nie kolidowała.
  if (*f.unit) {
    std::string u;
    for (const char *c = f.unit; *c; c++) {
      unsigned char ch = static_cast<unsigned char>(*c);
      if (ch == 0xC2 || ch == 0xB3) {  // "³" -> 3, "°" pomijamy
        if (ch == 0xB3) u += '3';
        continue;
      }
      if (ch == 0xB0) continue;
      if (ch == '/') u += 'p';
      else u += static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch + 32 : ch);
    }
    f.key += "_" + u;
  }

  if (r.numeric && r.text.empty()) {
    f.value = r.value * o.factor;
  } else if (r.numeric) {  // VIF tekstowy z liczbą
    f.value = r.value;
    f.label = r.text;
  } else {
    f.is_text = true;
    f.text = r.text;
  }
  if (!r.valid) {  // licznik zgłasza brak wartości
    f.is_text = true;
    f.text = "brak";
  }
  return f;
}

void add_field(Result &res, Field f) {
  std::string base = f.key;
  for (int n = 2; res.find(f.key); n++) {
    char b[16];
    snprintf(b, sizeof b, "_%d", n);
    f.key = base + b;
  }
  res.fields.push_back(std::move(f));
}

std::string tpl_status_text(uint8_t st) {
  if ((st & 0x1F) == 0) return "OK";
  std::string s;
  auto add = [&s](const char *t) {
    if (!s.empty()) s += ", ";
    s += t;
  };
  switch (st & 0x03) {
    case 1: add("zajęty"); break;
    case 2: add("błąd aplikacji"); break;
    case 3: add("stan nietypowy"); break;
    default: break;
  }
  if (st & 0x04) add("słaba bateria");
  if (st & 0x08) add("błąd trwały");
  if (st & 0x10) add("błąd chwilowy");
  return s;
}

}  // namespace sihos::wmb
