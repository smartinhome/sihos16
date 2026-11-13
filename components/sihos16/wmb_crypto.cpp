// sihos16 — AES-128: wspólne CMAC/CTR na bazie jednego szyfrowania bloku.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "wmb_crypto.h"

#include <cstring>

#ifdef SIHOS_HOST
#include <openssl/evp.h>
#else
#include "mbedtls/aes.h"
#endif

namespace sihos::wmb {

#ifdef SIHOS_HOST
static bool evp(const EVP_CIPHER *c, bool enc, const uint8_t *key, const uint8_t *iv, const uint8_t *in,
                uint8_t *out, size_t len) {
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return false;
  int n = 0;
  bool ok = EVP_CipherInit_ex(ctx, c, nullptr, key, iv, enc ? 1 : 0) == 1 &&
            EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
            EVP_CipherUpdate(ctx, out, &n, in, static_cast<int>(len)) == 1;
  EVP_CIPHER_CTX_free(ctx);
  return ok;
}

bool aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t len) {
  if (len % 16) return false;
  return evp(EVP_aes_128_cbc(), false, key, iv, data, data, len);
}

bool aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  return evp(EVP_aes_128_ecb(), true, key, nullptr, in, out, 16);
}
#else
bool aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t len) {
  if (len % 16) return false;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  uint8_t ivc[16];
  memcpy(ivc, iv, 16);
  bool ok = mbedtls_aes_setkey_dec(&ctx, key, 128) == 0 &&
            mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, len, ivc, data, data) == 0;
  mbedtls_aes_free(&ctx);
  return ok;
}

bool aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  bool ok = mbedtls_aes_setkey_enc(&ctx, key, 128) == 0 &&
            mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in, out) == 0;
  mbedtls_aes_free(&ctx);
  return ok;
}
#endif

// Podklucze CMAC: przesunięcie o bit w lewo z warunkowym XOR 0x87.
static void cmac_shift(const uint8_t in[16], uint8_t out[16]) {
  uint8_t carry = 0;
  for (int i = 15; i >= 0; i--) {
    uint8_t b = in[i];
    out[i] = static_cast<uint8_t>((b << 1) | carry);
    carry = b >> 7;
  }
  if (in[0] & 0x80) out[15] ^= 0x87;
}

bool aes128_cmac(const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t mac[16]) {
  uint8_t zero[16] = {0}, l[16], k1[16], k2[16];
  if (!aes128_ecb_encrypt(key, zero, l)) return false;
  cmac_shift(l, k1);
  cmac_shift(k1, k2);

  size_t blocks = len == 0 ? 1 : (len + 15) / 16;
  bool complete = len != 0 && len % 16 == 0;
  uint8_t x[16] = {0}, y[16];
  for (size_t b = 0; b < blocks; b++) {
    for (int i = 0; i < 16; i++) {
      size_t p = b * 16 + i;
      uint8_t m;
      if (b + 1 < blocks || complete) {
        m = msg[p];
      } else {
        m = p < len ? msg[p] : (p == len ? 0x80 : 0x00);
      }
      if (b + 1 == blocks) m ^= complete ? k1[i] : k2[i];
      y[i] = x[i] ^ m;
    }
    if (!aes128_ecb_encrypt(key, y, x)) return false;
  }
  memcpy(mac, x, 16);
  return true;
}

bool aes128_ctr(const uint8_t key[16], uint8_t ctr[16], uint8_t *data, size_t len) {
  uint8_t ks[16];
  for (size_t off = 0; off < len; off += 16) {
    if (!aes128_ecb_encrypt(key, ctr, ks)) return false;
    size_t n = len - off < 16 ? len - off : 16;
    for (size_t i = 0; i < n; i++) data[off + i] ^= ks[i];
    for (int i = 15; i >= 0; i--)
      if (++ctr[i] != 0) break;
  }
  return true;
}

}  // namespace sihos::wmb
