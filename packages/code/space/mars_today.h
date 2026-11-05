// mars_today.yaml: the newest colour photo Perseverance has sent back, from NASA's raw images. The list of the
// newest is asked for once a day and read as it streams. The photo looking out nearest the horizon of the newest sol
// is downloaded as an online picture when it's shown, filling the screen, its levels stretched as the raw frames
// are dim. Its sol, Mars's time when it was taken and its date go over it as marks.
// Raw images: NASA/JPL-Caltech/ASU.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include "../panel/picture_online.h"
#include "space.h"

namespace flexit::panel::space::mars {

inline constexpr const char *LIST =
    "https://mars.nasa.gov/rss/api/?feed=raw_images&category=mars2020&feedtype=json&num=50&page=0&order=sol+desc"
    "&extended=sample_type::full,";

struct Photo {
  std::string url, camera;  // its 320 px JPEG, and the camera's name for us
  int sol = -1;
  std::string on_mars;  // "13:10", local mean solar time where it was
  uint32_t taken = 0;   // UTC
  float elevation = -90;
};

// The camera's name for us, "" for one that doesn't take colour photos of the view. Mastcam-Z's _RGB_ND are through
// its sun filters.
inline const char *camera_of(const std::string &instrument, const std::string &filter) {
  if (!instrument.compare(0, 4, "MCZ_"))
    return filter.size() > 4 && !filter.compare(filter.size() - 4, 4, "_RGB") ? "Mastcam-Z" : "";
  if (!instrument.compare(0, 6, "NAVCAM"))
    return "Navcam";
  return "";  // the Hazcams look at the ground by the wheels, the rest at rocks or the sky
}

// Of two photos, the one to show: the newest sol, then nearest the horizon, then the latest
inline bool better(const Photo &a, const Photo &b) {
  if (a.sol != b.sol)
    return a.sol > b.sol;
  float da = fabsf(a.elevation + 5), db = fabsf(b.elevation + 5);  // a little below level shows the ground and sky
  if (fabsf(da - db) > 5)
    return da < db;
  return a.taken > b.taken;
}

// The list's photos as they come, the best kept
struct List : Ask {
  Photo now, best;
  std::string instrument, filter;

  void value(int depth, const char *key, int, const char *v) override {
    if (depth == 3 && !strcmp(key, "sol"))
      now.sol = atoi(v);
    else if (depth == 3 && !strcmp(key, "date_taken_utc"))
      now.taken = utc_of(v);
    else if (depth == 3 && !strcmp(key, "date_taken_mars") && strchr(v, 'M') && strlen(strchr(v, 'M')) >= 6)
      now.on_mars = std::string(strchr(v, 'M') + 1, 5);  // "Sol-02024M13:10:59.198"
    else if (depth == 4 && !strcmp(key, "instrument"))
      instrument = v;
    else if (depth == 4 && !strcmp(key, "filter_name"))
      filter = v;
    else if (depth == 4 && !strcmp(key, "small"))
      now.url = v;
    else if (depth == 4 && !strcmp(key, "mastEl") && (isdigit((unsigned char) *v) || *v == '-'))
      now.elevation = (float) atof(v);  // "UNK" when it's not known
  }
  void end(int depth, const char *key) override {
    if (depth != 3 || strcmp(key, "images"))
      return;
    now.camera = camera_of(instrument, filter);
    if (!now.camera.empty() && !now.url.empty() && now.sol >= 0 && (best.sol < 0 || better(now, best)))
      best = now;
    now = Photo();
    instrument.clear();
    filter.clear();
  }
};

// NASA's list of the newest
struct Photos : fetch::Asker {
  fetch::Request *make(int) override {
    auto *r = new (std::nothrow) List();
    if (r)
      r->url = LIST;
    return r;
  }
  bool got(fetch::Request &r) override;
};

inline Photos list;
inline Photo photo;  // shown, or to be once downloaded
inline Today today;

inline marks::Layer layer_of(const Photo &p) {
  marks::Layer l;
  l.scrim('t', 52, 0.6f);
  l.scrim('b', 70, 0.75f);
  l.text(12, 20, "Mars today", marks::TITLE);
  l.text(12, 32, "Jezero crater, by Perseverance's " + p.camera, marks::SMALL, marks::WHITE, 0.85f);
  l.text(12, marks::H - 26, "Sol " + std::to_string(p.sol), marks::BIG);
  l.caps(12, marks::H - 14, day_of(p.taken) + " · " + p.on_mars + " on Mars");
  l.caps(marks::W - 12, marks::H - 14, "NASA/JPL-Caltech", 0.4f, marks::WHITE, 'r');
  return l;
}

inline bool Photos::got(fetch::Request &q) {
  const List &r = static_cast<List &>(q);
  if (r.best.sol < 0)
    return false;
  char s[96];
  snprintf(s, sizeof s, "Sol %d, %s, %s", r.best.sol, r.best.camera.c_str(), when(r.best.taken).c_str());
  say(s);
  if (r.best.url == photo.url)
    return true;
  ESP_LOGI("space", "Mars today: %s", r.best.url.c_str());
  photo = r.best;
  today.show(photo.url);
  marks::set(today.name, layer_of(photo));
  return true;
}

// The photo's levels: the luminance's 1st and 99.6th percentile, so a dim raw frame fills the range
inline void level(int w, int h) {
  uint16_t hist[64] = {};
  for (int y = 0; y < h; y += 2)
    for (int x = 0; x < w; x += 2) {
      uint32_t c = today.px(x, y);
      hist[((c >> 16 & 255) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 10 / 4]++;
    }
  int n = ((w + 1) / 2) * ((h + 1) / 2), sum = 0, lo = 0, hi = 255;
  for (int i = 0; i < 64; i++) {
    sum += hist[i];
    if (sum * 100 < n)
      lo = i * 4;
    if (sum * 1000 <= n * 996)
      hi = i * 4 + 4;
  }
  today.lo = lo;
  today.hi = std::max(hi, lo + 64);
}

// The photo scaled to fill the screen, cut at its sides as it needs
inline uint32_t pixel(int x, int y) {
  int w, h;
  today.dims(w, h);
  float k = std::max((float) marks::W / w, (float) marks::H / h);
  float fx = (x + 0.5f - marks::W / 2.0f) / k + w / 2.0f - 0.5f;
  float fy = (y + 0.5f - marks::H / 2.0f) / k + h / 2.0f - 0.5f;
  return today.at(fx, fy, w, h);
}

// From mars_today.yaml's on_boot, after online_picture.yaml's. every: how often to look for a newer photo, status:
// its sensor.
template<typename Img, typename S> void setup(const std::string &name, Img &&image, const char *every, S &&status) {
  list.what = "photo";
  list.server = "NASA";
  ask_every(list, every, status);
  space::today(today, name, image, pixel, level);
}

}  // namespace flexit::panel::space::mars
