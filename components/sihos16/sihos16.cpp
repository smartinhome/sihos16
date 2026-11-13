// sihos16 — komponent ESPHome: radio CC1101, dekoder wM-Bus, liczniki z pamięci modułu jako urządzenia HA.
// Copyright (C) 2026 smartinhome.pl, GPL-3.0-or-later.
#include "sihos16.h"

#include <cinttypes>
#include <cstdlib>
#include <cstring>

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "page.h"
#include "wmb_decoder.h"
#include "wmb_drivers.h"

namespace esphome::sihos16 {

using namespace sihos::wmb;

static const char *const TAG = "sihos16";

static constexpr UBaseType_t QUEUE_DEPTH = 8;
static constexpr uint32_t STACK = 4096;
static constexpr UBaseType_t PRIORITY = 3;  // nad pętlą ESPHome (1): długa operacja w pętli nie gubi ramek
static constexpr uint32_t STATS_EVERY_MS = 60000;
static constexpr const char *RSSI_KEY = "rssi_dbm";

// ======================= pomocnicze =======================

namespace {

void copy_str(char *dst, size_t n, const std::string &src) {
  size_t len = src.size() < n - 1 ? src.size() : n - 1;
  // Nie ucinamy w połowie znaku UTF-8.
  while (len > 0 && len < src.size() && (static_cast<uint8_t>(src[len]) & 0xC0) == 0x80) len--;
  memcpy(dst, src.data(), len);
  dst[len] = 0;
}

void json_str(std::string &o, const char *s) {
  o += '"';
  for (; *s; s++) {
    char c = *s;
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if (static_cast<uint8_t>(c) < 0x20) {
      char b[8];
      snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
    } else {
      o += c;
    }
  }
  o += '"';
}

void json_num(std::string &o, double v) {
  char b[32];
  if (std::isfinite(v)) snprintf(b, sizeof b, "%.10g", v);
  else snprintf(b, sizeof b, "null");
  o += b;
}

void json_kv(std::string &o, const char *k, const char *v, bool comma = true) {
  if (comma) o += ',';
  json_str(o, k);
  o += ':';
  json_str(o, v);
}

void json_kn(std::string &o, const char *k, double v, bool comma = true) {
  if (comma) o += ',';
  json_str(o, k);
  o += ':';
  json_num(o, v);
}

std::string id_hex(uint32_t id) {
  char b[12];
  snprintf(b, sizeof b, "%08" PRIx32, id);
  return b;
}

bool archival(const char *key) {
  for (const char *p = key; *p; p++)
    if (p[0] == '_' && p[1] == 's' && p[2] >= '0' && p[2] <= '9') return true;
  return strstr(key, "month") || strstr(key, "_limit") || strstr(key, "_max") || strstr(key, "_min") ||
         strstr(key, "_err");
}

struct HaMeta {
  const char *dc;
  sensor::StateClass sc;
  int8_t accuracy;
};

// Klasa urządzenia, klasa stanu i dokładność w HA na podstawie jednostki i rodzaju pola.
HaMeta ha_meta(const SelField &f, Media media) {
  const char *u = f.unit;
  bool arch = archival(f.key);
  auto total = arch ? sensor::STATE_CLASS_NONE : sensor::STATE_CLASS_TOTAL_INCREASING;
  auto meas = sensor::STATE_CLASS_MEASUREMENT;
  if (!strcmp(u, "kWh") || !strcmp(u, "GJ")) return {"energy", total, 3};
  if (!strcmp(u, "m³")) {
    bool water = media == Media::WATER || media == Media::WARM_WATER || media == Media::HOT_WATER ||
                 media == Media::COLD_WATER || media == Media::DUAL_WATER;
    return {water ? "water" : media == Media::GAS ? "gas" : "volume", total, 3};
  }
  if (!strcmp(u, "kvarh")) return {"reactive_energy", total, 3};
  if (!strcmp(u, "W")) return {"power", meas, 0};
  if (!strcmp(u, "var")) return {"reactive_power", meas, 0};
  if (!strcmp(u, "V")) return {"voltage", meas, 1};
  if (!strcmp(u, "A")) return {"current", meas, 2};
  if (!strcmp(u, "°C") || !strcmp(u, "K")) return {"temperature", meas, 1};
  if (!strcmp(u, "bar")) return {"pressure", meas, 2};
  if (!strcmp(u, "m³/h")) return {"volume_flow_rate", meas, 3};
  if (!strcmp(u, "kg")) return {"weight", total, 1};
  if (!strcmp(u, "dBm")) return {"signal_strength", meas, 0};
  if (!strcmp(u, "h")) return {"duration", sensor::STATE_CLASS_TOTAL_INCREASING, 0};
  if (!strcmp(u, "d") || !strcmp(u, "s")) return {"duration", meas, 0};
  return {"", arch ? sensor::STATE_CLASS_NONE : meas, 2};
}

}  // namespace

// ======================= start =======================

void Sihos16::setup() {
  this->load_meters_();
  this->bind_entities_();

  if (this->web_base_ != nullptr) {
    this->web_base_->init();
    this->web_base_->add_handler(this);
  }

  this->gdo0_->setup();
  this->gdo0_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLDOWN);
  this->gdo2_->setup();
  this->gdo2_->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLDOWN);
  this->spi_setup();

  if (!this->radio_.begin(this, this->gdo0_, this->gdo2_, this->mhz_)) {
    this->status_set_error(LOG_STR("CC1101 nie odpowiada"));
    this->mark_failed();
    return;
  }

  this->queue_ = xQueueCreate(QUEUE_DEPTH, sizeof(RadioFrame));
  BaseType_t ok = xTaskCreatePinnedToCore(Sihos16::radio_task_, "sihos16_radio", STACK, this, PRIORITY,
                                          &this->task_, portNUM_PROCESSORS > 1 ? 1 : 0);
  if (this->queue_ == nullptr || ok != pdPASS) {
    ESP_LOGE(TAG, "Nie udało się uruchomić zadania radia");
    this->mark_failed();
    return;
  }
  // GDO0: FIFO zapełnione do progu, GDO2: synchronizacja. Oba budzą zadanie od razu.
  this->gdo0_->attach_interrupt(Sihos16::gdo_isr_, this, gpio::INTERRUPT_RISING_EDGE);
  this->gdo2_->attach_interrupt(Sihos16::gdo_isr_, this, gpio::INTERRUPT_RISING_EDGE);
}

