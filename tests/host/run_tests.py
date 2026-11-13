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


if __name__ == "__main__":
    ok = real_frames()
    wmbusmeters_vectors("-v" in sys.argv)
    sys.exit(0 if ok else 1)
