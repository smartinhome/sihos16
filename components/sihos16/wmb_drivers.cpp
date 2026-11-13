// sihos16 — sterowniki tylko dla liczników odbiegających od OMS.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "wmb_drivers.h"

#include <cstring>

namespace sihos::wmb {

namespace {

// Sanxing S34U28 (Tauron, PGE): zwykły licznik OMS z wadą sprawdzenia klucza.
bool detect_sanxing(const Address &a) { return (a.mfct & 0x7FFF) == mfct_code("KPL") && a.type == 0x02; }
const Driver SANXING = {"sanxing", "Sanxing S34U28 (prąd)", detect_sanxing, nullptr, nullptr, QUIRK_SANXING_609B};

const Driver *const DRIVERS[] = {&SANXING};

}  // namespace

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