void Sihos16::load_meters_() {
  for (int s = 0; s < MAX_METERS; s++) {
    char name[24];
    snprintf(name, sizeof name, "sihos16_meter_v1_%d", s);
    this->prefs_[s] = global_preferences->make_preference<MeterConfig>(fnv1_hash(name), true);
    MeterConfig &c = this->cfg_[s];
    if (!this->prefs_[s].load(&c) || c.used != 1 || c.nfields > MAX_FIELDS) memset(&c, 0, sizeof c);
    c.name[sizeof c.name - 1] = 0;
    c.driver[sizeof c.driver - 1] = 0;
    for (int i = 0; i < c.nfields; i++) {
      c.fields[i].key[sizeof c.fields[i].key - 1] = 0;
      c.fields[i].label[sizeof c.fields[i].label - 1] = 0;
      c.fields[i].unit[sizeof c.fields[i].unit - 1] = 0;
    }
  }
}

uint8_t Sihos16::unit_idx_(const char *u) const {
  for (const auto &n : this->units_)
    if (!strcmp(n.name, u)) return n.idx;
  return 0;
}

uint8_t Sihos16::dc_idx_(const char *dc) const {
  for (const auto &n : this->dcs_)
    if (!strcmp(n.name, dc)) return n.idx;
  return 0;
}

