// sihos16 — AES-128 dla wM-Bus. Na ESP32 mbedTLS (sprzętowe AES), na komputerze OpenSSL.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sihos::wmb {

// Odszyfrowuje `len` bajtów (wielokrotność 16) w miejscu.
bool aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16], uint8_t *data, size_t len);

// Szyfruje pojedynczy blok (do trybu CTR i wyprowadzania kluczy).
bool aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);

// AES-CMAC (RFC 4493) — wyprowadzanie klucza w trybie 7.
bool aes128_cmac(const uint8_t key[16], const uint8_t *msg, size_t len, uint8_t mac[16]);

// AES-128-CTR (odszyfrowanie = szyfrowanie), licznik w `ctr` jest modyfikowany.
bool aes128_ctr(const uint8_t key[16], uint8_t ctr[16], uint8_t *data, size_t len);

}  // namespace sihos::wmb
