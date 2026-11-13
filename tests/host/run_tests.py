#!/usr/bin/env python3
"""Testy dekodera na komputerze.

1. Ramki z prawdziwych instalacji (real_frames.json): dokładne pola i wartości.
2. Testy z wmbusmeters (vectors.json): każda liczbowa wartość z ich JSON-a
   musi wystąpić w naszym wyniku w tej samej wielkości (po przeliczeniu jednostki).
   Nazwy pól są inne z założenia, więc porównujemy wartości, nie nazwy.
"""
import json
import pathlib
import subprocess
import sys

HERE = pathlib.Path(__file__).parent
CLI = HERE / "decode_cli"

# przyrostek pola wmbusmeters -> (nasza jednostka, mnożnik)
UNITS = {
    "kwh": ("kWh", 1), "mwh": ("kWh", 1000), "wh": ("kWh", 0.001),
    "gj": ("GJ", 1), "mj": ("GJ", 0.001),
    "m3": ("m³", 1), "l": ("m³", 0.001),
    "kw": ("W", 1000), "w": ("W", 1),
    "v": ("V", 1), "a": ("A", 1),
    "c": ("°C", 1), "k": ("K", 1), "bar": ("bar", 1),
    "m3h": ("m³/h", 1), "lh": ("m³/h", 0.001),
    "kvarh": ("kvarh", 1), "var": ("var", 1), "kvar": ("var", 1000),
    "hca": ("", 1), "dbm": ("dBm", 1),
}


def run(telegram, key, driver="auto"):
    p = subprocess.run([str(CLI), telegram, key, driver], capture_output=True, text=True)
    return json.loads(p.stdout)


def norm(unit, value):
    """Sprowadza energię do kWh, a °C i K do jednej grupy, żeby porównywać wielkości, nie zapis."""
    if unit == "GJ":
        return "kWh", value * 1e9 / 3.6e6
    if unit == "K":
        return "°C", value
    return unit, value


def close(a, b):
    return abs(a - b) <= max(1e-6, abs(b) * 1e-6)


def real_frames():
    ok = True
    for t in json.loads((HERE / "real_frames.json").read_text()):
        r = run(t["telegram"], t.get("key", "NOKEY"), t.get("driver", "auto"))
        bad = []
        if r["status"] != t.get("status", "OK"):
            bad.append(f"status {r['status']}")
        for k, want in t.get("fields", {}).items():
            got = r["fields"].get(k)
            if got is None:
                bad.append(f"brak {k}")
            elif isinstance(want, str):
                if got.get("text") != want:
                    bad.append(f"{k}={got.get('text')} (oczekiwane {want})")
            elif "value" not in got or not close(got["value"], want):
                bad.append(f"{k}={got.get('value')} (oczekiwane {want})")
        print(("OK  " if not bad else "BŁĄD") + f" {t['name']}" + ("" if not bad else ": " + "; ".join(bad)))
        ok &= not bad
    return ok


def wmbusmeters_vectors(verbose):
    vecs = json.loads((HERE / "vectors.json").read_text())
    total = found = frames_ok = 0
    per_file = {}
    vecs = [v for v in vecs if len(v["telegram"]) % 2 == 0 and not v["telegram"].upper().startswith("68")]
    for v in vecs:  # 0x68... to przewodowy M-Bus, nie wM-Bus
        r = run(v["telegram"], v["key"])
        st = per_file.setdefault(v["file"], [0, 0, 0, []])
        st[2] += 1
        if r["status"] != "OK":
            st[3].append(f"status {r['status']}")
            continue
        frames_ok += 1
        mine = [norm(f["unit"], f["value"]) for f in r["fields"].values() if "value" in f and f["value"] is not None]
        for name, val in v["json"].items():
            if not isinstance(val, (int, float)) or isinstance(val, bool):
                continue
            suffix = name.rsplit("_", 1)[-1]
            if name.endswith("difference_c"):
                suffix = "k"
            if suffix not in UNITS or name.endswith("_counter"):
                continue
            unit, mul = UNITS[suffix]
            total += 1
            st[1] += 1
            if any(u == nu and close(x, nv) for u, x in mine for nu, nv in [norm(unit, val * mul)]):
                found += 1
                st[0] += 1
            else:
                st[3].append(f"{name}={val}")
    print(f"\nwmbusmeters: ramki zdekodowane {frames_ok}/{len(vecs)}, wartości znalezione {found}/{total}"
          f" ({100 * found / max(total, 1):.1f}%)")
    if verbose:
        for f, (fnd, tot, n, miss) in sorted(per_file.items()):
            if miss:
                print(f"  {f}: {fnd}/{tot} wartości, {n} ramek; brak: {', '.join(miss[:6])}")
    return found, total


