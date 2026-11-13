# sihos16

Oprogramowanie ESPHome dla **SIH Reader 1.6** (LOLIN S3 mini + CC1101, 868 MHz) od [smartinhome.pl](https://smartinhome.pl).

Liczniki dodaje się na stronie modułu, bez kompilacji. Każdy licznik pojawia się w Home Assistant jako osobne urządzenie przez natywne API ESPHome.

## Stan prac

| Krok | Zakres | Stan |
|---|---|---|
| 1 | Własny dekoder wM-Bus + testy na komputerze | ✅ |
| 2 | Sterowniki formatów producenta: Izar, Apator 162, Hydrodigit | ✅ |
| 3 | Radio CC1101 | ✅ (do sprawdzenia na module) |
| 4 | Komponent ESPHome: liczniki jako urządzenia w HA | ✅ (do sprawdzenia na module) |
| 5 | Strona konfiguracji w module | ✅ (do sprawdzenia na module) |
| 6 | Diody, WiFi ze strony, ustawienia fabryczne, aktualizacja .bin | ✅ (do sprawdzenia na module) |

## Dekoder

`components/sihos16/wmb_*` — własna implementacja EN 13757-3/-4 i OMS, bez kodu wmbusmeters.

- **Jeden ogólny dekoder zamiast tabeli na każdy licznik.** Rekordy DIF/VIF są zamieniane na pola na podstawie znaczenia (wielkość, kierunek, taryfa, faza, zapis archiwalny). Pole dostaje stały klucz (`energy_backward_t1_kwh`) i polską nazwę zależną od nośnika („Energia oddana T1” dla prądu, „Przepływ wsteczny” dla wody).
- **Sterowniki tylko tam, gdzie licznik odbiega od OMS** (`wmb_drivers.cpp`):
  - `izar` — Diehl Izar/PRIOS (SAP, HYD, DME): dane LFSR z kluczem domyślnym albo własnym, numer z obudowy (np. `C21FA000711`), alarmy,
  - `apator162` — stan licznika z rejestrów producenta,
  - `hydrodigit` — bateria, przepływ wsteczny, data wycieku, 12 stanów miesięcznych,
  - `sanxing` — znacznik `609B` zamiast `2F2F`.
  - Diehl OMS bez podanego klucza: używany jest publicznie znany klucz producenta.
- **Szyfrowanie:** AES-CBC tryb 5, tryb 7 (klucz z AES-CMAC i licznika z AFL), ELL AES-CTR (Kamstrup). Na ESP32 przez mbedTLS ze sprzętowym AES.
- **Rozmiar:** ok. 27 kB kodu.

## Radio

`components/sihos16/radio_cc1101.*` — odbiór T1/C1 (formaty A i B) na CC1101, maszyna stanów przeniesiona z `smartinhome/esphome-components`.

- Osobne zadanie FreeRTOS (priorytet 3, rdzeń 1) budzone przerwaniami GDO0 (próg FIFO) i GDO2 (synchronizacja) — długie operacje w pętli ESPHome nie gubią ramek.
- RSSI mierzone w chwili synchronizacji, a nie po odebraniu ramki.
- CRC sprawdzane już w radiu (`wmb_link.*`): do dekodera trafiają tylko poprawne ramki, szum jest liczony i odrzucany.
- Kolejka 8 ramek, pętla opróżnia ją w całości; co minutę statystyka w logu.



## Pierwsze uruchomienie u klienta

YAML nie zawiera sieci WiFi (`examples/sih-reader-1.6.yaml`), więc moduł bez zapisanej sieci uruchamia hotspot **SIH Reader** (dioda niebieska):

1. Klient łączy się z hotspotem telefonem — strona modułu otwiera się sama (portal).
2. Wybiera swoją sieć z listy (albo wpisuje nazwę), podaje hasło → „Zapisz i połącz”. Dane trafiają do pamięci modułu.
3. Moduł łączy się z siecią, Home Assistant wykrywa nowe urządzenie ESPHome, strona jest pod `http://sih-reader.local/`.
4. Błędne hasło: po około minucie moduł znowu uruchamia hotspot.

**Ustawienia fabryczne** (sieć WiFi + liczniki): przycisk na stronie albo przytrzymanie przycisku **BOOT** przez 10 s (dioda miga na biało od 2. sekundy, puszczenie przerywa). Moduł wraca do hotspotu — gotowy do przekazania klientowi.

**Diody:** RGB — niebieska: hotspot konfiguracji, czerwona: brak WiFi, żółta: WiFi bez Home Assistant, zielona: połączono z HA. Dioda odbioru mruga przy ramkach moich liczników (albo wszystkich, albo wcale). Jasność i tryby — na stronie, bez restartu.

**Aktualizacja:** plik `.bin` (OTA) wgrywany ze strony modułu (`ota: - platform: web_server`).

## Liczniki i Home Assistant

Konfiguracja YAML jest minimalna (`examples/sih-reader-1.6.yaml`): WiFi, piny i komponent `sihos16`. Liczniki dodaje się na stronie modułu `http://<adres>/` (albo `/sihos16`, gdy w YAML jest też `web_server`):

1. Lista „Słyszane w okolicy” pokazuje liczniki w zasięgu (producent, numer, rodzaj, sygnał).
2. „Dodaj” → nazwa, klucz AES (32 znaki hex lub 16 znaków tekstu, np. klucz PGE) → „Sprawdź i pokaż pola” dekoduje ostatnią ramkę i pokazuje wszystkie pola z wartościami.
3. Zaznaczasz pola (do 16 liczbowych i 4 tekstowych) → „Zapisz”: konfiguracja trafia do pamięci modułu, moduł uruchamia się ponownie.
4. Licznik pojawia się w Home Assistant jako **osobne urządzenie** z encjami o polskich nazwach, jednostkach i klasach (energia, woda, gaz, moc, napięcie…) — energia i objętość od razu nadają się do panelu Energia.

Jak to działa: przy kompilacji powstaje pula 8 urządzeń, a w każdym 16 encji liczbowych i 4 tekstowe. Przy starcie moduł nadaje zajętym nazwy, jednostki i klasy z konfiguracji w NVS; reszta jest ukryta. Usunięty licznik znika z HA razem z urządzeniem.

API strony: `GET /sihos16/api/state`, `GET /sihos16/api/preview?id=&key=&driver=`, `POST /sihos16/api/save`, `POST /sihos16/api/delete`, `GET /sihos16/api/system`, `POST /sihos16/api/wifi`, `POST /sihos16/api/leds`, `POST /sihos16/api/factory_reset`.

## Testy na komputerze

```bash
cd tests/host
make test        # wymaga g++ i libssl-dev
```

- `real_frames.json` — ramki z prawdziwych instalacji (Sanxing, 12 liczników Izar, 4× Apator 162, 4× Hydrodigit), sprawdzane pole po polu.
- `link_frames.json` — surowe pakiety radiowe (z CRC) z logów SIH Reader: sprawdzanie CRC musi dać to samo co stary fork, a szum odrzucić.
- `vectors.json` — telegramy testowe z [wmbusmeters](https://github.com/wmbusmeters/wmbusmeters) (GPL-3.0), wyciągnięte skryptem `extract_xmq.py`. Porównywane są wartości, nie nazwy pól.

## Licencja

GPL-3.0-or-later (patrz `LICENSE`). Telegramy testowe pochodzą z projektu wmbusmeters © Fredrik Öhrström i współautorzy.
