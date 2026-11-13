# sihos16

Oprogramowanie ESPHome dla **SIH Reader 1.6** (LOLIN S3 mini + CC1101, 868 MHz) od [smartinhome.pl](https://smartinhome.pl).

Liczniki dodaje się na stronie modułu, bez kompilacji. Każdy licznik pojawia się w Home Assistant jako osobne urządzenie przez natywne API ESPHome.

## Stan prac

| Krok | Zakres | Stan |
|---|---|---|
| 1 | Własny dekoder wM-Bus + testy na komputerze | ✅ |
| 2 | Sterowniki formatów producenta: Izar, Apator 162, Hydrodigit | ⏳ |
| 3 | Radio CC1101 | ⏳ |
| 4 | Komponent ESPHome: liczniki jako urządzenia w HA | ⏳ |
| 5 | Strona konfiguracji w module | ⏳ |
| 6 | Diody, minimalny YAML, kompilacja | ⏳ |

## Dekoder

`components/sihos16/wmb_*` — własna implementacja EN 13757-3/-4 i OMS, bez kodu wmbusmeters.

- **Jeden ogólny dekoder zamiast tabeli na każdy licznik.** Rekordy DIF/VIF są zamieniane na pola na podstawie znaczenia (wielkość, kierunek, taryfa, faza, zapis archiwalny). Pole dostaje stały klucz (`energy_backward_t1_kwh`) i polską nazwę zależną od nośnika („Energia oddana T1” dla prądu, „Przepływ wsteczny” dla wody).
- **Sterowniki tylko tam, gdzie licznik odbiega od OMS** (format producenta, wady jak `609B` w Sanxingu).
- **Szyfrowanie:** AES-CBC tryb 5, tryb 7 (klucz z AES-CMAC i licznika z AFL), ELL AES-CTR (Kamstrup). Na ESP32 przez mbedTLS ze sprzętowym AES.
- **Rozmiar:** ok. 27 kB kodu.

## Testy na komputerze

```bash
cd tests/host
make test        # wymaga g++ i libssl-dev
```

- `real_frames.json` — ramki z prawdziwych instalacji, sprawdzane pole po polu.
- `vectors.json` — telegramy testowe z [wmbusmeters](https://github.com/wmbusmeters/wmbusmeters) (GPL-3.0), wyciągnięte skryptem `extract_xmq.py`. Porównywane są wartości, nie nazwy pól.

## Licencja

GPL-3.0-or-later (patrz `LICENSE`). Telegramy testowe pochodzą z projektu wmbusmeters © Fredrik Öhrström i współautorzy.