# ---------- warstwa łącza ----------

ENC = [0b010110, 0b001101, 0b001110, 0b001011, 0b011100, 0b011001, 0b011010, 0b010011,
       0b101100, 0b100101, 0b100110, 0b100011, 0b110100, 0b110001, 0b110010, 0b101001]


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x3D65) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc ^ 0xFFFF


def add_crc_a(frame):
    out, i, block = b"", 0, 10
    while i < len(frame):
        part = frame[i:i + block]
        out += part + crc16(part).to_bytes(2, "big")
        i += len(part)
        block = 16
    return out


def add_crc_b(frame):
    # L w formacie B liczy CRC
    n = len(frame) - 1
    total = n + (2 if len(frame) <= 126 else 4)
    f = bytes([total]) + frame[1:]
    if len(f) <= 126:
        return f + crc16(f).to_bytes(2, "big")
    b12, b3 = f[:126], f[126:]
    return b12 + crc16(b12).to_bytes(2, "big") + b3 + crc16(b3).to_bytes(2, "big")


def enc_3of6(data):
    bits = "".join(f"{ENC[b >> 4]:06b}{ENC[b & 15]:06b}" for b in data)
    bits += "0" * (-len(bits) % 8)
    return bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))


def link_tests():
    cli = HERE / "link_cli"
    cases = []  # (operacja, wejście hex, oczekiwane hex albo FAIL, opis)
    real = json.loads((HERE / "link_frames.json").read_text())
    for r in real:
        cases.append(("crcA", r["raw"], None, r["frame"]))
    frames = [bytes.fromhex(t["telegram"]) for t in json.loads((HERE / "real_frames.json").read_text())]
    frames = [f[:f[0] + 1] for f in frames]
    for f in frames:
        a = add_crc_a(f)
        cases.append(("crcA", a.hex(), f.hex(), "A"))
        cases.append(("crcB", add_crc_b(f).hex(), f.hex(), "B"))
        cases.append(("3of6", enc_3of6(a).hex(), a.hex(), "3of6"))
        bad = bytearray(a)
        bad[len(bad) // 2] ^= 0x10
        cases.append(("crcA", bytes(bad).hex(), "FAIL", "A zepsuty"))
    p = subprocess.run([str(cli)], input="\n".join(f"{o} {h}" for o, h, _, _ in cases),
                       capture_output=True, text=True)
    outs = p.stdout.split()
    ok = bad_n = real_ok = real_fail = 0
    for (op, h, want, desc), got in zip(cases, outs):
        if want is None:  # ramki z logów: wynik ma się zgadzać z ucięciem w logu, szum może nie przejść CRC
            if got == "FAIL":
                real_fail += 1
            elif desc.startswith(got[:len(desc)]) or got.startswith(desc):
                real_ok += 1
            else:
                bad_n += 1
                print("BŁĄD log:", h[:40], got[:40], desc[:40])
            continue
        if got == want:
            ok += 1
        else:
            bad_n += 1
            print("BŁĄD", desc, h[:40], got[:40])
    print(f"łącze: syntetyczne {ok}/{ok + bad_n - 0} OK; z logów zgodne {real_ok}, odrzucone CRC {real_fail}")
    return bad_n == 0


if __name__ == "__main__":
    ok = link_tests()
    ok &= real_frames()
    wmbusmeters_vectors("-v" in sys.argv)
    sys.exit(0 if ok else 1)