// Encje z puli dostają nazwy i jednostki z konfiguracji. Musi się to stać przed połączeniem z HA.
void Sihos16::bind_entities_() {
  memset(this->num_field_, -1, sizeof this->num_field_);
  memset(this->text_field_, -1, sizeof this->text_field_);
  for (int s = 0; s < MAX_METERS; s++) {
    const MeterConfig &c = this->cfg_[s];
    if (!c.used || this->devices_[s] == nullptr) continue;
    this->devices_[s]->set_name(c.name);
    App.register_device(this->devices_[s]);
    int n = 0, t = 0;
    for (int i = 0; i < c.nfields; i++) {
      const SelField &f = c.fields[i];
      bool diag = !strcmp(f.key, RSSI_KEY) || !strcmp(f.key, "status");
      if (f.is_text) {
        if (t >= MAX_TEXT_FIELDS || this->texts_[s][t] == nullptr) continue;
        this->texts_[s][t]->bind(f.label, diag);
        this->text_field_[s][t++] = static_cast<int8_t>(i);
      } else {
        if (n >= MAX_NUM_FIELDS || this->nums_[s][n] == nullptr) continue;
        HaMeta m = ha_meta(f, static_cast<Media>(c.media));
        SlotSensor *e = this->nums_[s][n];
        e->bind(f.label, this->unit_idx_(f.unit), this->dc_idx_(m.dc), diag);
        e->set_state_class(m.sc);
        e->set_accuracy_decimals(m.accuracy);
        this->num_field_[s][n++] = static_cast<int8_t>(i);
      }
    }
    ESP_LOGI(TAG, "Licznik %d: %s (%08" PRIx32 "), encji %d + %d", s + 1, c.name, c.id, n, t);
  }
  // API zawsze wysyła ESPHOME_DEVICE_COUNT pozycji; puste (numer 0) HA pokazałby jako puste urządzenie.
  // Wolne pozycje wypełniamy pierwszym licznikiem — HA scala identyczne wpisy w jedno urządzenie.
  Device *first = nullptr;
  for (int s = 0; s < MAX_METERS && first == nullptr; s++)
    if (this->cfg_[s].used) first = this->devices_[s];
  if (first != nullptr)
    while (App.get_devices().size() < MAX_METERS) App.register_device(first);
}

// ======================= radio =======================

void IRAM_ATTR Sihos16::gdo_isr_(Sihos16 *self) {
  BaseType_t woken = pdFALSE;
  if (self->task_ != nullptr) vTaskNotifyGiveFromISR(self->task_, &woken);
  portYIELD_FROM_ISR(woken);
}

void Sihos16::radio_task_(void *arg) {
  auto *self = static_cast<Sihos16 *>(arg);
  static RadioFrame f;  // jedno zadanie radia — bufor poza stosem
  for (;;) {
    if (self->radio_.poll(f)) {
      if (xQueueSend(self->queue_, &f, 0) != pdTRUE) self->radio_.stats.dropped++;
      continue;
    }
    // W trakcie ramki sprawdzamy co 1 ms; w spoczynku czekamy na przerwanie (z zapasem 2 ms).
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(self->radio_.busy() ? 1 : 2));
  }
}

void Sihos16::loop() {
  uint32_t now = millis();
  if (this->queue_ != nullptr) {
    while (xQueueReceive(this->queue_, &this->frame_, 0) == pdTRUE) this->handle_frame_(this->frame_);
    if (now - this->last_stats_ms_ >= STATS_EVERY_MS) {
      this->last_stats_ms_ = now;
      this->log_stats_();
    }
  }

  // Zmiany ze strony: zapis do NVS i restart, żeby HA dostał nową listę encji.
  bool save = false;
  int slot = -1;
  {
    LockGuard g(this->lock_);
    if (this->pending_save_) {
      save = true;
      slot = this->pending_slot_;
      this->cfg_[slot] = this->pending_cfg_;
      this->pending_save_ = false;
    }
  }
  if (save) {
    this->prefs_[slot].save(&this->cfg_[slot]);
    global_preferences->sync();
    ESP_LOGI(TAG, "Zapisano licznik %d — restart za chwilę", slot + 1);
    this->reboot_at_ = now + 1500;
  }
  if (this->reboot_at_ != 0 && static_cast<int32_t>(now - this->reboot_at_) >= 0) App.safe_reboot();
}

int Sihos16::find_meter_(const Address &a) const {
  for (int s = 0; s < MAX_METERS; s++) {
    const MeterConfig &c = this->cfg_[s];
    if (c.used && c.id == a.id && (c.mfct == 0 || c.mfct == (a.mfct & 0x7FFF))) return s;
  }
  return -1;
}

