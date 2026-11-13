// sihos16 — parser rekordów danych DIF/VIF (EN 13757-3) i ich znaczenie.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "wmb_records.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace sihos::wmb {

namespace {

// Długość danych dla typów 0x0..0xE (0xD = zmienna, liczona osobno).
constexpr int8_t DATA_LEN[16] = {0, 1, 2, 3, 4, 4, 6, 8, 0, 1, 2, 3, 4, -1, 6, -1};

constexpr double TIME_UNIT[4] = {1, 60, 3600, 86400};

double p10(int e) { return std::pow(10.0, e); }

struct VifInfo {
  Q q{Q::UNKNOWN};
  double scale{1};  // mnożnik do jednostki bazowej
};

VifInfo primary_vif(uint8_t v) {
  uint8_t n3 = v & 0x07, n2 = v & 0x03;
  switch (v & 0x78) {
    case 0x00: return {Q::ENERGY_WH, p10(n3 - 3)};
    case 0x08: return {Q::ENERGY_J, p10(n3)};
    case 0x10: return {Q::VOLUME, p10(n3 - 6)};
    case 0x18: return {Q::MASS, p10(n3 - 3)};
    case 0x28: return {Q::POWER, p10(n3 - 3)};
    case 0x30: return {Q::POWER_JH, p10(n3)};
    case 0x38: return {Q::VOLUME_FLOW, p10(n3 - 6)};
    case 0x40: return {Q::VOLUME_FLOW, p10(n3 - 7) * 60};     // m3/min -> m3/h
    case 0x48: return {Q::VOLUME_FLOW, p10(n3 - 9) * 3600};   // m3/s -> m3/h
    case 0x50: return {Q::MASS_FLOW, p10(n3 - 3)};
    default: break;
  }
  switch (v & 0x7C) {
    case 0x20: return {Q::ON_TIME, TIME_UNIT[n2]};
    case 0x24: return {Q::OP_TIME, TIME_UNIT[n2]};
    case 0x58: return {Q::FLOW_TEMP, p10(n2 - 3)};
    case 0x5C: return {Q::RETURN_TEMP, p10(n2 - 3)};
    case 0x60: return {Q::TEMP_DIFF, p10(n2 - 3)};
    case 0x64: return {Q::EXT_TEMP, p10(n2 - 3)};
    case 0x68: return {Q::PRESSURE, p10(n2 - 3)};
    case 0x70: return {Q::AVG_DURATION, TIME_UNIT[n2]};
    case 0x74: return {Q::ACT_DURATION, TIME_UNIT[n2]};
    default: break;
  }
  switch (v) {
    case 0x6C: return {Q::DATE, 1};
    case 0x6D: return {Q::DATETIME, 1};
    case 0x6E: return {Q::HCA, 1};
    case 0x78: return {Q::FABRICATION_NO, 1};
    case 0x79: return {Q::ENHANCED_ID, 1};
    case 0x7A: return {Q::BUS_ADDRESS, 1};
    default: return {Q::UNKNOWN, 1};
  }
}

// Rozszerzenie 0xFD (EN 13757-3, tabela 14).
VifInfo fd_vif(uint8_t v) {
  uint8_t n2 = v & 0x03;
  if ((v & 0x70) == 0x40) return {Q::VOLTAGE, p10((v & 0x0F) - 9)};
  if ((v & 0x70) == 0x50) return {Q::CURRENT, p10((v & 0x0F) - 12)};
  switch (v & 0x7C) {
    case 0x24: return {Q::STORAGE_INTERVAL, TIME_UNIT[n2]};
    case 0x2C: return {Q::SINCE_READOUT, TIME_UNIT[n2]};
    case 0x6C: return {Q::BATTERY_TIME, TIME_UNIT[n2]};
    default: break;
  }
  switch (v) {
    case 0x08: return {Q::ACCESS_NO, 1};
    case 0x0C: return {Q::MODEL_VERSION, 1};
    case 0x0D: return {Q::HW_VERSION, 1};
    case 0x0E: return {Q::FW_VERSION, 1};
    case 0x0F: return {Q::SW_VERSION, 1};
    case 0x10: return {Q::CUSTOMER_LOCATION, 1};
    case 0x11: return {Q::CUSTOMER, 1};
    case 0x17: return {Q::ERROR_FLAGS, 1};
    case 0x1A: return {Q::DIGITAL_OUTPUT, 1};
    case 0x1B: return {Q::DIGITAL_INPUT, 1};
    case 0x28: return {Q::STORAGE_INTERVAL, 30.44 * 86400};
    case 0x29: return {Q::STORAGE_INTERVAL, 365.25 * 86400};
    case 0x3A: return {Q::DIMENSIONLESS, 1};
    case 0x60: return {Q::RESET_COUNTER, 1};
    case 0x61: return {Q::CUMULATION_COUNTER, 1};
    case 0x70: return {Q::BATTERY_CHANGE, 1};
    case 0x71: return {Q::RSSI, 1};
    case 0x74: return {Q::BATTERY_DAYS, 1};
    default: return {Q::UNKNOWN, 1};
  }
}

// Rozszerzenie 0xFB (EN 13757-3, tabela 12) — tylko pozycje spotykane w licznikach.
VifInfo fb_vif(uint8_t v) {
  uint8_t n1 = v & 0x01, n2 = v & 0x03;
  switch (v & 0x7E) {
    case 0x00: return {Q::ENERGY_WH, p10(n1 + 5)};   // 0,1 MWh .. 1 MWh
    case 0x02: return {Q::REACTIVE_ENERGY, p10(n1 + 3)};  // kvarh
    case 0x08: return {Q::ENERGY_J, p10(n1 + 8)};    // 0,1 GJ .. 1 GJ
    case 0x10: return {Q::VOLUME, p10(n1 + 2)};
    case 0x18: return {Q::MASS, p10(n1 + 5)};
    case 0x28: return {Q::POWER, p10(n1 + 5)};
    case 0x30: return {Q::POWER_JH, p10(n1 + 8)};
    default: break;
  }
  if ((v & 0x7C) == 0x14) return {Q::REACTIVE_POWER, p10(n2)};
  return {Q::UNKNOWN, 1};
}

void put_hex(std::string &s, uint8_t b) {
  static const char H[] = "0123456789ABCDEF";
  s += H[b >> 4];
  s += H[b & 0x0F];
}

}  // namespace

