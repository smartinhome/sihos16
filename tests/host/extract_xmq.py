#!/usr/bin/env python3
"""Wyciąga testy (telegram + oczekiwany JSON) z plików *.xmq wmbusmeters do vectors.json.

uzycie: extract_xmq.py <katalog drivers/src wmbusmeters> [plik_wyjsciowy]
Dane testowe pochodzą z wmbusmeters (GPL-3.0) i służą tylko do sprawdzania dekodera.
"""
import json
import pathlib
import re
import sys

src = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2] if len(sys.argv) > 2 else "vectors.json")

test_re = re.compile(r"test\s*\{(.*?)\n\s*\}", re.S)
val_re = re.compile(r"^\s*(\w+)\s*=\s*(.*)$", re.M)

vectors = []
for f in sorted(src.glob("*.xmq")):
    text = f.read_text()
    for m in test_re.finditer(text):
        body = m.group(1)
        kv = {}
        for k, v in val_re.findall(body):
            v = v.strip()
            if v.startswith("'") and v.endswith("'"):
                v = v[1:-1]
            kv[k] = v
        if "telegram" not in kv or "args" not in kv or "json" not in kv:
            continue
        args = kv["args"].split()
        if len(args) < 4:
            continue
        try:
            expected = json.loads(kv["json"])
        except json.JSONDecodeError:
            continue
        vectors.append({
            "file": f.stem,
            "driver": args[1],
            "id": args[2],
            "key": args[3],
            "telegram": re.sub(r"[^0-9a-fA-F]", "", kv["telegram"]),
            "json": expected,
        })

out.write_text(json.dumps(vectors, indent=0, ensure_ascii=False))
print(f"{len(vectors)} testów z {src}")