void Sihos16::handle_frame_(const RadioFrame &f) {
  Address a;
  if (!peek_address(f.data, f.len, a)) return;
  int s = this->find_meter_(a);
  if (s >= 0) {
    const MeterConfig &c = this->cfg_[s];
    Result r = decode(f.data, f.len, c.has_key ? c.key : nullptr, c.driver);
    ESP_LOGI(TAG, "%s (%08" PRIx32 "): %s, %d dBm, pól %u", c.name, a.id, status_str(r.status), f.rssi,
             static_cast<unsigned>(r.fields.size()));
    {
      LockGuard g(this->lock_);
      MeterState &st = this->state_[s];
      st.last_ms = millis();
      st.frames++;
      st.rssi = f.rssi;
      st.status = r.status;
      st.frame.assign(f.data, f.data + f.len);
      st.result = std::move(r);
    }
    this->publish_(s);
    return;
  }

  // Licznik spoza konfiguracji: pamiętamy do podglądu na stronie.
  Result r = decode(f.data, f.len, nullptr, "auto");
  ESP_LOGD(TAG, "w okolicy: %s %08" PRIx32 " typ %02X (%s), %d dBm, %s", mfct_str(a.mfct).c_str(), a.id, a.type,
           media_str(r.media), f.rssi, status_str(r.status));
  LockGuard g(this->lock_);
  Heard *h = nullptr;
  for (auto &e : this->heard_)
    if (e.addr.id == a.id && e.addr.mfct == a.mfct) h = &e;
  if (h == nullptr) {
    if (this->heard_.size() < MAX_HEARD) {
      this->heard_.emplace_back();
      h = &this->heard_.back();
    } else {  // zastępujemy najdawniej słyszany
      h = &this->heard_[0];
      for (auto &e : this->heard_)
        if (static_cast<int32_t>(e.last_ms - h->last_ms) < 0) h = &e;
      h->count = 0;
    }
  }
  h->addr = r.addr;
  h->last_ms = millis();
  h->count++;
  h->rssi = f.rssi;
  h->status = r.status;
  h->media = r.media;
  h->driver = r.driver;
  h->frame.assign(f.data, f.data + f.len);
}

void Sihos16::publish_(int s) {
  const MeterConfig &c = this->cfg_[s];
  const MeterState &st = this->state_[s];  // zapisywany tylko w tej pętli — odczyt bez blokady
  for (int n = 0; n < MAX_NUM_FIELDS; n++) {
    int i = this->num_field_[s][n];
    if (i < 0) continue;
    const SelField &sf = c.fields[i];
    if (!strcmp(sf.key, RSSI_KEY)) {
      this->nums_[s][n]->publish_state(st.rssi);
      continue;
    }
    const Field *v = st.result.find(sf.key);
    if (v != nullptr && !v->is_text) this->nums_[s][n]->publish_state(v->value);
  }
  for (int t = 0; t < MAX_TEXT_FIELDS; t++) {
    int i = this->text_field_[s][t];
    if (i < 0) continue;
    const Field *v = st.result.find(c.fields[i].key);
    if (v != nullptr) this->texts_[s][t]->publish_state(v->is_text ? v->text : std::to_string(v->value));
  }
}

void Sihos16::log_stats_() {
  const RadioStats &s = this->radio_.stats;
  ESP_LOGI(TAG, "Radio: ramki %u, błędne CRC %u, błędy 3-of-6 %u, urwane %u, przepełnienia FIFO %u, utracone %u",
           (unsigned) s.frames, (unsigned) s.crc_errors, (unsigned) s.code_errors, (unsigned) s.truncated,
           (unsigned) s.overflows, (unsigned) s.dropped);
}

void Sihos16::dump_config() {
  ESP_LOGCONFIG(TAG, "SIHOS16 wM-Bus:");
  LOG_PIN("  CS: ", this->cs_);
  LOG_PIN("  GDO0: ", this->gdo0_);
  LOG_PIN("  GDO2: ", this->gdo2_);
  ESP_LOGCONFIG(TAG, "  Częstotliwość: %.3f MHz", this->mhz_);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  CC1101 nie wykryty");
  } else {
    ESP_LOGCONFIG(TAG, "  CC1101 wersja 0x%02X", this->radio_.version());
  }
  int used = 0;
  for (const auto &c : this->cfg_) used += c.used;
  ESP_LOGCONFIG(TAG, "  Liczniki: %d z %d (konfiguracja: http://<adres modułu>/sihos16)", used, MAX_METERS);
}

