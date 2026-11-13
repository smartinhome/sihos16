// sihos16 — warstwa łącza wM-Bus (EN 13757-4).
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "wmb_link.h"

#include <cstring>

namespace sihos::wmb {

size_t format_a_size(uint8_t l) {
  // Blok 1: L + 9 bajtów + CRC; dalej bloki po 16 bajtów + CRC; ostatni krótszy.
  size_t blocks = l < 10 ? 1 : 1 + (l - 9 + 15) / 16;
  return 1u + l + 2 * blocks;
}

size_t encoded_3of6_size(size_t decoded) { return (3 * decoded + 1) / 2; }

size_t decode_3of6(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) {
  // Symbol 6-bitowy -> półbajt; 0xFF = symbol niedozwolony.
  static const uint8_t SYM[64] = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3,  0xFF, 0x1,  0x2,  0xFF,
      0xFF, 0xFF, 0xFF, 0x7,  0xFF, 0xFF, 0x0,  0xFF, 0xFF, 0x5,  0x6,  0xFF, 0x4,  0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xB,  0xFF, 0x9,  0xA,  0xFF, 0xFF, 0xF,  0xFF, 0xFF, 0x8,  0xFF, 0xFF, 0xFF,
      0xFF, 0xD,  0xE,  0xFF, 0xC,  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  };
  size_t symbols = in_len * 8 / 6;
  size_t n = symbols / 2;
  if (n > out_max) n = out_max;
  uint32_t acc = 0;
  int bits = 0;
  size_t in_pos = 0;
  for (size_t i = 0; i < n * 2; i++) {
    while (bits < 6) {
      acc = (acc << 8) | in[in_pos++];
      bits += 8;
    }
    uint8_t sym = (acc >> (bits - 6)) & 0x3F;
    bits -= 6;
    uint8_t nib = SYM[sym];
    if (nib == 0xFF) return 0;
    if (i & 1) out[i / 2] |= nib;
    else out[i / 2] = static_cast<uint8_t>(nib << 4);
  }
  return n;
}

uint16_t crc16_en13757(const uint8_t *p, size_t n) {
  uint16_t crc = 0;
  for (size_t i = 0; i < n; i++) {
    crc ^= static_cast<uint16_t>(p[i]) << 8;
    for (int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (crc << 1) ^ 0x3D65 : crc << 1;
  }
  return ~crc;
}

static bool crc_ok(const uint8_t *data, size_t n) {
  uint16_t c = crc16_en13757(data, n);
  return data[n] == (c >> 8) && data[n + 1] == (c & 0xFF);  // CRC wysyłane starszym bajtem
}

bool strip_crc(uint8_t *buf, size_t &len, FrameFormat fmt) {
  if (len < 12) return false;
  uint8_t l = buf[0];
  if (fmt == FrameFormat::A) {
    size_t need = format_a_size(l);
    if (len < need) return false;
    size_t in = 0, out = 0;
    size_t remaining = 1u + l;  // bajty danych łącznie z L
    size_t block = 10;
    while (remaining > 0) {
      size_t n = remaining < block ? remaining : block;
      if (!crc_ok(buf + in, n)) return false;
      memmove(buf + out, buf + in, n);
      in += n + 2;
      out += n;
      remaining -= n;
      block = 16;
    }
    len = out;
    return true;
  }
  // Format B: bloki 1+2 (do 126 bajtów danych) z jednym CRC, opcjonalny blok 3 z własnym CRC.
  size_t total = 1u + l;  // wszystko łącznie z CRC
  if (len < total || total < 12) return false;
  size_t out;
  if (total <= 128) {
    size_t n = total - 2;
    if (!crc_ok(buf, n)) return false;
    out = n;
  } else {
    if (!crc_ok(buf, 126)) return false;
    size_t n3 = total - 128 - 2;
    if (!crc_ok(buf + 128, n3)) return false;
    memmove(buf + 126, buf + 128, n3);
    out = 126 + n3;
  }
  buf[0] = static_cast<uint8_t>(out - 1);  // L jak w formacie A: bez CRC
  len = out;
  return true;
}

}  // namespace sihos::wmb
