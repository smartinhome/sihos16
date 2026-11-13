// sihos16 — warstwa łącza wM-Bus (EN 13757-4): kodowanie 3-of-6 (tryb T), bloki z CRC (format A/B).
// Bez zależności od ESPHome — testowana na komputerze.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sihos::wmb {

constexpr size_t MAX_FRAME = 256;  // ramka bez CRC: L + 255 bajtów

enum class LinkMode : uint8_t { T, C };
enum class FrameFormat : uint8_t { A, B };

// Liczba bajtów ramki formatu A z CRC dla danego L (L nie liczy CRC).
size_t format_a_size(uint8_t l_field);

// Liczba bajtów zakodowanych 3-of-6 dla `decoded` bajtów.
size_t encoded_3of6_size(size_t decoded);

// Dekoduje 3-of-6. Zwraca liczbę bajtów albo 0 przy niedozwolonym symbolu.
size_t decode_3of6(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max);

uint16_t crc16_en13757(const uint8_t *p, size_t n);

// Sprawdza CRC i usuwa je w miejscu. Po sukcesie buf[0..len) to ramka z L równym długości bez CRC.
// Format B: L w ramce liczy też CRC — zostaje poprawione.
// Zwraca false przy złym CRC lub niespójnej długości.
bool strip_crc(uint8_t *buf, size_t &len, FrameFormat fmt);

}  // namespace sihos::wmb