// ======================= strona i API =======================

bool Sihos16::canHandle(AsyncWebServerRequest *request) const {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef url = request->url_to(buf);
  if (strncmp(url.c_str(), "/sihos16", 8) == 0) return true;
#ifndef USE_WEBSERVER
  if (url == "/") return true;  // bez web_server strona liczników jest stroną główną
#endif
  return false;
}

void Sihos16::handleRequest(AsyncWebServerRequest *req) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  std::string url = req->url_to(buf).str();
  if (url == "/" || url == "/sihos16" || url == "/sihos16/") {
    req->send(200, "text/html; charset=utf-8", PAGE_HTML);
  } else if (url == "/sihos16/api/state") {
    this->send_state_(req);
  } else if (url == "/sihos16/api/preview") {
    this->send_preview_(req);
  } else if (url == "/sihos16/api/save" && req->method() == HTTP_POST) {
    this->handle_save_(req);
  } else if (url == "/sihos16/api/delete" && req->method() == HTTP_POST) {
    this->handle_delete_(req);
  } else {
    req->send(404, "text/plain", "Nie ma takiej strony");
  }
}

void Sihos16::send_state_(AsyncWebServerRequest *req) {
  std::string o;
  o.reserve(4096);
  uint32_t now = millis();
  const RadioStats &rs = this->radio_.stats;
  o += "{\"radio\":{";
  json_kn(o, "frames", rs.frames, false);
  json_kn(o, "crc", rs.crc_errors);
  json_kn(o, "code", rs.code_errors);
  json_kn(o, "truncated", rs.truncated);
  json_kn(o, "overflow", rs.overflows);
  json_kn(o, "dropped", rs.dropped);
  json_kn(o, "ok", !this->is_failed());
  o += "},\"max_meters\":";
  json_num(o, MAX_METERS);
  o += ",\"max_num\":";
  json_num(o, MAX_NUM_FIELDS);
  o += ",\"max_text\":";
  json_num(o, MAX_TEXT_FIELDS);
  o += ",\"drivers\":[\"auto\",\"oms\"";
  size_t nd;
  const Driver *const *drv = all_drivers(nd);
  for (size_t i = 0; i < nd; i++) {
    o += ',';
    json_str(o, drv[i]->name);
  }
  o += "],\"meters\":[";
  {
    LockGuard g(this->lock_);
    bool first = true;
    for (int s = 0; s < MAX_METERS; s++) {
      const MeterConfig &c = this->cfg_[s];
      if (!c.used) continue;
      const MeterState &st = this->state_[s];
      if (!first) o += ',';
      first = false;
      o += '{';
      json_kn(o, "slot", s, false);
      json_kv(o, "id", id_hex(c.id).c_str());
      json_kv(o, "mfct", c.mfct ? mfct_str(c.mfct).c_str() : "");
      json_kv(o, "name", c.name);
      json_kv(o, "driver", c.driver);
      json_kn(o, "has_key", c.has_key);
      json_kv(o, "media", media_str(static_cast<Media>(c.media)));
      json_kn(o, "seen", st.last_ms ? (now - st.last_ms) / 1000 : -1);
      json_kn(o, "rssi", st.rssi);
      json_kn(o, "frames", st.frames);
      json_kv(o, "status", st.last_ms ? status_str(st.status) : "jeszcze nie odebrano");
      o += ",\"fields\":[";
      for (int i = 0; i < c.nfields; i++) {
        const SelField &f = c.fields[i];
        if (i) o += ',';
        o += '{';
        json_kv(o, "key", f.key, false);
        json_kv(o, "label", f.label);
        json_kv(o, "unit", f.unit);
        json_kn(o, "text", f.is_text);
        if (!strcmp(f.key, RSSI_KEY)) {
          if (st.last_ms) json_kn(o, "value", st.rssi);
        } else if (const Field *v = st.result.find(f.key)) {
          if (v->is_text) json_kv(o, "value", v->text.c_str());
          else json_kn(o, "value", v->value);
        }
        o += '}';
      }
      o += "]}";
    }
    o += "],\"heard\":[";
    first = true;
    for (const auto &h : this->heard_) {
      if (!first) o += ',';
      first = false;
      char t[8];
      o += '{';
      json_kv(o, "id", id_hex(h.addr.id).c_str(), false);
      json_kv(o, "mfct", mfct_str(h.addr.mfct).c_str());
      snprintf(t, sizeof t, "%02X", h.addr.type);
      json_kv(o, "type", t);
      snprintf(t, sizeof t, "%02X", h.addr.version);
      json_kv(o, "ver", t);
      json_kv(o, "media", media_str(h.media));
      json_kv(o, "driver", h.driver);
      json_kv(o, "status", status_str(h.status));
      json_kn(o, "rssi", h.rssi);
      json_kn(o, "count", h.count);
      json_kn(o, "seen", (now - h.last_ms) / 1000);
      o += '}';
    }
  }
  o += "]}";
  req->send(200, "application/json", o.c_str());
}

