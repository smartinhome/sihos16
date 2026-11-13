// sihos16 — sterowniki tylko dla liczników odbiegających od OMS.
// Zwykłe liczniki OMS nie potrzebują sterownika: dekoder ogólny nazywa pola sam.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wmb_records.h"
#include "wmb_types.h"

namespace sihos::wmb {

enum Quirk : uint8_t {
  QUIRK_NONE = 0,
  QUIRK_SANXING_609B = 1 << 0,  // po odszyfrowaniu 0x609B zamiast 2F2F, klucz sprawdzany po końcowym 2F2F
};

struct Driver {
  const char *name;
  const char *label;
  // Czy sterownik pasuje do adresu (tryb auto).
  bool (*detect)(const Address &a);
  // Własny format producenta (CI 0xA0–0xB7 lub cała ramka). nullptr = dekoder ogólny.
  // frame/len — cała ramka bez CRC; pos — początek danych za CI.
  bool (*decode_proprietary)(const uint8_t *frame, size_t len, size_t pos, const uint8_t *key, Result &res);
  // Uzupełnienie po dekoderze ogólnym (np. dane producenta po 0x0F). Może być nullptr.
  void (*post)(const Parsed &p, Result &res);
  uint8_t quirks;
};

// Sterownik o nazwie albo wykryty po adresie; nullptr = ogólny OMS.
const Driver *find_driver(const char *name, const Address &a);

// Poprawia adres z ramek, które kodują go niestandardowo (Diehl/Izar).
void normalize_address(const uint8_t *frame, size_t len, Address &a);

// Klucz publicznie znany dla danego producenta (Diehl OMS), gdy użytkownik nie podał swojego.
bool default_key(const uint8_t *frame, size_t len, uint8_t key[16]);

// Lista wszystkich (do strony konfiguracji).
const Driver *const *all_drivers(size_t &count);

}  // namespace sihos::wmb
