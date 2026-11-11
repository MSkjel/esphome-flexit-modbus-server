// The coast's wave height: Oceanforecast's answer read as it comes in, the hour wave_at() takes, and the text
// forecast_marks() puts right of the lighthouse above the sea, or doesn't. make -C tests met_coast_wave_test
#include <cstring>
#include <string>
#include "check.h"
#include "met/met_raster.h"

using namespace flexit::panel;
using namespace flexit::panel::met;

static int utc(uint32_t) { return 0; }

static Stamps stamps;
static Land land;
static Scene scene;

// An Oceanforecast hour, as its timeseries has them: h hours into 20 August 2026
static std::string hour(int h, const char *height) {
  char b[160];
  snprintf(b, sizeof b,
           "{\"time\":\"2026-08-%02dT%02d:00:00Z\",\"data\":{\"instant\":{\"details\":{%s\"sea_water_speed\":0.1}}}},",
           20 + h / 24, h % 24, height);
  return b;
}

// What a Waves reader makes of s, handed to it in parts of k bytes
static Sea read_sea(const std::string &s, size_t k) {
  Waves r;
  for (size_t i = 0; i < s.size(); i += k)
    if (!r.feed(s.data() + i, std::min(k, s.size() - i)))
      break;
  return r.sea;
}

// The forecast over the landscape at 15:00 UTC on 20 August 2026 in Bergen, waves m high
static marks::Layer build_at(float waves, bool forecast = true, Ground g = G_COAST) {
  Weather w;
  w.ok = true;
  w.time = unix_of(2026, 8, 20, 15);
  snprintf(w.symbol, sizeof w.symbol, "partlycloudy_day");
  w.temp = 16;
  w.cloud = 50;
  w.wind = 9;
  w.wind_from = 250;
  w.wave = waves;
  for (int i = 0; i < HOURS; i++) {
    Hour h{};
    h.time = w.time + 3600u * i;
    h.temp = 16;
    h.icon = icon_of(w.symbol);
    w.hour[w.hours++] = h;
  }
  Options o;
  o.ground = g;
  scene.stamps = &stamps;
  scene.land = &land;
  build(scene, w, 60.39, 5.32, w.time, forecast, utc, o);
  return forecast_marks(scene, w, w.time, utc);
}

// The wave height: a line of text, the one op more than without, last. Null if there's none.
static const marks::Op *wave_text(const marks::Layer &l, size_t without) {
  const marks::Op *t = l.ops.size() == without + 1 ? &l.ops.back() : nullptr;
  return t && t->kind == marks::Op::BITMAP && t->rows.size() == 7 && t->outline ? t : nullptr;
}

// How many of t's pixels are drawn in its colour, and in which columns and rows
static int drawn(const marks::Op &t, int &l, int &r, int &top, int &bottom) {
  marks::Layer only;
  only.ops = {t};
  static marks::Sharp sharp;
  int n = 0;
  l = top = 9999;
  r = bottom = -1;
  for (int y0 = 0; y0 < H; y0 += marks::SHARP_BAND) {
    marks::draw_sharp(only, sharp, y0);
    for (int y = y0; y < std::min(H, y0 + marks::SHARP_BAND); y++)
      for (int x = 0; x < W; x++)
        if (sharp.px[(y - y0) * W + x] == (0xFF000000 | t.rgb)) {
          n++;
          l = std::min(l, x);
          r = std::max(r, x);
          top = std::min(top, y);
          bottom = std::max(bottom, y);
        }
  }
  return n;
}