// Dekoduje ostatnią ramkę licznika z podanym kluczem — lista pól do wyboru na stronie.
void Sihos16::send_preview_(AsyncWebServerRequest *req) {
  uint32_t id = strtoul(req->arg("id").c_str(), nullptr, 16);
  std::string key_s = req->arg("key"), drv = req->arg("driver");
  if (drv.empty()) drv = "auto";
  std::vector<uint8_t> frame;
  uint8_t key[16];
  bool have_key = false;
  int8_t rssi = 0;
  {
    LockGuard g(this->lock_);
    for (int s = 0; s < MAX_METERS; s++) {
      const MeterConfig &c = this->cfg_[s];
      if (c.used && c.id == id && !this->state_[s].frame.empty()) {
        frame = this->state_[s].frame;
        rssi = this->state_[s].rssi;
        if (key_s == "*" && c.has_key) {
          memcpy(key, c.key, 16);
          have_key = true;
        }
      }
    }
    if (frame.empty())
      for (const auto &h : this->heard_)
        if (h.addr.id == id) {
          frame = h.frame;
          rssi = h.rssi;
        }
  }
  if (frame.empty()) {
    req->send(200, "application/json", "{\"status\":\"jeszcze nie odebrano ramki z tego licznika\",\"fields\":[]}");
    return;
  }
  if (!have_key && !key_s.empty() && key_s != "*") {
    if (!parse_key(key_s.c_str(), key)) {
      req->send(200, "application/json",
                "{\"status\":\"klucz musi mieć 32 znaki hex albo 16 znaków tekstu\",\"fields\":[]}");
      return;
    }
    have_key = true;
  }
  Result r = decode(frame.data(), frame.size(), have_key ? key : nullptr, drv.c_str());
  std::string o = "{";
  json_kv(o, "status", status_str(r.status), false);
  json_kn(o, "ok", r.status == Status::OK);
  json_kv(o, "driver", r.driver);
  json_kv(o, "media", media_str(r.media));
  json_kv(o, "mfct", mfct_str(r.addr.mfct).c_str());
  o += ",\"fields\":[";
  for (size_t i = 0; i < r.fields.size(); i++) {
    const Field &f = r.fields[i];
    if (i) o += ',';
    o += '{';
    json_kv(o, "key", f.key.c_str(), false);
    json_kv(o, "label", f.label.c_str());
    json_kv(o, "unit", f.unit);
    json_kn(o, "text", f.is_text);
    if (f.is_text) json_kv(o, "value", f.text.c_str());
    else json_kn(o, "value", f.value);
    o += '}';
  }
  o += r.fields.empty() ? "" : ",";
  o += "{\"key\":\"rssi_dbm\",\"label\":\"Sygnał (RSSI)\",\"unit\":\"dBm\",\"text\":0,\"value\":";
  json_num(o, rssi);
  o += "}]}";
  req->send(200, "application/json", o.c_str());
}

