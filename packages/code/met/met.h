// met_scene.h as a panel picture. met.yaml calls fetch() for the forecast, poll() for its answer and redraw()
// every few minutes so the sun and moon move. Only changed rows are sent to the panel. MET Norway is asked again
// once its last answer has expired, with If-Modified-Since, in a task of its own so the bus is still answered, and
// less often while it refuses or doesn't answer, see fetched() and unreachable().
#pragma once
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <strings.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include "../panel/items.h"
#include "../panel/picture.h"
#include "met_live_shows.h"
#include "met_raster.h"

// True in fireplace mode. Weak, since packages/code/fireplace.h only defines it when that package is included.
extern "C" bool flexit_fireplace_active() __attribute__((weak));

namespace flexit::panel::met {

inline Weather weather;  // the forecast, from the coming hour on

// The dashboard tile's label and icon for each Icon
struct Kind {
  const char *name, *icon;
};
inline constexpr Kind KINDS[] = {{"Weather|Vær", "cloud"},   {"Clear|Klart", "sun"},      {"Clear|Klart", "night"},
                                 {"Fair|Lettskyet", "sun"},  {"Fair|Lettskyet", "night"}, {"Cloudy|Skyet", "cloud"},
                                 {"Rain|Regn", "rain"},      {"Snow|Snø", "snowy"},       {"Sleet|Sludd", "rain"},
                                 {"Thunder|Torden", "bolt"}, {"Fog|Tåke", "cloud"}};
static_assert(std::size(KINDS) == I_FOG + 1, "a name and an icon for each of met_forecast.h's Icons");
// MET Norway wants to know who's asking
inline constexpr const char *USER_AGENT =
    "esphome-flexit-modbus-server (github.com/MSkjel/esphome-flexit-modbus-server)";
// The request's task: TLS's handshake, then the reader's 4 KB buffer in the same room. As big as ESPHome's main
// loop's, which also does HTTPS.
inline constexpr uint32_t STACK = 8192;
inline int now_item = -1;
inline std::string modified;  // Last-Modified of our forecast, for If-Modified-Since. Empty until we have one
inline uint32_t not_before;   // no request before this (Unix time): the last answer's Expires, or a back-off
inline uint32_t backoff_s;    // doubles with each failure in a row
inline bool answered;         // MET Norway has answered since boot
inline uint8_t early_tries;   // quick retries before that
inline bool retry_soon;       // one of those is due, tick() makes it
inline constexpr uint32_t EARLY_RETRY_S = 30;
inline char url[128];
inline double lat, lon;
inline int timeout_ms;  // http_request's
inline std::string name;
inline bool with_forecast;  // the Panel Weather Forecast switch
inline Options options;     // set from met.yaml
inline uint32_t pal[16];    // the scene's colours, for picture.h

// name's index in names, for met.yaml's selects. 0 if it's none of them
template<size_t N> int option_of(const char *const (&names)[N], const char *name) {
  for (size_t i = 0; i < N; i++)
    if (!strcmp(name, names[i]))
      return (int) i;
  ESP_LOGW("met", "No weather option called '%s', it's %s", name, names[0]);
  return 0;
}
inline Ground ground_of(const char *name) { return (Ground) option_of(GROUNDS, name); }
inline Aurora aurora_of(const char *name) { return (Aurora) option_of(AURORAS, name); }
// Publishes to Panel Weather, set by met.yaml's on_boot
inline std::function<void(const std::string &)> on_status = [](const std::string &) {};

inline uint32_t now_s() { return (uint32_t) id(flexit_panel_time).now().timestamp; }

inline void tile(Icon icon, float temp) {
  item_label(now_item, KINDS[icon].name);
  item_icon(now_item, KINDS[icon].icon);
  item_value(now_item, temp);
}

// The tile shows the forecast hour it is now. A forecast that has run out, with no answer from MET Norway for most
// of a day, is dropped.
inline void send_now() {
  if (!weather.ok)
    return;
  uint32_t t = now_s();
  if (t >= ends(weather)) {
    weather.ok = false;
    modified.clear();  // so the next answer is all of it
    tile(I_NONE, NAN);
    on_status("The forecast has run out");
    return;
  }
  Hour h = now_of(weather, t);
  tile(h.icon, h.temp);
}

// Local time minus UTC at t, which changes with summer time
inline int utc_offset(uint32_t t) {
  ESPTime l = ESPTime::from_epoch_local(t);
  return (int) (unix_of(l.year, l.month, l.day_of_month, l.hour, l.minute, l.second) - t);
}

// The scene, only while picture.h makes the picture. Its sprites and overlay are blocks of their own, the land is
// kept while it's the panel's picture.
struct Drawing {
  Scene scene;
  uint8_t row[W];  // the last row pixel() drew
  int row_y;
  Palette colours;  // its colours counted, while built is still false
};
inline Drawing *drawing;
inline Land *land;
inline bool built;     // drawing has the scene for now
inline bool counting;  // drawing has it but its colours, see prepare()

inline void free_drawing() {
  if (drawing) {
    RAMAllocator<Sprite>().deallocate(drawing->scene.sprite, SPRITES);
    if (drawing->scene.ov)
      RAMAllocator<uint8_t>().deallocate(drawing->scene.ov[0], OV_H * W / 4);
    RAMAllocator<Drawing>().deallocate(drawing, 1);
  }
  drawing = nullptr;
  built = counting = false;
  if (land && picture::S.want != name) {
    RAMAllocator<Land>().deallocate(land, 1);
    land = nullptr;
  }
}

inline bool new_drawing() {
  size_t ov_bytes = with_forecast ? OV_H * W / 4 : 0;
  if (!picture::has_ram(sizeof(Drawing) + SPRITES * sizeof(Sprite) + ov_bytes + (land ? 0 : sizeof(Land)),
                        sizeof(Drawing)))
    return false;
  bool new_land = !land;
  if (new_land && (land = RAMAllocator<Land>().allocate(1)))
    land->made = false;
  Drawing *c = RAMAllocator<Drawing>().allocate(1);
  Sprite *sprite = RAMAllocator<Sprite>().allocate(SPRITES);
  uint8_t *ov = ov_bytes ? RAMAllocator<uint8_t>().allocate(ov_bytes) : nullptr;
  if (!land || !c || !sprite || (ov_bytes && !ov)) {
    RAMAllocator<Drawing>().deallocate(c, 1);
    RAMAllocator<Sprite>().deallocate(sprite, SPRITES);
    RAMAllocator<uint8_t>().deallocate(ov, ov_bytes);
    if (new_land) {
      RAMAllocator<Land>().deallocate(land, 1);
      land = nullptr;
    }
    return false;
  }
  c->scene.sprite = sprite;
  c->scene.ov = (uint8_t (*)[W / 4]) ov;
  c->scene.land = land;
  drawing = c;
  return true;
}

// Builds the scene for now and starts counting its colours. False without the time or the RAM.
inline bool begin() {
  ESPTime t = id(flexit_panel_time).now();
  if (!t.is_valid())
    return false;
  if (drawing && !drawing->scene.ov != !with_forecast)
    free_drawing();  // the forecast went on or off
  if (!drawing && !new_drawing()) {
    picture::S.waiting = "no RAM to draw it";
    return false;
  }
#pragma GCC diagnostic push
// null without the fireplace package, but with it GCC warns the check is always true
#pragma GCC diagnostic ignored "-Waddress"
  options.fireplace = flexit_fireplace_active ? flexit_fireplace_active() : false;
#pragma GCC diagnostic pop
  options.frame++;
  Scene &s = drawing->scene;
  build(s, weather, lat, lon, (uint32_t) t.timestamp, with_forecast, utc_offset, options);
  s.live = live::leaves_out(s);  // what the animations that fit it draw themselves
  drawing->colours.start(s);
  counting = true;
  return true;
}

// Once the colours are counted: the 16 of them, keeping the panel's where they're the same if it's this picture
inline void finish() {
  Scene &s = drawing->scene;
  const picture::Pic &shown = picture::S.shown;
  drawing->colours.end(s, shown.name == name && shown.format == PIC_PALETTE ? shown.pal : nullptr);
  live::apply(s);
  memcpy(pal, s.pal, sizeof pal);
  drawing->row_y = -1;
  counting = false;
  built = true;
}

inline constexpr int COUNT_ROWS = 8;  // rows prepare() counts between looks at the time

// picture.h's prepare: the scene for now built, then its colours counted a few rows at a time until deadline_us.
// True once there's nothing more to do now, built or not to be built yet, which draw() then says.
inline bool prepare(uint32_t deadline_us) {
  if (built || (!counting && !begin()))
    return true;
  while (!drawing->colours.count(drawing->scene, COUNT_ROWS))
    if (!before(deadline_us))
      return false;
  finish();
  return true;
}

// The scene for now, all of it at once if prepare() hasn't. False without the time or the RAM, picture.h asks
// again a second later.
inline bool draw() {
  if (built)
    return true;
  if (!counting && !begin())
    return false;
  drawing->colours.count(drawing->scene, H);
  finish();
  return true;
}

// The time, the weather or a setting changed. Drawn again if it's the panel's picture.
inline void redraw() {
  send_now();
  built = counting = false;
  picture::changed(name);
}

// The Panel Animation Weather switch
inline void animate(bool on) {
  live::R.on = on;
  if (!on)
    live::stop();
  redraw();
}

// The picture's pixel as 0xRRGGBB, for a JPEG. Its 16 colours go to picture.h as rows of indexes.
inline uint32_t pixel(int x, int y) {
  if (!drawing || x < 0 || y < 0 || y >= H)
    return 0;
  Drawing &c = *drawing;
  if (c.row_y != y) {
    row(c.scene, y, c.row);
    c.row_y = y;
  }
  return c.scene.pal[c.row[x < W ? x : W - 1]];
}

// An HTTP date like "Mon, 19 Oct 2026 11:34:38 GMT" as Unix time, 0 if it isn't one
inline uint32_t http_date(const std::string &s) {
  static const char MONTHS[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {};
  int d, y, h, mi, sec;
  if (sscanf(s.c_str(), "%*3s, %d %3s %d %d:%d:%d", &d, mon, &y, &h, &mi, &sec) != 6)
    return 0;
  const char *m = strstr(MONTHS, mon);
  if (!m || strlen(mon) != 3 || (m - MONTHS) % 3)
    return 0;
  return unix_of(y, (int) (m - MONTHS) / 3 + 1, d, h, mi, sec);
}

// Whether to ask MET Norway now. They want no new request before the last answer's Expires.
// Offline it waits for the next check, a failed request would back off.
inline bool due() { return network::is_connected() && id(flexit_panel_time).now().is_valid() && now_s() >= not_before; }

// After a failure, wait first, then twice as long each time up to most
inline void back_off(uint32_t first, uint32_t most) {
  backoff_s = std::clamp(2 * backoff_s, first, most);
  not_before = now_s() + backoff_s;
}

// No answer, or one cut off: more likely our own network than MET Norway, so ten minutes and up to an hour.
// Right after boot the network often isn't quite there yet and the ESP is busy, so the first few tries go again
// EARLY_RETRY_S later rather than at the next check.
inline void unreachable() {
  if (!answered && early_tries < 3) {
    early_tries++;
    not_before = now_s() + EARLY_RETRY_S;
    retry_soon = true;
  } else {
    back_off(600, 3600);
    retry_soon = false;
  }
  on_status("Couldn't reach MET Norway");
}

// A request runs in a task of its own, so the main loop keeps answering the bus through the TLS handshake and the
// 24 KB of answer. fetch() gives it a Fetch, poll() takes the answer once fetch_done is set. Until then the task has
// the Fetch and touches nothing else.
struct Fetch {
  std::string since;  // If-Modified-Since, "" for none
  int timeout_ms;
  int status = 0;  // 0 = no answer
  Weather w;
  std::string last_modified, expires;
  unsigned stack_left = 0;
};

// api.met.no's answer, 2xx with the forecast read from it into f.w, or 304 if it hasn't changed. 203 is a product
// that's deprecated, its data is still good. Refused, it waits an hour, then twice as long each time up to 12.
inline void fetched(const Fetch &f) {
  answered = true;
  retry_soon = false;
  bool ok = f.status >= 200 && f.status < 300 && f.w.ok;
  if (ok || f.status == 304) {
    backoff_s = 0;
    uint32_t now = now_s(), e = http_date(f.expires);
    not_before = e ? std::max(e, now + 600) : now + 3600;  // at least 10 min, in case our clock is off
  } else {
    back_off(3600, 12 * 3600);  // 429 is too many requests, 403 refused
  }
  if (f.status == 304) {
    ESP_LOGI("met", "The forecast hasn't changed (HTTP 304)");
    return;
  }
  if (f.status == 203) {
    ESP_LOGW("met", "MET Norway says this product is deprecated");
  }
  if (!ok) {
    ESP_LOGW("met", "No forecast from MET Norway (HTTP %d)", f.status);
    on_status("No forecast (HTTP " + std::to_string(f.status) + ")");
    return;
  }
  const Weather &w = f.w;
  weather = w;
  if (!f.last_modified.empty())
    modified = f.last_modified;
  char s[96];
  snprintf(s, sizeof s, "%s, %.1f °C, clouds %.0f %%, %.1f mm", w.symbol[0] ? w.symbol : "?", w.temp, w.cloud, w.rain);
  ESP_LOGI("met", "Weather: %s", s);
  on_status(s);
  redraw();
}

inline Fetch *fetching;
inline std::atomic<bool> fetch_done;

inline esp_err_t on_header(esp_http_client_event_t *e) {
  Fetch &f = *(Fetch *) e->user_data;
  if (e->event_id == HTTP_EVENT_ON_HEADER) {
    if (!strcasecmp(e->header_key, "last-modified"))
      f.last_modified = e->header_value;
    else if (!strcasecmp(e->header_key, "expires"))
      f.expires = e->header_value;
  }
  return ESP_OK;
}

// The answer, an hour at a time until there are HOURS of them. Not inlined, so its buffer takes the stack the
// handshake had. False if the connection failed or the answer was cut short.
__attribute__((noinline)) inline bool read_answer(esp_http_client_handle_t c, Weather &w) {
  char buf[Reader::SIZE + 1];
  Reader rd(buf);
  int r;
  bool more;
  do {
    size_t room;
    char *p = rd.space(room);
    r = esp_http_client_read(c, p, (int) room);
    more = r <= 0 || rd.got((size_t) r);
  } while (r > 0 && more);
  w = rd.w;
  return !more || (r == 0 && esp_http_client_is_complete_data_received(c));
}

inline void request(void *arg) {
  Fetch &f = *(Fetch *) arg;
  esp_http_client_config_t config = {};
  config.url = url;
  config.user_agent = USER_AGENT;
  config.timeout_ms = f.timeout_ms;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.event_handler = on_header;
  config.user_data = &f;
  if (esp_http_client_handle_t c = esp_http_client_init(&config)) {
    if (!f.since.empty())
      esp_http_client_set_header(c, "If-Modified-Since", f.since.c_str());
    for (int hops = 0; esp_http_client_open(c, 0) == ESP_OK && esp_http_client_fetch_headers(c) >= 0; hops++) {
      f.status = esp_http_client_get_status_code(c);
      bool moved = f.status == 301 || f.status == 302 || f.status == 307 || f.status == 308;
      if (!moved || hops == 3 || esp_http_client_set_redirection(c) != ESP_OK)
        break;
    }
    if (f.status >= 200 && f.status < 300 && !read_answer(c, f.w))
      f.status = 0;              // counts as no answer, its Last-Modified isn't kept
    esp_http_client_cleanup(c);  // TLS's buffers go with it
  }
  f.stack_left = uxTaskGetStackHighWaterMark(nullptr);
  fetch_done.store(true);  // f is the loop's from here on
  vTaskDelete(nullptr);
}

// Asks for the forecast, with If-Modified-Since once we have a Last-Modified
inline void fetch() {
  if (fetching)
    return;  // the last one still runs
  RAMAllocator<Fetch> ram;
  Fetch *f = ram.allocate(1);
  if (f) {
    new (f) Fetch();
    f->since = modified;
    f->timeout_ms = timeout_ms;
    fetch_done.store(false);
    fetching = f;
    if (xTaskCreate(request, "met", STACK, f, 1, nullptr) == pdPASS)
      return;
    fetching = nullptr;
    f->~Fetch();
    ram.deallocate(f, 1);
  }
  ESP_LOGW("met", "No RAM for the forecast, %u bytes", (unsigned) (sizeof(Fetch) + STACK));
  not_before = now_s() + 600;
  on_status("No RAM for the forecast");
}

// The answer, once the request's task is done with it
inline void poll() {
  if (!fetching || !fetch_done.load())
    return;
  Fetch *f = fetching;
  fetching = nullptr;
  ESP_LOGD("met", "The request left %u bytes of its stack", f->stack_left);
  if (f->status)
    fetched(*f);
  else
    unreachable();
  f->~Fetch();
  RAMAllocator<Fetch>().deallocate(f, 1);
}

// Every second from met.yaml: the answer once the task has it, and an early retry when it's time
inline void tick() {
  poll();
  if (retry_soon && !fetching && due()) {
    retry_soon = false;
    fetch();
  }
}

// From met.yaml's on_boot, with http_request's timeout
inline void setup(double latitude, double longitude, const char *picture, const char *encoding, const char *quality,
                  uint32_t http_timeout_ms) {
  lat = latitude;
  lon = longitude;
  timeout_ms = (int) http_timeout_ms;
  // MET Norway wants at most 4 decimals, more can get a 403
  snprintf(url, sizeof url, "https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=%.4f&lon=%.4f", lat, lon);
  name = picture;
  now_item = Item("weather", ITEM_SENSOR)
                 .view(false)
                 .menu(false)
                 .icon(KINDS[I_NONE].icon)
                 .label(KINDS[I_NONE].name)
                 .unit("°C")
                 .decimals(1)
                 .range(-50, 50)
                 .add();
  picture::Source s;
  s.size = [](int &w, int &h) {
    w = W;
    h = H;
    return draw();
  };
  s.prepare = prepare;
  s.pixel = pixel;
  s.pal = pal;
  s.row = [](int y, uint8_t *idx) {
    if (drawing)
      row(drawing->scene, y, idx);
  };
  s.done = free_drawing;
  s.encoding = picture::encoding_of(encoding);
  s.quality = picture::quality_of(quality);
  picture::add(name, s);
}

}  // namespace flexit::panel::met