bool decode_number(uint8_t t, const std::vector<uint8_t> &d, double &out, bool &valid) {
  valid = true;
  size_t n = d.size();
  switch (t) {
    case 0x1: case 0x2: case 0x3: case 0x4: case 0x6: case 0x7: {
      uint64_t u = 0;
      for (size_t i = 0; i < n; i++) u |= static_cast<uint64_t>(d[i]) << (8 * i);
      if (n < 8 && (d[n - 1] & 0x80)) u |= ~0ULL << (8 * n);  // rozszerzenie znaku
      out = static_cast<double>(static_cast<int64_t>(u));
      return true;
    }
    case 0x5: {
      float f;
      memcpy(&f, d.data(), 4);
      out = f;
      valid = std::isfinite(f);
      return true;
    }
    case 0x9: case 0xA: case 0xB: case 0xC: case 0xE: {
      double v = 0;
      bool neg = false;
      for (size_t i = n; i-- > 0;) {
        uint8_t hi = d[i] >> 4, lo = d[i] & 0x0F;
        if (i == n - 1 && hi == 0x0F) {
          neg = true;
          hi = 0;
        }
        if (hi > 9 || lo > 9) valid = false;
        v = v * 100 + hi * 10 + lo;
      }
      out = neg ? -v : v;
      return true;
    }
    default:
      return false;
  }
}

bool decode_datetime(const std::vector<uint8_t> &d, std::string &out, bool &valid) {
  char buf[24];
  valid = true;
  if (d.size() == 2) {
    int day = d[0] & 0x1F, month = d[1] & 0x0F;
    int year = ((d[0] >> 5) & 0x07) | ((d[1] >> 4) << 3);
    valid = !(d[0] == 0xFF && d[1] == 0xFF) && day != 0 && month >= 1 && month <= 12;
    snprintf(buf, sizeof buf, "%04d-%02d-%02d", 2000 + year, month, day);
  } else if (d.size() == 4) {
    int min = d[0] & 0x3F, hour = d[1] & 0x1F, day = d[2] & 0x1F, month = d[3] & 0x0F;
    int year = ((d[2] >> 5) & 0x07) | ((d[3] >> 4) << 3);
    valid = !(d[0] & 0x80) && day != 0 && month >= 1 && month <= 12;
    snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d", 2000 + year, month, day, hour, min);
  } else if (d.size() == 6) {
    int sec = d[0] & 0x3F, min = d[1] & 0x3F, hour = d[2] & 0x1F, day = d[3] & 0x1F, month = d[4] & 0x0F;
    int year = ((d[3] >> 5) & 0x07) | ((d[4] >> 4) << 3);
    valid = day != 0 && month >= 1 && month <= 12;
    snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d", 2000 + year, month, day, hour, min, sec);
  } else {
    return false;
  }
  out = buf;
  return true;
}