void Sihos16::handle_save_(AsyncWebServerRequest *req) {
  int slot = req->hasArg("slot") ? atoi(req->arg("slot").c_str()) : -1;
  std::string key_s = req->arg("key"), mf = req->arg("mfct"), drv = req->arg("driver"), fields = req->arg("fields");
  MeterConfig c{};
  c.used = 1;
  c.id = strtoul(req->arg("id").c_str(), nullptr, 16);
  if (mf.size() == 3) c.mfct = mfct_code(mf.c_str());
  copy_str(c.name, sizeof c.name, req->arg("name"));
  if (c.name[0] == 0) snprintf(c.name, sizeof c.name, "Licznik %08" PRIx32, c.id);
  copy_str(c.driver, sizeof c.driver, drv.empty() ? std::string("auto") : drv);

  LockGuard g(this->lock_);
  if (slot < 0 || slot >= MAX_METERS) {
    slot = -1;
    for (int s = 0; s < MAX_METERS && slot < 0; s++)
      if (this->cfg_[s].used && this->cfg_[s].id == c.id) slot = s;  // ten sam licznik: nadpisz
    for (int s = 0; s < MAX_METERS && slot < 0; s++)
      if (!this->cfg_[s].used) slot = s;
    if (slot < 0) {
      req->send(200, "application/json", "{\"ok\":false,\"error\":\"Wszystkie 8 miejsc na liczniki jest zajętych\"}");
      return;
    }
  }
  const MeterConfig &old = this->cfg_[slot];
  if (key_s == "*") {
    c.has_key = old.has_key;
    memcpy(c.key, old.key, 16);
  } else if (!key_s.empty()) {
    if (!parse_key(key_s.c_str(), c.key)) {
      req->send(200, "application/json", "{\"ok\":false,\"error\":\"Klucz: 32 znaki hex albo 16 znaków tekstu\"}");
      return;
    }
    c.has_key = 1;
  }

  // Opisy pól z aktualnej ramki (z nowym kluczem), a gdy jej brak — z dotychczasowej konfiguracji.
  std::vector<uint8_t> frame;
  if (old.used && old.id == c.id) frame = this->state_[slot].frame;
  if (frame.empty())
    for (const auto &h : this->heard_)
      if (h.addr.id == c.id) frame = h.frame;
  Result r;
  if (!frame.empty()) {
    r = decode(frame.data(), frame.size(), c.has_key ? c.key : nullptr, c.driver);
    c.media = static_cast<uint8_t>(r.media);
    if (c.mfct == 0) c.mfct = r.addr.mfct & 0x7FFF;
  } else {
    c.media = old.media;
  }
  int nnum = 0, ntext = 0;
  size_t p = 0;
  while (p <= fields.size() && c.nfields < MAX_FIELDS) {
    size_t e = fields.find(',', p);
    if (e == std::string::npos) e = fields.size();
    std::string k = fields.substr(p, e - p);
    p = e + 1;
    if (k.empty()) continue;
    SelField sf{};
    bool found = false;
    if (k == RSSI_KEY) {
      copy_str(sf.label, sizeof sf.label, "Sygnał (RSSI)");
      copy_str(sf.unit, sizeof sf.unit, "dBm");
      found = true;
    } else if (const Field *f = r.find(k)) {
      copy_str(sf.label, sizeof sf.label, f->label);
      copy_str(sf.unit, sizeof sf.unit, f->unit);
      sf.is_text = f->is_text;
      found = true;
    } else {
      for (int i = 0; i < old.nfields && !found; i++)
        if (k == old.fields[i].key) {
          sf = old.fields[i];
          found = true;
        }
    }
    if (!found) continue;
    if (sf.is_text ? ntext >= MAX_TEXT_FIELDS : nnum >= MAX_NUM_FIELDS) continue;
    (sf.is_text ? ntext : nnum)++;
    copy_str(sf.key, sizeof sf.key, k);
    c.fields[c.nfields++] = sf;
  }

  this->pending_cfg_ = c;
  this->pending_slot_ = slot;
  this->pending_save_ = true;
  req->send(200, "application/json", "{\"ok\":true}");
}

void Sihos16::handle_delete_(AsyncWebServerRequest *req) {
  int slot = atoi(req->arg("slot").c_str());
  if (slot < 0 || slot >= MAX_METERS) {
    req->send(400, "application/json", "{\"ok\":false}");
    return;
  }
  LockGuard g(this->lock_);
  this->pending_cfg_ = MeterConfig{};
  this->pending_slot_ = slot;
  this->pending_save_ = true;
  req->send(200, "application/json", "{\"ok\":true}");
}

}  // namespace esphome::sihos16
