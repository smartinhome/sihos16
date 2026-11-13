// sihos16 — sterowniki tylko dla liczników odbiegających od OMS.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "wmb_drivers.h"

#include <cstdio>
#include <cstring>

#include "wmb_naming.h"

namespace sihos::wmb {

namespace {

uint16_t mfct15(const Address &a) { return a.mfct & 0x7FFF; }

uint32_t le32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint32_t be32(const uint8_t *p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

Field num(const char *key, const char *label, const char *unit, double v) {
  Field f;
  f.key = key;
  f.label = label;
  f.unit = unit;
  f.value = v;
  return f;
}

Field txt(const char *key, const char *label, std::string t) {
  Field f;
  f.key = key;
  f.label = label;
  f.is_text = true;
  f.text = std::move(t);
  return f;
}

void join(std::string &s, const char *t) {
  if (!s.empty()) s += ", ";
  s += t;
}

// ===================== Sanxing =====================
// S34U28 (Tauron, PGE): zwykły licznik OMS z wadą sprawdzenia klucza (609B).

bool detect_sanxing(const Address &a) { return mfct15(a) == mfct_code("KPL") && a.type == 0x02; }

const Driver SANXING = {"sanxing", "Sanxing S34U28 (prąd)", detect_sanxing, nullptr, nullptr, QUIRK_SANXING_609B};

// ===================== Diehl: Izar / PRIOS =====================
// Ramki producenta (CI 0xA0–0xA7) z danymi zaszyfrowanymi generatorem LFSR.
// Klucz 32-bitowy: z klucza licznika albo jeden z dwóch domyślnych, zmieszany z nagłówkiem.

bool is_diehl(uint16_t m) {
  return m == mfct_code("DME") || m == mfct_code("EWT") || m == mfct_code("HYD") || m == mfct_code("SAP") ||
         m == mfct_code("SPL");
}

enum class DiehlFrame { NONE, OMS, PRIOS, SAP_PRIOS, OTHER };

DiehlFrame diehl_frame(const uint8_t *f, size_t len) {
  if (len < 15) return DiehlFrame::NONE;
  uint16_t m = (f[2] | (f[3] << 8)) & 0x7FFF;
  if (!is_diehl(m) || (f[1] != 0x44 && f[1] != 0x46)) return DiehlFrame::NONE;
  uint8_t ci = f[10];
  if (ci >= 0xA0 && ci <= 0xA7) return m == mfct_code("SAP") ? DiehlFrame::SAP_PRIOS : DiehlFrame::PRIOS;
  if (ci == 0x7A && !(f[14] & 0x10)) return DiehlFrame::OMS;
  if (ci == 0x71 || ci == 0x7A || (ci >= 0xB1 && ci <= 0xB3)) return DiehlFrame::OTHER;
  return DiehlFrame::NONE;
}

// Dwa klucze domyślne Diehl (8 bajtów, w użyciu jako XOR połówek).
constexpr uint8_t DIEHL_KEY1[8] = {0x39, 0xBC, 0x8A, 0x10, 0xE6, 0x6D, 0x83, 0xF8};
constexpr uint8_t DIEHL_KEY2[8] = {0x51, 0x72, 0x89, 0x10, 0xE6, 0x6D, 0x83, 0xF8};

uint32_t fold_key(const uint8_t *k8) { return be32(k8) ^ be32(k8 + 4); }

// Odszyfrowanie danych PRIOS od bajtu 15. Pierwszy bajt jawnego tekstu to zawsze 0x4B.
bool prios_decode(const uint8_t *f, size_t len, uint32_t key, std::vector<uint8_t> &out) {
  key ^= be32(f + 2) ^ be32(f + 6) ^ be32(f + 10);
  out.resize(len - 15);
  for (size_t i = 0; i < out.size(); i++) {
    for (int b = 0; b < 8; b++) {
      uint32_t bit = ((key >> 1) ^ (key >> 2) ^ (key >> 11) ^ (key >> 31)) & 1;
      key = (key << 1) | bit;
    }
    out[i] = f[15 + i] ^ static_cast<uint8_t>(key);
    if (i == 0 && out[0] != 0x4B) return false;
  }
  return !out.empty();
}

bool detect_izar(const Address &a) {
  uint16_t m = mfct15(a);
  if (m == mfct_code("SAP")) return a.type == 0x15 || a.type == 0x04 || (a.type == 0x07 && a.version == 0x00);
  if (m == mfct_code("HYD")) return a.type == 0x07 && (a.version == 0x85 || a.version == 0x86);
  if (m == mfct_code("DME")) return (a.type == 0x07 || a.type == 0x06) && a.version == 0x78;
  return false;
}

bool decode_izar(const uint8_t *f, size_t len, size_t /*pos*/, const uint8_t *key, Result &res) {
  DiehlFrame kind = diehl_frame(f, len);
  if (kind != DiehlFrame::PRIOS && kind != DiehlFrame::SAP_PRIOS) return false;
  res.media = Media::WATER;

  std::vector<uint8_t> d;
  bool ok = false;
  if (key) ok = prios_decode(f, len, fold_key(key), d);
  if (!ok) ok = prios_decode(f, len, fold_key(DIEHL_KEY1), d);
  if (!ok) ok = prios_decode(f, len, fold_key(DIEHL_KEY2), d);
  if (!ok || d.size() < 5) {
    res.status = key ? Status::BAD_KEY : Status::NO_KEY;
    return true;
  }

  add_field(res, num("volume_m3", "Zużycie wody", "m³", le32(&d[1]) / 1000.0));
  if (d.size() > 8) add_field(res, num("volume_s1_m3", "Stan na koniec okresu", "m³", le32(&d[5]) / 1000.0));
  if (d.size() > 10) {
    int year = ((d[10] & 0xF0) >> 1) + ((d[9] & 0xE0) >> 5);
    year += year > 80 ? 1900 : 2000;
    char b[16];
    snprintf(b, sizeof b, "%04d-%02d-%02d", year, d[10] & 0x0F, d[9] & 0x1F);
    add_field(res, txt("date_s1", "Data stanu okresowego", b));
  }

  add_field(res, num("battery_days", "Pozostały czas baterii", "d", (f[12] & 0x1F) / 2.0 * 365));
  add_field(res, num("transmit_period_s", "Okres nadawania", "s", 1u << ((f[11] & 0x0F) + 2)));

  // Alarmy: bieżące (bajty 11–13) i wcześniejsze.
  std::string now, before;
  if (f[12] & 0x80) join(now, "wyciek");
  if (f[12] & 0x20) join(now, "licznik zablokowany");
  if (f[13] & 0x80) join(now, "przepływ wsteczny");
  if (f[13] & 0x40) join(now, "przepływ za mały");
  if (f[13] & 0x20) join(now, "przepływ za duży");
  if (f[13] & 0x10) join(now, "zalanie");
  if (f[13] & 0x08) join(now, "sabotaż czujnika");
  if (f[13] & 0x02) join(now, "sabotaż mechaniczny");
  if (!now.empty() && (f[11] & 0x80)) now = "alarm ogólny";
  if (f[12] & 0x40) join(before, "wyciek");
  if (f[13] & 0x04) join(before, "sabotaż czujnika");
  if (f[13] & 0x01) join(before, "sabotaż mechaniczny");
  add_field(res, txt("alarms", "Alarmy", now.empty() ? "brak" : now));
  add_field(res, txt("alarms_previous", "Alarmy wcześniejsze", before.empty() ? "brak" : before));

  // SAP PRIOS: numer z obudowy (np. C21FA000711) zakodowany w adresie.
  if (kind == DiehlFrame::SAP_PRIOS) {
    uint32_t digits = ((f[7] & 0x03) << 24) | (f[6] << 16) | (f[5] << 8) | f[4];
    unsigned yy = (digits / 1000000) % 100, serial = digits % 1000000;
    char supplier = static_cast<char>('@' + (((f[9] & 0x0F) << 1) | (f[8] >> 7)));
    char type = static_cast<char>('@' + ((f[8] & 0x7C) >> 2));
    char diameter = static_cast<char>('@' + (((f[8] & 0x03) << 3) | (f[7] >> 5)));
    char b[24];
    snprintf(b, sizeof b, "%c%02u%c%c%06u", supplier, yy, type, diameter, serial);
    add_field(res, txt("fabrication_no", "Numer fabryczny", b));
    snprintf(b, sizeof b, "%u", yy > 70 ? 1900 + yy : 2000 + yy);
    add_field(res, txt("manufacture_year", "Rok produkcji", b));
  }
  add_field(res, txt("status", "Status licznika", now.empty() ? "OK" : now));
  return true;
}

const Driver IZAR = {"izar", "Diehl Izar (woda)", detect_izar, decode_izar, nullptr, QUIRK_NONE};

// ===================== Apator 162 =====================
// OMS z AES (zwykle klucz zerowy), ale stan licznika w danych producenta po 0x0F:
// 7 bajtów nagłówka, potem rejestry „kod + dane” o długości zależnej od kodu.

int apator_reg_size(uint8_t c) {
  if (c >= 0x71 && c <= 0x7B) return 1 + 4 * (c - 0x6F);  // dane archiwalne
  if ((c >= 0xB6 && c <= 0xC7) || c == 0xD0 || c == 0xD3) return 3;
  switch (c) {
    case 0x00: case 0x10: case 0x42: case 0xA0: case 0xA1: case 0xA4: case 0xF0: return 4;
    case 0x01: case 0x44: case 0xA6: return 3;
    case 0x11: case 0x41: case 0x43: case 0xA7: case 0xA8: case 0xAA: case 0xAB: case 0xAC: case 0xAD: case 0xB4:
      return 2;
    case 0x40: case 0x8B: case 0x8C: return 6;
    case 0xA2: case 0xA5: case 0xA9: case 0xAF: return 1;
    case 0xA3: case 0x8E: return 7;
    case 0xB0: return 5;
    case 0xB1: case 0xB3: return 8;
    case 0xB2: case 0xB5: return 16;
    case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x86: case 0x87: return 10;
    case 0x85: case 0x88: case 0x8F: return 11;
    case 0x8A: return 9;
    default: return -1;
  }
}

bool detect_apator162(const Address &a) {
  return mfct15(a) == mfct_code("APA") && a.version == 0x05 && (a.type == 0x06 || a.type == 0x07);
}

void post_apator162(const Parsed &p, Result &res) {
  const auto &m = p.mfct_data;
  for (size_t i = 7; i < m.size();) {
    uint8_t c = m[i++];
    if (c == 0xFF) break;  // wypełnienie do granicy bloku AES
    int n = apator_reg_size(c);
    if (n < 0 || i + n > m.size()) break;
    if (c == 0x10) add_field(res, num("volume_m3", "Zużycie wody", "m³", le32(&m[i]) / 1000.0));
    i += n;
  }
}

const Driver APATOR162 = {"apator162", "Apator 162 (woda)", detect_apator162, nullptr, post_apator162, QUIRK_NONE};

// ===================== Hydrodigit (B METERS) =====================
// OMS + blok producenta po 0x0F: bajt flag, a za nim tylko te bloki, których bity są ustawione,
// w kolejności: bateria (1), sabotaż (3), wyciek (3), przepływ wsteczny (4), 12 stanów miesięcznych (po 3).

bool detect_hydrodigit(const Address &a) {
  return mfct15(a) == mfct_code("BMT") && (a.type == 0x06 || a.type == 0x07) &&
         (a.version == 0x13 || a.version == 0x15 || a.version == 0x17);
}

double hydrodigit_voltage(uint8_t b) {
  static const double V[16] = {3.7, 1.9, 2.1, 2.2, 2.3, 2.4, 2.5, 2.65, 2.8, 2.9, 3.05, 3.2, 3.35, 3.5, 3.7, 3.7};
  return V[b & 0x0F];
}

void post_hydrodigit(const Parsed &p, Result &res) {
  const auto &m = p.mfct_data;
  if (m.empty()) return;
  uint8_t flags = m[0];
  size_t i = 1;
  if (flags & 0x01) {
    if (i >= m.size()) return;
    add_field(res, num("battery_voltage_v", "Napięcie baterii", "V", hydrodigit_voltage(m[i])));
    i += 1;
  }
  if (flags & 0x02) {  // dane sabotażu: znaczenie niepotwierdzone, pomijamy
    if (i + 3 > m.size()) return;
    i += 3;
  }
  if (flags & 0x80) {
    if (i + 3 > m.size()) return;
    char b[16];
    snprintf(b, sizeof b, "20%02X-%02X-%02X", m[i], m[i + 1], m[i + 2]);
    add_field(res, txt("leak_date", "Data wycieku", m[i] | m[i + 1] | m[i + 2] ? b : "brak"));
    i += 3;
  }
  if (flags & 0x04) {
    if (i + 4 > m.size()) return;
    add_field(res, num("volume_backward_m3", "Przepływ wsteczny", "m³", le32(&m[i]) / 1000.0));
    i += 4;
  }
  if (flags & 0x10) {
    static const char *const MONTH[12] = {"stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca",
                                          "lipca", "sierpnia", "września", "października", "listopada", "grudnia"};
    for (int k = 0; k < 12 && i + 3 <= m.size(); k++, i += 3) {
      char key[24], label[48];
      snprintf(key, sizeof key, "volume_month_%02d_m3", k + 1);
      snprintf(label, sizeof label, "Stan na koniec %s", MONTH[k]);
      add_field(res, num(key, label, "m³", (m[i] | (m[i + 1] << 8) | (m[i + 2] << 16)) / 100.0));
    }
  }
}

const Driver HYDRODIGIT = {"hydrodigit", "B METERS Hydrodigit (woda)", detect_hydrodigit, nullptr, post_hydrodigit,
                           QUIRK_NONE};

const Driver *const DRIVERS[] = {&SANXING, &IZAR, &APATOR162, &HYDRODIGIT};

}  // namespace

void normalize_address(const uint8_t *f, size_t len, Address &a) {
  switch (diehl_frame(f, len)) {
    case DiehlFrame::PRIOS:
    case DiehlFrame::OTHER:  // Diehl zamienia kolejność: wersja i typ przed numerem
      a.id = le32(f + 6);
      a.version = f[4];
      a.type = f[5];
      break;
    case DiehlFrame::SAP_PRIOS:  // bajt wersji jest częścią numeru; to wodomierz
      a.version = 0x00;
      a.type = 0x07;
      break;
    default:
      break;
  }
}

bool default_key(const uint8_t *f, size_t len, uint8_t key[16]) {
  if (diehl_frame(f, len) != DiehlFrame::OMS) return false;
  memcpy(key, DIEHL_KEY2, 8);
  memcpy(key + 8, DIEHL_KEY2, 8);
  return true;
}

const Driver *find_driver(const char *name, const Address &a) {
  if (name != nullptr && strcmp(name, "oms") == 0) return nullptr;  // wymuszony dekoder ogólny
  bool autod = name == nullptr || *name == 0 || strcmp(name, "auto") == 0;
  for (const Driver *d : DRIVERS) {
    if (autod ? d->detect(a) : strcmp(name, d->name) == 0) return d;
  }
  return nullptr;
}

const Driver *const *all_drivers(size_t &count) {
  count = sizeof(DRIVERS) / sizeof(DRIVERS[0]);
  return DRIVERS;
}

}  // namespace sihos::wmb
