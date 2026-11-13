// sihos16 — zamiana rekordów DIF/VIF na pola z polską nazwą i jednostką.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include "wmb_records.h"
#include "wmb_types.h"

namespace sihos::wmb {

// Pole z rekordu: klucz zbudowany ze znaczenia (np. "energy_backward_t1_kwh"),
// etykieta po polsku zależna od nośnika (prąd, woda, ciepło), wartość w jednostce wyjściowej.
Field field_from_record(const Record &r, Media media);

// Dodaje pole, nadając unikalny klucz (_2, _3...) przy powtórzeniu.
void add_field(Result &res, Field f);

// Tekst bajtu statusu TPL: "OK" albo lista flag.
std::string tpl_status_text(uint8_t st);

}  // namespace sihos::wmb
