// sihos16 — dekoder telegramu wM-Bus: nagłówek, ELL/AFL/TPL, odszyfrowanie, rekordy, pola.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wmb_types.h"

namespace sihos::wmb {

// Sam nagłówek DLL (adres licznika) — do listy „usłyszane w okolicy” bez dekodowania.
bool peek_address(const uint8_t *frame, size_t len, Address &out);

// Dekoduje ramkę bez bajtów CRC (L-pole jako pierwszy bajt).
//  key    — 16 bajtów albo nullptr, gdy licznik nie szyfruje
//  driver — nazwa sterownika albo "auto" (wybór po producencie/wersji/typie)
Result decode(const uint8_t *frame, size_t len, const uint8_t *key, const char *driver = "auto");

// Klucz z tekstu: 32 znaki hex albo 16 znaków ASCII (np. klucz z formularza operatora).
bool parse_key(const char *text, uint8_t out[16]);

}  // namespace sihos::wmb
