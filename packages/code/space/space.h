// What the space pictures share: a list asked for every few hours through fetch.h and read as it streams, the photo
// of the day it names, and the Mars calendar the rovers keep
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <new>
#include <string>
#include "../marks/marks.h"
#include "../panel/fetch.h"
#include "../panel/json_scan.h"
#include "../panel/picture_online.h"

namespace flexit::panel::space {

// A request whose JSON answer goes to value() and end() as it streams
struct Ask : fetch::Request, JsonScan {
  bool feed(const char *p, size_t n) override { return scan(p, n); }
};

// From a package's setup: a asked every `every`, like 6h, at least every least_s, with status saying how it went
template<typename S> void ask_every(fetch::Asker &a, const char *every, S &&status, uint32_t least_s = 600) {
  a.every_s = std::max<uint32_t>(picture::interval_ms(every) / 1000, least_s);
  a.status = ptr_of(status);
  fetch::add(a);
}

// A photo of the day: online_picture.yaml's picture, downloaded from the URL the package's list names, and drawn by
// the package's pixel() from at()
struct Today {
  std::string name, url;
  std::function<void(const std::string &)> set_url;  // online_url() for the yaml's image
  std::function<uint32_t(int, int)> px;              // the image's
  std::function<void(int &, int &)> dims;            // what it has
  int lo = 0, hi = 255;                              // its levels, stretched to 0..255
  bool measured = false;                             // measure() has had this download

  void show(const std::string &u) {
    url = u;
    measured = false;
    set_url(u);
  }
  // The photo w x h at fx, fy in its pixels, smoothly between them, black off it
  uint32_t at(float fx, float fy, int w, int h) const {
    if (w <= 0 || h <= 0 || fx < -0.5f || fy < -0.5f || fx > w - 0.5f || fy > h - 0.5f)
      return 0;
    int x0 = std::clamp((int) floorf(fx), 0, w - 1), y0 = std::clamp((int) floorf(fy), 0, h - 1);
    int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    float ax = std::clamp(fx - x0, 0.0f, 1.0f), ay = std::clamp(fy - y0, 0.0f, 1.0f);
    uint32_t c00 = px(x0, y0), c10 = px(x1, y0), c01 = px(x0, y1), c11 = px(x1, y1), out = 0;
    for (int s = 0; s < 24; s += 8) {
      float top = (c00 >> s & 255) * (1 - ax) + (c10 >> s & 255) * ax;
      float bot = (c01 >> s & 255) * (1 - ax) + (c11 >> s & 255) * ax;
      float v = ((top * (1 - ay) + bot * ay) - lo) * 255 / (hi - lo);
      out |= (uint32_t) std::clamp((int) lroundf(v), 0, 255) << s;
    }
    return out;
  }
};

// From a package's setup, after online_picture.yaml's: picture name is t's photo, drawn by pixel(). measure(w, h)
// once a download is in, if there's one to call.
template<typename Img>
void today(Today &t, const std::string &name, Img &&image, uint32_t (*pixel)(int, int),
           void (*measure)(int w, int h) = nullptr) {
  t.name = name;
  auto *img = ptr_of(image);
  t.set_url = [&t, img](const std::string &u) { picture::online_url(t.name, img, u); };
  t.dims = [img](int &w, int &h) {
    w = img->get_width();
    h = img->get_height();
  };
  auto it = picture::sources.find(name);
  if (it == picture::sources.end())
    return;
  picture::Source &s = it->second;
  t.px = s.pixel;
  auto size = s.size;  // online_picture's, which downloads it if it has to
  s.size = [&t, size, measure](int &w, int &h) {
    if (t.url.empty()) {
      if (picture::S.want == t.name)
        picture::S.waiting = "looking for the newest photo";
      return false;
    }
    if (!size(w, h))
      return false;
    if (measure && !t.measured)
      measure(w, h);
    t.measured = true;
    w = marks::W;
    h = marks::H;
    return true;
  };
  s.pixel = pixel;
  auto done = s.done;
  s.done = [&t, done]() {
    t.measured = false;  // its pixels go, the next download's are measured again
    if (done)
      done();
  };
  marks::on(name);
}

// "1 Nov · 23:41" in local time
inline std::string when(uint32_t t) {
  ESPTime l = ESPTime::from_epoch_local((time_t) t);
  return marks::day(l.day_of_month, l.month) + " · " + marks::clock(l.hour, l.minute);
}
inline std::string day_of(uint32_t t) {
  ESPTime l = ESPTime::from_epoch_local((time_t) t);
  return marks::day(l.day_of_month, l.month);
}

// A rover's calendar: the Mars sol date it landed on and the longitude its sols are counted at
struct Landing {
  double msd, lon;
};
// When sol began on Earth, Unix time
inline uint32_t sol_start(int sol, const Landing &l) {
  double m = floor(l.msd + l.lon / 360) + sol - l.lon / 360;
  return (uint32_t) llround((m * 1.0274912517 + 2405522.0028779 - 69.184 / 86400 - 2440587.5) * 86400);
}

}  // namespace flexit::panel::space
