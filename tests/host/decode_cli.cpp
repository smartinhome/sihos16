// Dekodowanie ramki na komputerze: decode_cli <hex> [klucz|NOKEY] [sterownik]
// Wynik jako JSON na stdout.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "wmb_decoder.h"

using namespace sihos::wmb;

static std::string esc(const std::string &s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    if (static_cast<unsigned char>(c) < 0x20) {
      char b[8];
      snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
      continue;
    }
    o += c;
  }
  return o;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "uzycie: %s <hex> [klucz|NOKEY] [sterownik]\n", argv[0]);
    return 2;
  }
  std::vector<uint8_t> frame;
  std::string hex;
  for (const char *p = argv[1]; *p; p++)
    if (isxdigit(static_cast<unsigned char>(*p))) hex += *p;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) frame.push_back(std::stoi(hex.substr(i, 2), nullptr, 16));

  uint8_t key[16];
  const uint8_t *kp = nullptr;
  if (argc > 2 && strcmp(argv[2], "NOKEY") != 0) {
    if (!parse_key(argv[2], key)) {
      fprintf(stderr, "zly klucz\n");
      return 2;
    }
    kp = key;
  }
  const char *drv = argc > 3 ? argv[3] : "auto";
  Result r = decode(frame.data(), frame.size(), kp, drv);

  printf("{\"status\":\"%s\",\"mfct\":\"%s\",\"id\":\"%08x\",\"version\":%u,\"type\":%u,\"media\":\"%s\",\"driver\":\"%s\",\"fields\":{",
         status_str(r.status), esc(mfct_str(r.addr.mfct)).c_str(), r.addr.id, r.addr.version, r.addr.type,
         media_str(r.media), r.driver);
  for (size_t i = 0; i < r.fields.size(); i++) {
    const Field &f = r.fields[i];
    printf("%s\"%s\":{\"label\":\"%s\",\"unit\":\"%s\",", i ? "," : "", f.key.c_str(), esc(f.label).c_str(), f.unit);
    if (f.is_text) printf("\"text\":\"%s\"}", esc(f.text).c_str());
    else if (std::isfinite(f.value)) printf("\"value\":%.10g}", f.value);
    else printf("\"value\":null}");
  }
  printf("}}\n");
  return r.status == Status::OK ? 0 : 1;
}