static void set_text_from_data(Record &r) {
  // Numer seryjny i podobne: BCD jako cyfry (z zerami wiodącymi), inaczej hex od najstarszego bajtu.
  r.text.clear();
  if (r.data_type == 0xD) {  // zmienna długość: tekst ASCII zapisany od końca
    bool printable = !r.data.empty();
    for (uint8_t c : r.data)
      if (c < 0x20 || c > 0x7E) printable = false;
    if (printable) {
      for (size_t i = r.data.size(); i-- > 0;) r.text += static_cast<char>(r.data[i]);
      return;
    }
  }
  bool bcd = r.data_type >= 0x9 && r.data_type <= 0xE && r.data_type != 0xD;
  if (bcd || r.q == Q::FABRICATION_NO || r.q == Q::ENHANCED_ID) {
    if (!bcd) {
      double v;
      bool ok;
      if (decode_number(r.data_type, r.data, v, ok)) {
        char b[24];
        snprintf(b, sizeof b, "%.0f", v);
        r.text = b;
        return;
      }
    }
    for (size_t i = r.data.size(); i-- > 0;) put_hex(r.text, r.data[i]);
    return;
  }
  for (size_t i = r.data.size(); i-- > 0;) put_hex(r.text, r.data[i]);
}

Parsed parse_records(const uint8_t *p, size_t len) {
  Parsed out;
  size_t i = 0;
  while (i < len) {
    uint8_t dif = p[i];
    if (dif == 0x2F) {  // wypełniacz
      i++;
      continue;
    }
    if (dif == 0x0F || dif == 0x1F) {  // dane producenta do końca
      out.mfct_data.assign(p + i + 1, p + len);
      break;
    }
    if (dif == 0x7F) break;  // odczyt globalny — brak danych

    Record r;
    r.offset = static_cast<uint16_t>(i);
    size_t start = i++;
    r.dif = dif;
    r.data_type = dif & 0x0F;
    r.func = static_cast<Func>((dif >> 4) & 0x03);
    r.storage = (dif >> 6) & 0x01;

    bool ext = dif & 0x80;
    for (int k = 0; ext; k++) {
      if (i >= len || k >= 10) return out.truncated = true, out;
      uint8_t dife = p[i++];
      r.storage |= static_cast<uint32_t>(dife & 0x0F) << (1 + 4 * k);
      r.tariff |= static_cast<uint16_t>((dife >> 4) & 0x03) << (2 * k);
      r.subunit |= static_cast<uint16_t>((dife >> 6) & 0x01) << k;
      ext = dife & 0x80;
    }

    if (i >= len) return out.truncated = true, out;
    uint8_t vif = p[i++];
    uint8_t v = vif & 0x7F;
    ext = vif & 0x80;
    VifInfo vi;
    std::string plain;
    if (v == 0x7B || v == 0x7D) {
      if (i >= len) return out.truncated = true, out;
      uint8_t e = p[i++];
      vi = v == 0x7B ? fb_vif(e & 0x7F) : fd_vif(e & 0x7F);
      ext = e & 0x80;
    } else if (v == 0x7C) {
      if (i >= len) return out.truncated = true, out;
      uint8_t n = p[i++];
      if (i + n > len) return out.truncated = true, out;
      for (size_t k = n; k-- > 0;) plain += static_cast<char>(p[i + k]);
      i += n;
      vi = {Q::TEXT, 1};
    } else if (v == 0x7F || v == 0x7E) {
      vi = {v == 0x7F ? Q::MFCT : Q::UNKNOWN, 1};
    } else {
      vi = primary_vif(v);
    }

    while (ext) {
      if (i >= len) return out.truncated = true, out;
      uint8_t e = p[i++];
      ext = e & 0x80;
      uint8_t c = e & 0x7F;
      if (c == 0x3C) {
        r.backward = true;
      } else if (c == 0x3B) {
        r.forward = true;
      } else if (c == 0x48) {
        r.upper_limit = true;
      } else if (c == 0x40) {
        r.lower_limit = true;
      } else if (c >= 0x70 && c <= 0x77) {
        vi.scale *= p10((c & 0x07) - 6);
      } else if (c == 0x7D) {
        vi.scale *= 1000;
      } else if (c == 0x7C) {  // rozszerzenie VIFE łączonych: faza
        if (i >= len) return out.truncated = true, out;
        uint8_t x = p[i++];
        ext = x & 0x80;
        uint8_t code = x & 0x7F;
        if (code >= 1 && code <= 3) r.phase = code;
        else r.vife_unknown = true;
      } else {
        r.vife_unknown = true;
      }
    }
    r.q = vi.q;
    for (size_t k = start; k < i; k++) put_hex(r.difvif, p[k]);

    // Dane
    size_t dlen;
    if (r.data_type == 0xD) {
      if (i >= len) return out.truncated = true, out;
      uint8_t lv = p[i++];
      if (lv <= 0xBF) dlen = lv;
      else if (lv >= 0xC0 && lv <= 0xC9) dlen = lv - 0xC0, r.data_type = 0xC0;  // BCD dodatni
      else if (lv >= 0xD0 && lv <= 0xD9) dlen = lv - 0xD0, r.data_type = 0xD0;  // BCD ujemny
      else if (lv >= 0xE0 && lv <= 0xEF) dlen = lv - 0xE0, r.data_type = 0xE0;  // liczba
      else if (lv >= 0xF0 && lv <= 0xF4) dlen = 4 * (lv - 0xEC), r.data_type = 0xE0;
      else return out.truncated = true, out;
    } else {
      int8_t dl = DATA_LEN[r.data_type];
      if (dl < 0) return out.truncated = true, out;
      dlen = static_cast<size_t>(dl);
    }
    if (i + dlen > len) return out.truncated = true, out;
    r.data.assign(p + i, p + i + dlen);
    i += dlen;

    // Wartość
    if (r.q == Q::TEXT) {
      r.text = plain;
      double v2;
      bool ok;
      if (decode_number(r.data_type, r.data, v2, ok)) {
        r.numeric = true;
        r.value = v2 * vi.scale;
        r.valid = ok;
      }
    } else if (r.q == Q::DATE || r.q == Q::DATETIME || r.q == Q::BATTERY_CHANGE) {
      if (!decode_datetime(r.data, r.text, r.valid)) set_text_from_data(r);
    } else if (r.q == Q::FABRICATION_NO || r.q == Q::ENHANCED_ID || r.q == Q::ERROR_FLAGS ||
               r.q == Q::DIGITAL_INPUT || r.q == Q::DIGITAL_OUTPUT || r.q == Q::MODEL_VERSION ||
               r.q == Q::HW_VERSION || r.q == Q::FW_VERSION || r.q == Q::SW_VERSION ||
               r.q == Q::CUSTOMER || r.q == Q::CUSTOMER_LOCATION) {
      set_text_from_data(r);
    } else if (r.data_type == 0xC0 || r.data_type == 0xD0 || r.data_type == 0xE0) {
      // zmienna długość: BCD albo liczba binarna
      bool ok = true;
      double v2 = 0;
      if (r.data_type == 0xE0) {
        uint64_t u = 0;
        for (size_t k = 0; k < r.data.size() && k < 8; k++) u |= static_cast<uint64_t>(r.data[k]) << (8 * k);
        v2 = static_cast<double>(u);
      } else {
        decode_number(0xC, r.data, v2, ok);
        if (r.data_type == 0xD0) v2 = -v2;
      }
      r.numeric = true;
      r.valid = ok;
      r.value = v2 * vi.scale;
    } else if (r.data_type == 0xD) {
      set_text_from_data(r);
    } else {
      double v2;
      bool ok;
      if (decode_number(r.data_type, r.data, v2, ok)) {
        r.numeric = true;
        r.valid = ok;
        r.value = v2 * vi.scale;
      }
    }
    out.records.push_back(std::move(r));
  }
  return out;
}

}  // namespace sihos::wmb