int main() {
  // Oceanforecast's hours, in parts that cut the keys in two too. The last hour is read once the next one starts,
  // so it waits for one more, and an hour without the height is skipped.
  std::string answer = "{\"type\":\"Feature\",\"properties\":{\"timeseries\":[";
  answer += hour(14, "\"sea_surface_wave_height\":1.2,") + hour(15, "\"sea_surface_wave_height\":1.8,") + hour(16, "") +
            hour(17, "\"sea_surface_wave_height\":-0.3,") + hour(18, "\"sea_surface_wave_height\":2.0,");
  answer += "{\"time\":\"2026-08-20T19:00:00Z\"}]}}";
  for (size_t k : {answer.size(), (size_t) 1, (size_t) 7, (size_t) 100}) {
    Sea sea = read_sea(answer, k);
    CHECK(sea.ok);
    CHECK(sea.hours == 4);
    CHECK(sea.time[0] == unix_of(2026, 8, 20, 14) && sea.height[0] == 1.2f);
    CHECK(sea.time[1] == unix_of(2026, 8, 20, 15) && sea.height[1] == 1.8f);
    CHECK(sea.time[2] == unix_of(2026, 8, 20, 17) && sea.height[2] == 0);  // never below 0
    CHECK(sea.time[3] == unix_of(2026, 8, 20, 18) && sea.height[3] == 2.0f);
  }
  CHECK(!read_sea("{\"type\":\"Feature\",\"properties\":{\"timeseries\":[]}}", 16).ok);  // inland
  // More hours than it keeps, several of them in a piece as the ESP gets them, or all at once: the first SEA_HOURS,
  // and one that isn't a number is no hour
  std::string many = "{\"type\":\"Feature\",\"properties\":{\"timeseries\":[";
  many += hour(0, "\"sea_surface_wave_height\":null,");
  for (int h = 1; h < SEA_HOURS + 9; h++)
    many += hour(h, "\"sea_surface_wave_height\":1.5,");
  many += "{\"time\":\"2026-08-22T00:00:00Z\"}]}}";
  for (size_t k : {many.size(), (size_t) 512, (size_t) 3}) {
    Sea sea = read_sea(many, k);
    CHECK(sea.ok && sea.hours == SEA_HOURS);
    CHECK(sea.time[0] == unix_of(2026, 8, 20, 1) && sea.time[SEA_HOURS - 1] == unix_of(2026, 8, 21, 0));
    CHECK(sea.height[0] == 1.5f && sea.height[SEA_HOURS - 1] == 1.5f);
  }

  // The hour nearest t, if t's within three hours of it
  Sea sea = read_sea(answer, answer.size());
  uint32_t t15 = unix_of(2026, 8, 20, 15);
  CHECK(wave_at(sea, t15 + 1800) == 1.8f);
  CHECK(wave_at(sea, t15 + 2400) == 1.8f);
  CHECK(wave_at(sea, t15 - 600) == 1.2f);
  CHECK(wave_at(sea, unix_of(2026, 8, 20, 22)) == 2.0f);  // 3 hours after the 18:00 hour ends
  CHECK(wave_at(sea, unix_of(2026, 8, 20, 22) + 1) == -1);
  CHECK(wave_at(sea, unix_of(2026, 8, 20, 11)) == 1.2f);  // and before the 14:00 one starts
  CHECK(wave_at(sea, unix_of(2026, 8, 20, 11) - 1) == -1);
  CHECK(wave_at(Sea{}, t15) == -1);

  // On the coast, with the forecast, right of the lighthouse and above the sea, in the forecast's text and
  // outline. One decimal under 10 m, none from there: "10m" and "12m" are as wide, "1.8m", "0.1m" and "9.9m" wider.
  size_t without = build_at(-1).ops.size();
  int widths[5];
  uint64_t middle[5];
  float ms[] = {1.8f, 0.06f, 9.94f, 9.95f, 12.4f};
  for (int i = 0; i < 5; i++) {
    marks::Layer f = build_at(ms[i]);
    const marks::Op *t = wave_text(f, without);
    CHECK(t);
    if (!t)
      continue;
    int l, r, top, bottom;
    CHECK(drawn(*t, l, r, top, bottom) > 20);
    CHECK(l > LH_X + 4 && r < W);                     // clear of the lighthouse, on the screen
    CHECK(bottom < scene.sea_top - 1 && top > OV_H);  // above the sea, under the forecast
    widths[i] = t->cols;
    middle[i] = t->rows[3];
    for (int j = 0; j < i; j++)
      CHECK(middle[i] != middle[j] || widths[i] != widths[j]);
  }
  CHECK(widths[0] == widths[1] && widths[1] == widths[2]);
  CHECK(widths[3] == widths[4] && widths[3] < widths[0]);

  // None at 0.0 m, as in the fjords, inland, where Oceanforecast has no waves, without the forecast, and on the
  // other landscapes
  for (float m : {0.0f, 0.04f})
    CHECK(build_at(m).ops.size() == without);
  CHECK(build_at(1.8f, false).ops.empty());
  for (Ground g : {G_HILLS, G_MOUNTAINS, G_CITY})
    CHECK(build_at(1.8f, true, g).ops.size() == build_at(-1, true, g).ops.size());

  printf("%s\n", fails ? "FAILED" : "met_coast_wave_test: all passed");
  return fails != 0;
}
