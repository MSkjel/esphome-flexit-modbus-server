// met_scene.h as a panel picture. met.yaml calls redraw() every few minutes so the sun and moon move. Only changed
// rows are sent to the panel. MET Norway is asked for the forecast through fetch.h, again once its last answer has
// expired, with If-Modified-Since, soon again after a server error and less often while it refuses or doesn't answer,
// see Forecasts::got() and fetch::Schedule. The forecast is kept through all of that until it runs out. NOAA is asked
// for the Kp every 3 hours while the aurora is on, see KpIndex.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "../panel/fetch.h"
#include "../panel/items.h"
#include "../panel/picture.h"
#include "met_kp.h"
#include "met_live_shows.h"
#include "met_raster.h"

// True in fireplace mode. Weak, since packages/code/fireplace.h only defines it when that package is included.
extern "C" bool flexit_fireplace_active() __attribute__((weak));

namespace flexit::panel::met {

inline Weather weather;  // the forecast, from the coming hour on
inline Sea sea;          // the waves, from Oceanforecast. Not ok inland, where MET Norway has none

// The dashboard tile's label and icon for each Icon
struct Kind {
  const char *name, *icon;
};
inline constexpr Kind KINDS[] = {{"Weather|Vær", "cloud"},   {"Clear|Klart", "sun"},      {"Clear|Klart", "night"},
                                 {"Fair|Lettskyet", "sun"},  {"Fair|Lettskyet", "night"}, {"Cloudy|Skyet", "cloud"},
                                 {"Rain|Regn", "rain"},      {"Snow|Snø", "snowy"},       {"Sleet|Sludd", "rain"},
                                 {"Thunder|Torden", "bolt"}, {"Fog|Tåke", "cloud"}};
static_assert(std::size(KINDS) == I_FOG + 1, "a name and an icon for each of met_forecast.h's Icons");
inline int now_item = -1;
inline double lat, lon;
inline std::string name;
inline bool with_forecast;    // the Panel Weather Forecast switch
inline Options options;       // set from met.yaml
inline uint32_t pal[16];      // the scene's colours, for picture.h
inline picture::Parts parts;  // and its parts

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

// A request for the forecast, and what's read from its answer
struct Ask : fetch::Request {
  Forecast f;
  bool feed(const char *p, size_t n) override { return f.feed(p, n); }
};

// MET Norway, asked through fetch.h. Panel Weather is its status.
struct Forecasts : fetch::Asker {
  std::string modified;  // Last-Modified of our forecast, for If-Modified-Since. Empty until we have one
  char url[128];
  fetch::Request *make(int) override;
  bool got(fetch::Request &r) override;
};
inline Forecasts forecasts;

// A request for the Kp, and what's read from its answer
struct KpAsk : fetch::Request {
  KpReader read;
  bool feed(const char *p, size_t n) override { return read.feed(p, n); }
};

// NOAA's Kp, asked for while the aurora is on auto or always. Panel Weather Kp is its status.
struct KpIndex : fetch::Asker {
  fetch::Request *make(int) override { return fetch::request<KpAsk>(KP_URL); }
  bool got(fetch::Request &r) override;
  bool wanted() override { return options.aurora != A_OFF; }
};
inline KpIndex kp_index;
inline KpBlocks kp;  // its last answer's

// A request for the waves, and what's read from its answer
struct SeaAsk : fetch::Request {
  Waves f;
  bool feed(const char *p, size_t n) override { return f.feed(p, n); }
};

// MET Norway's Oceanforecast, asked through fetch.h after the forecast. Inland it has no waves, a 422, and is asked
// again the next day. No status of its own, Panel Weather is the forecast's.
struct Seas : fetch::Asker {
  std::string modified;
  char url[128];
  fetch::Request *make(int) override;
  bool got(fetch::Request &r) override;
  void answered(fetch::Request *r) override;
  void none(int status);
  bool wanted() override { return options.ground == G_COAST && with_forecast; }  // only the coast shows the waves
};
inline Seas seas;

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
  uint32_t t = fetch::now_s();
  if (t >= ends(weather)) {
    weather.ok = false;
    forecasts.modified.clear();  // so the next answer is all of it
    forecasts.have = false;
    tile(I_NONE, NAN);
    forecasts.say("The forecast has run out");
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

// The scene, only while picture.h makes the picture. Its stamps and overlay are blocks of their own, the land is
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
    RAMAllocator<Stamps>().deallocate(drawing->scene.stamps, 1);
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
  uint8_t at = picture::room_for(sizeof(Drawing) + sizeof(Stamps) + ov_bytes + (land ? 0 : sizeof(Land)),
                                 sizeof(Drawing));  // internal RAM, PSRAM when that's short
  if (!at)
    return false;
  bool new_land = !land;
  if (new_land && (land = RAMAllocator<Land>(at).allocate(1)))
    land->made = false;
  Drawing *c = RAMAllocator<Drawing>(at).allocate(1);
  Stamps *stamps = RAMAllocator<Stamps>(at).allocate(1);
  uint8_t *ov = ov_bytes ? RAMAllocator<uint8_t>(at).allocate(ov_bytes) : nullptr;
  if (!land || !c || !stamps || (ov_bytes && !ov)) {
    RAMAllocator<Drawing>().deallocate(c, 1);
    RAMAllocator<Stamps>().deallocate(stamps, 1);
    RAMAllocator<uint8_t>().deallocate(ov, ov_bytes);
    if (new_land) {
      RAMAllocator<Land>().deallocate(land, 1);
      land = nullptr;
    }
    return false;
  }
  c->scene.stamps = stamps;
  c->scene.ov = (uint8_t (*)[W / 4]) ov;
  c->scene.land = land;
  drawing = c;
  return true;
}

// Builds the scene for now and starts counting its colours. False without the time or the RAM.
inline bool begin() {
  ESPTime t = id(flexit_panel_time).now();
  if (!t.is_valid()) {
    picture::S.waiting = "waiting for the time from Home Assistant";
    return false;
  }
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
  options.kp = kp.at((uint32_t) t.timestamp);
  Scene &s = drawing->scene;
  weather.wave = wave_at(sea, (uint32_t) t.timestamp);
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
  parts = parts_of(s);
  drawing->row_y = -1;
  counting = false;
  built = true;
}

inline constexpr int COUNT_ROWS = 8;    // rows prepare() counts between looks at the time
inline uint32_t begin_us = UINT32_MAX;  // what begin() took, as fits() has it

// picture.h's prepare: the scene for now built, then its colours counted a few rows at a time until deadline_us.
// True once there's nothing more to do now, built or not to be built yet, which draw() then says.
inline bool prepare(uint32_t deadline_us) {
  if (built)
    return true;
  if (!counting) {
    if (!fits(deadline_us, begin_us, true))
      return false;
    uint32_t t0 = micros();
    if (!begin())
      return true;
    begin_us = micros() - t0;
  }
  while (before(deadline_us))
    if (drawing->colours.count(drawing->scene, COUNT_ROWS)) {
      finish();
      return true;
    }
  return false;
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

// With If-Modified-Since once we have a Last-Modified
inline fetch::Request *Forecasts::make(int) {
  auto *r = fetch::request<Ask>(url);
  if (r) {
    r->header = "If-Modified-Since";
    r->value = modified;
  }
  return r;
}

// api.met.no's answer, 2xx with the forecast read from it, or 304 if it hasn't changed. 203 is a product that's
// deprecated, its data is still good. A 2xx without a forecast in it counts as a server error. Asked again at least
// 10 min after Expires, in case our clock is off, or in an hour without one.
inline bool Forecasts::got(fetch::Request &r) {
  uint32_t now = fetch::now_s(), e = fetch::http_date(r.expires);
  every_s = e ? std::max(e, now + 600) - now : 3600;
  if (r.status == 304) {
    ESP_LOGI("met", "The forecast hasn't changed (HTTP 304)");
    return true;
  }
  if (r.status == 203) {  // braces, as ESP_LOGW can be nothing
    ESP_LOGW("met", "MET Norway says this product is deprecated");
  }
  const Weather &w = static_cast<Ask &>(r).f.w;
  if (!w.ok)
    return false;
  weather = w;
  if (!r.last_modified.empty())
    modified = r.last_modified;
  char s[96];
  snprintf(s, sizeof s, "%s, %.1f °C, clouds %.0f %%, %.1f mm", w.symbol[0] ? w.symbol : "?", w.temp, w.cloud, w.rain);
  ESP_LOGI("met", "Weather: %s", s);
  say(s);
  redraw();
  return true;
}

// NOAA's answer, with a Kp from now on in it. The aurora's redrawn by it.
inline bool KpIndex::got(fetch::Request &r) {
  const KpBlocks &k = static_cast<KpAsk &>(r).read.blocks;
  uint32_t now = fetch::now_s();
  if (!k.n || k.b[k.n - 1].t + KP_BLOCK_S <= now)
    return false;
  kp = k;
  char s[64];
  snprintf(s, sizeof s, "Kp %.1f now, the aurora needs %.1f here", kp.at(now), kp_needed(lat, lon));
  ESP_LOGI("met", "%s", s);
  say(s);
  redraw();
  return true;
}

inline fetch::Request *Seas::make(int) {
  auto *r = new (std::nothrow) SeaAsk();
  if (r) {
    r->url = url;
    r->header = "If-Modified-Since";
    r->value = modified;
  }
  return r;
}

// No waves here: none drawn, asked again tomorrow
inline void Seas::none(int status) {
  every_s = 86400;
  modified.clear();
  bool had = sea.ok;
  sea = Sea{};
  if (had)
    redraw();
  ESP_LOGI("met", "No waves here (HTTP %d), MET Norway has them for points at sea", status);
}

// Oceanforecast's answer, as Forecasts::got() has the forecast's. One without waves is inland, see none().
inline bool Seas::got(fetch::Request &r) {
  uint32_t now = fetch::now_s(), e = fetch::http_date(r.expires);
  every_s = e ? std::max(e, now + 600) - now : 3600;
  if (r.status == 304)
    return true;
  const Sea &w = static_cast<SeaAsk &>(r).f.sea;
  if (!w.ok) {
    none(r.status);
    return true;
  }
  sea = w;
  if (!r.last_modified.empty())
    modified = r.last_modified;
  ESP_LOGI("met", "Waves: %.1f m", wave_at(sea, now));
  redraw();
  return true;
}

// A 422 (or 400, 404) is a point inland, not a refusal to back off from
inline void Seas::answered(fetch::Request *r) {
  if (r->status == 422 || r->status == 400 || r->status == 404) {
    asking = nullptr;
    none(r->status);
    schedule.got(*r, true, every_s);
    delete r;
    return;
  }
  fetch::Asker::answered(r);
}

// From met.yaml's on_boot. status: Panel Weather, kp_status: Panel Weather Kp.
template<typename S, typename K>
void setup(double latitude, double longitude, const char *picture, const char *encoding, const char *quality,
           S &&status, K &&kp_status) {
  lat = latitude;
  lon = longitude;
  // MET Norway wants at most 4 decimals, more can get a 403
  snprintf(forecasts.url, sizeof forecasts.url,
           "https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=%.4f&lon=%.4f", lat, lon);
  forecasts.what = "forecast";
  forecasts.server = "MET Norway";
  forecasts.status = ptr_of(status);
  fetch::add(forecasts);
  kp_index.what = "Kp";
  kp_index.server = "NOAA";
  kp_index.every_s = KP_BLOCK_S;
  kp_index.status = ptr_of(kp_status);
  fetch::add(kp_index);
  snprintf(seas.url, sizeof seas.url, "https://api.met.no/weatherapi/oceanforecast/2.0/complete?lat=%.4f&lon=%.4f", lat,
           lon);
  seas.what = "waves";
  seas.server = "MET Norway";
  fetch::add(seas);  // after the forecast, so that's asked first
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
  s.parts = [] { return parts; };
  s.done = free_drawing;
  s.coding(encoding, quality);
  picture::add(name, s);
}

}  // namespace flexit::panel::met
