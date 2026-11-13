// sihos16 — dekoder telegramu wM-Bus: nagłówek, ELL/AFL/TPL, odszyfrowanie, rekordy, pola.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "wmb_decoder.h"

#include <cctype>
#include <cstring>
#include <vector>

#include "wmb_crypto.h"
#include "wmb_drivers.h"
#include "wmb_naming.h"
#include "wmb_records.h"

namespace sihos::wmb {

// ---------- drobiazgi z wmb_types.h ----------

const char *status_str(Status s) {
  switch (s) {
    case Status::OK: return "OK";
    case Status::TOO_SHORT: return "ramka za krótka";
    case Status::NO_KEY: return "brak klucza";
    case Status::BAD_KEY: return "zły klucz";
    case Status::UNSUPPORTED_CI: return "nieobsługiwany format ramki";
    case Status::UNSUPPORTED_SEC: return "nieobsługiwane szyfrowanie";
    case Status::BAD_RECORDS: return "uszkodzone dane";
  }
  return "?";
}

Media media_from_type(uint8_t t) {
  switch (t) {
    case 0x01: return Media::OIL;
    case 0x02: return Media::ELECTRICITY;
    case 0x03: return Media::GAS;
    case 0x04: case 0x0C: return Media::HEAT;
    case 0x05: return Media::STEAM;
    case 0x06: return Media::WARM_WATER;
    case 0x07: return Media::WATER;
    case 0x08: return Media::HCA;
    case 0x09: return Media::COMPRESSED_AIR;
    case 0x0A: case 0x0B: return Media::COOLING;
    case 0x0D: return Media::HEAT_COOLING;
    case 0x0E: return Media::BUS;
    case 0x0F: return Media::UNKNOWN_MEDIA;
    case 0x15: return Media::HOT_WATER;
    case 0x16: return Media::COLD_WATER;
    case 0x17: return Media::DUAL_WATER;
    case 0x18: return Media::PRESSURE;
    case 0x19: return Media::AD_CONVERTER;
    case 0x1A: return Media::SMOKE;
    case 0x1B: return Media::ROOM_SENSOR;
    case 0x1C: return Media::GAS_DETECTOR;
    case 0x20: return Media::BREAKER;
    case 0x21: return Media::VALVE;
    case 0x25: return Media::CUSTOMER_UNIT;
    case 0x28: return Media::WASTE_WATER;
    case 0x29: return Media::GARBAGE;
    case 0x37: return Media::RADIO_CONVERTER;
    default: return Media::OTHER;
  }
}

const char *media_str(Media m) {
  switch (m) {
    case Media::ELECTRICITY: return "prąd";
    case Media::GAS: return "gaz";
    case Media::HEAT: return "ciepło";
    case Media::WARM_WATER: return "woda ciepła";
    case Media::HOT_WATER: return "woda gorąca";
    case Media::WATER: return "woda";
    case Media::COLD_WATER: return "woda zimna";
    case Media::DUAL_WATER: return "woda";
    case Media::HCA: return "podzielnik ciepła";
    case Media::COOLING: return "chłód";
    case Media::HEAT_COOLING: return "ciepło/chłód";
    case Media::SMOKE: return "czujnik dymu";
    case Media::ROOM_SENSOR: return "czujnik pokojowy";
    case Media::WASTE_WATER: return "ścieki";
    case Media::RADIO_CONVERTER: return "nakładka radiowa";
    default: return "inne";
  }
}

std::string mfct_str(uint16_t m) {
  char s[4] = {static_cast<char>(((m >> 10) & 0x1F) + 64), static_cast<char>(((m >> 5) & 0x1F) + 64),
               static_cast<char>((m & 0x1F) + 64), 0};
  return s;
}

uint16_t mfct_code(const char *t) {
  return static_cast<uint16_t>(((t[0] - 64) << 10) | ((t[1] - 64) << 5) | (t[2] - 64));
}

// ---------- pomocnicze ----------

namespace {

uint32_t rd32(const uint8_t *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t crc16_en13757(const uint8_t *p, size_t n) {
  uint16_t crc = 0;
  for (size_t i = 0; i < n; i++) {
    crc ^= static_cast<uint16_t>(p[i]) << 8;
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x3D65 : crc << 1;
  }
  return ~crc;
}

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Adres do IV / wyprowadzenia klucza: M(2) + ID(4) + wersja + typ, w kolejności z ramki.
struct IvAddr {
  uint8_t b[8];
};

}  // namespace

bool parse_key(const char *text, uint8_t out[16]) {
  if (!text) return false;
  size_t n = strlen(text);
  if (n == 32) {
    for (int i = 0; i < 16; i++) {
      int h = hexval(text[2 * i]), l = hexval(text[2 * i + 1]);
      if (h < 0 || l < 0) return false;
      out[i] = static_cast<uint8_t>(h << 4 | l);
    }
    return true;
  }
  if (n == 16) {  // klucz ASCII (PGE podaje 16 znaków)
    memcpy(out, text, 16);
    return true;
  }
  return false;
}

bool peek_address(const uint8_t *f, size_t len, Address &a) {
  if (len < 11) return false;
  a.mfct = f[2] | (f[3] << 8);
  a.id = rd32(f + 4);
  a.version = f[8];
  a.type = f[9];
  normalize_address(f, len, a);
  return true;
}

Result decode(const uint8_t *frame, size_t len, const uint8_t *key, const char *driver_name) {
  Result res;
  if (len < 11 || frame[0] + 1u > len) {
    res.status = Status::TOO_SHORT;
    if (len >= 11) peek_address(frame, len, res.addr);
    return res;
  }
  // Radio oddaje dokładnie L+1 bajtów; dłuższy bufor (np. z testów) dekodujemy w całości.
  std::vector<uint8_t> f(frame, frame + len);  // kopia: odszyfrowujemy w miejscu
  peek_address(f.data(), len, res.addr);
  IvAddr iva;
  memcpy(iva.b, &f[2], 8);

  const Driver *drv = find_driver(driver_name, res.addr);
  if (drv) res.driver = drv->name;
  const bool user_key = key != nullptr;
  uint8_t mfct_key[16];
  if (!key && default_key(f.data(), len, mfct_key)) key = mfct_key;

  size_t pos = 10;
  uint8_t ci = f[pos++];
  uint32_t afl_counter = 0;
  bool have_counter = false;

  // Warstwy ELL / AFL przed właściwym TPL.
  for (int guard = 0; guard < 4; guard++) {
    if (ci == 0x8C) {  // ELL krótki: CC, ACC
      if (pos + 3 > len) return res.status = Status::TOO_SHORT, res;
      res.access_no = f[pos + 1];
      ci = f[pos + 2];
      pos += 3;
    } else if (ci == 0x8D) {  // ELL długi: CC, ACC, SN(4), [CRC(2) + dane] szyfrowane AES-CTR
      if (pos + 8 > len) return res.status = Status::TOO_SHORT, res;
      uint8_t cc = f[pos], acc = f[pos + 1];
      const uint8_t *sn = &f[pos + 2];
      res.access_no = acc;
      uint8_t enc = (sn[3] >> 5) & 0x07;  // pole ENC numeru sesji
      pos += 6;
      bool plain_crc = len - pos >= 2 && crc16_en13757(&f[pos + 2], len - pos - 2) == (f[pos] | (f[pos + 1] << 8));
      if (enc == 1 && !(key == nullptr && plain_crc)) {  // bez klucza, ale CRC się zgadza: już jawne
        if (!key) return res.status = Status::NO_KEY, res;
        uint8_t ctr[16] = {0};
        memcpy(ctr, iva.b, 8);
        ctr[8] = cc;
        memcpy(ctr + 9, sn, 4);
        aes128_ctr(key, ctr, &f[pos], len - pos);
        res.security = 1;
      } else if (enc > 1) {
        return res.status = Status::UNSUPPORTED_SEC, res;
      }
      uint16_t crc = f[pos] | (f[pos + 1] << 8);
      if (crc16_en13757(&f[pos + 2], len - pos - 2) != crc) {
        return res.status = enc ? Status::BAD_KEY : Status::BAD_RECORDS, res;
      }
      pos += 2;
      ci = f[pos++];
    } else if (ci == 0x90) {  // AFL: wyciągamy licznik wiadomości (tryb 7)
      if (pos + 1 > len) return res.status = Status::TOO_SHORT, res;
      size_t afll = f[pos];
      size_t a = pos + 1, end = pos + 1 + afll;
      if (end + 1 > len || afll < 2) return res.status = Status::TOO_SHORT, res;
      uint16_t fcl = f[a] | (f[a + 1] << 8);
      a += 2;
      uint8_t mcl = 0;
      if (fcl & 0x2000) mcl = f[a++];
      if (fcl & 0x0200) a += 2;  // KI
      if ((fcl & 0x0800) && a + 4 <= end) {
        afl_counter = rd32(&f[a]);
        have_counter = true;
      }
      (void) mcl;
      pos = end;
      ci = f[pos++];
    } else {
      break;
    }
  }

  // Format producenta — tylko przez sterownik.
  if (ci >= 0xA0 && ci <= 0xB7) {
    if (drv && drv->decode_proprietary && drv->decode_proprietary(f.data(), len, pos, key, res)) return res;
    return res.status = Status::UNSUPPORTED_CI, res;
  }

  uint16_t cfg = 0;
  if (ci == 0x7A || ci == 0x72) {
    if (ci == 0x72) {  // długi nagłówek: właściwy licznik za nakładką radiową
      if (pos + 8 > len) return res.status = Status::TOO_SHORT, res;
      res.addr.id = rd32(&f[pos]);
      res.addr.mfct = f[pos + 4] | (f[pos + 5] << 8);
      res.addr.version = f[pos + 6];
      res.addr.type = f[pos + 7];
      iva.b[0] = f[pos + 4];
      iva.b[1] = f[pos + 5];
      memcpy(iva.b + 2, &f[pos], 4);
      iva.b[6] = f[pos + 6];
      iva.b[7] = f[pos + 7];
      pos += 8;
      if (!drv || !driver_name || strcmp(driver_name, "auto") == 0) {
        drv = find_driver(driver_name, res.addr);
        res.driver = drv ? drv->name : "oms";
      }
    }
    if (pos + 4 > len) return res.status = Status::TOO_SHORT, res;
    res.access_no = f[pos];
    res.tpl_status = f[pos + 1];
    cfg = f[pos + 2] | (f[pos + 3] << 8);
    pos += 4;
  } else if (ci != 0x78) {
    return res.status = Status::UNSUPPORTED_CI, res;
  }
  res.media = media_from_type(res.addr.type);

  uint8_t mode = (cfg >> 8) & 0x1F;
  res.security = mode ? mode : res.security;
  if (mode == 5 || mode == 7) {
    if (mode == 7) pos += 1;  // CFG-Ext
    size_t blocks = (cfg >> 4) & 0x0F;
    size_t enc_len = blocks * 16;
    if (pos > len) return res.status = Status::TOO_SHORT, res;
    if (enc_len == 0 || pos + enc_len > len) enc_len = ((len - pos) / 16) * 16;
    bool already_plain = !user_key && enc_len >= 2 && f[pos] == 0x2F && f[pos + 1] == 0x2F;
    if (!already_plain) {
      if (!key) return res.status = Status::NO_KEY, res;
      uint8_t iv[16] = {0};
      uint8_t kenc[16];
      const uint8_t *k = key;
      if (mode == 5) {
        memcpy(iv, iva.b, 8);
        memset(iv + 8, res.access_no, 8);
      } else {
        // Tryb 7: klucz sesji = CMAC(K, 00 | licznik | ID | 07..07), IV = 0.
        if (!have_counter) return res.status = Status::UNSUPPORTED_SEC, res;
        uint8_t in[16];
        in[0] = 0x00;
        memcpy(in + 1, &afl_counter, 4);
        memcpy(in + 5, iva.b + 2, 4);
        memset(in + 9, 0x07, 7);
        if (!aes128_cmac(key, in, 16, kenc)) return res.status = Status::UNSUPPORTED_SEC, res;
        k = kenc;
      }
      aes128_cbc_decrypt(k, iv, &f[pos], enc_len);
      bool ok = f[pos] == 0x2F && f[pos + 1] == 0x2F;
      if (!ok && drv && (drv->quirks & QUIRK_SANXING_609B)) {
        ok = f[pos] == 0x60 && f[pos + 1] == 0x9B && f[len - 2] == 0x2F && f[len - 1] == 0x2F;
        if (ok) pos += 2;  // znacznik 609B nie jest rekordem
      }
      if (!ok) return res.status = Status::BAD_KEY, res;
    }
  } else if (mode != 0) {
    return res.status = Status::UNSUPPORTED_SEC, res;
  }

  Parsed parsed = parse_records(&f[pos], len - pos);
  // Urwany ostatni rekord (np. resztki CRC za wypełniaczem) nie przekreśla reszty.
  if (parsed.truncated && parsed.records.empty()) res.status = Status::BAD_RECORDS;
  for (const Record &r : parsed.records) add_field(res, field_from_record(r, res.media));
  if (drv && drv->post) drv->post(parsed, res);

  Field st;
  st.key = "status";
  st.label = "Status licznika";
  st.is_text = true;
  st.text = tpl_status_text(res.tpl_status);
  add_field(res, st);
  return res;
}

}  // namespace sihos::wmb
