// Test warstwy łącza: czyta linie "crcA|crcB|3of6 <hex>" ze stdin, wypisuje wynik hex albo FAIL.
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "wmb_link.h"

using namespace sihos::wmb;

int main() {
  std::string op, hex;
  while (std::cin >> op >> hex) {
    std::vector<uint8_t> b;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) b.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));
    std::vector<uint8_t> out(512);
    size_t n = 0;
    bool ok;
    if (op == "3of6") {
      n = decode_3of6(b.data(), b.size(), out.data(), out.size());
      ok = n > 0;
    } else {
      out = b;
      n = b.size();
      ok = strip_crc(out.data(), n, op == "crcA" ? FrameFormat::A : FrameFormat::B);
    }
    if (!ok) {
      puts("FAIL");
      continue;
    }
    for (size_t i = 0; i < n; i++) printf("%02x", out[i]);
    puts("");
  }
}
