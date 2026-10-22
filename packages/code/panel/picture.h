// Screensaver pictures from picture.yaml, online_picture.yaml and met.h. They go to the panel as 16 colours with
// each row packed on its own, or as a JPEG at the best quality that fits the panel's 14 KB.
#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "panel_link.h"
#include "pjpeg.h"
#include "plink.h"

namespace flexit::panel::anim {
bool sending();  // in animation.h, true while an animation is being sent
}

namespace flexit::panel::picture {

// The panel takes one picture or animation at a time. Each transfer gets a new id to match the acks.
inline uint8_t transfer_ids;

static constexpr uint32_t POOL = PIC_POOL;  // the panel's picture memory
static constexpr uint32_t SLICE_US = 5000;  // max conversion time per loop, so the main loop keeps running
static constexpr uint32_t ACK_MS = 10000;   // the panel answers END within a second or so
static constexpr int SCREEN_W = 320, SCREEN_H = 240;

enum Encoding { AUTO, PALETTE, JPEG };

// A picture as the packages register it
struct Source {
  std::function<bool(int &w, int &h)> size;  // false while it isn't ready, like an online one still downloading
  std::function<uint32_t(int, int)> pixel;   // 0xRRGGBB
  std::function<void()> done;                // optional, its pixels aren't needed now
  Encoding encoding = AUTO;
  int quality = 0;  // highest JPEG quality 1..100, 0 = the best that fits
};
inline std::map<std::string, Source> sources;

inline void add(const std::string &name, Source s) { sources[name] = std::move(s); }
// encoding and quality as written in picture.yaml
inline Encoding encoding_of(const std::string &s) { return s == "palette" ? PALETTE : s == "jpeg" ? JPEG : AUTO; }
inline int quality_of(const std::string &s) {
  int q = atoi(s.c_str());
  return q < 1 ? 0 : std::min(q, 100);
}

// id() is a pointer in ESPHome's lambdas and a reference in the sim's harness
template<typename T> T *ptr_of(T *p) { return p; }
template<typename T> T *ptr_of(T &r) { return &r; }

// An ESPHome image's pixels as 0xRRGGBB
template<typename Img> std::function<uint32_t(int, int)> pixels_of(Img *img) {
  return [img](int x, int y) -> uint32_t {
    auto c = img->get_pixel(x, y);
    return (uint32_t) c.r << 16 | (uint32_t) c.g << 8 | c.b;
  };
}

// A picture from an image file (picture.yaml)
template<typename Img> void add_image(const std::string &name, Img &&image, const char *encoding, const char *quality) {
  auto *img = ptr_of(image);
  Source s;
  s.size = [img](int &w, int &h) {
    w = img->get_width();
    h = img->get_height();
    return true;
  };
  s.pixel = pixels_of(img);
  s.encoding = encoding_of(encoding);
  s.quality = quality_of(quality);
  add(name, s);
}

// A picture from a URL (online_picture.yaml). Downloaded when it's shown, every `every` after that while it's
// shown, and its pixels are freed once it's converted. A failed download is tried again 30 s later, then twice as
// long each time up to 30 min, since each try holds up the main loop until the server answers or http_request
// times out. Before a download the server is asked how big the file is (a request that hangs up after the headers),
// because online_image keeps a JPEG whole and crashes the ESP on one that doesn't fit its memory.
struct Online {
  std::string url;
  std::function<size_t(const std::string &)> length;  // what the server says the file is, 0 = unknown
  std::function<void()> fetch;
  uint32_t every = 0;         // ms between downloads while shown, 0 = only when shown
  uint32_t fetched = 0;       // when the last one started
  uint32_t at = 0, wait = 0;  // when to try again after a failure, and the wait after that one
};
inline std::map<std::string, std::shared_ptr<Online>> online;

// "never", "30s", "10min", "1h" as in the yaml
inline uint32_t interval_ms(const char *s) {
  char *end;
  unsigned long n = strtoul(s, &end, 10);
  return n == 0 ? 0 : n * (!strncmp(end, "ms", 2) ? 1 : *end == 'h' ? 3600000 : *end == 'm' ? 60000 : 1000);
}

inline bool fits(const std::string &name, Online &o);  // below, it needs S

template<typename Img>
void add_online_image(const std::string &name, Img &&image, const char *url, const char *every, const char *encoding,
                      const char *quality, std::function<size_t(const std::string &)> length) {
  auto *img = ptr_of(image);
  auto o = std::make_shared<Online>();
  o->url = url;
  o->every = interval_ms(every);
  o->length = std::move(length);
  o->fetch = [img]() { img->update(); };
  online[name] = o;
  Source s;
  s.size = [img, o, name](int &w, int &h) {  // starts the download if there's nothing yet, we get asked again
    w = img->get_width();
    h = img->get_height();
    if (img->is_decoding())
      return false;
    if (w > 0 && h > 0) {
      o->wait = 0;
      return true;
    }
    if (!o->wait || (int32_t) (millis() - o->at) >= 0) {
      o->wait = o->wait ? std::min<uint32_t>(2 * o->wait, 30 * 60000) : 30000;
      o->at = millis() + o->wait;
      if (fits(name, *o)) {
        o->fetched = millis();
        o->fetch();
      }
    }
    return false;
  };
  s.pixel = pixels_of(img);
  s.done = [img]() {
    if (!img->is_decoding())  // a new download keeps its buffer
      img->release();
  };
  s.encoding = encoding_of(encoding);
  s.quality = quality_of(quality);
  add(name, s);
}

// The URL changed, like ha_picture.yaml's token. The next look at the picture downloads it again right away.
template<typename Img> void online_url(const std::string &name, Img &&image, const std::string &url) {
  auto it = online.find(name);
  if (it == online.end())
    return;
  ptr_of(image)->set_url(url);
  it->second->url = url;
  it->second->wait = 0;
}

// A converted picture. pal and rows are only for 16 colours.
struct Pic {
  uint8_t format = PIC_PALETTE;
  uint16_t w = 0, h = 0;
  int quality = 0;  // JPEG only
  uint32_t pal[16] = {};
  std::vector<uint8_t> data;
  std::vector<uint16_t> rows;  // offset of each row in data, plus the end
  double err = 0;              // mean squared error per pixel, Y + Cb + Cr
};

struct Job {  // one transfer, the whole picture or rows y..y+count-1, with or without the palette
  bool full, pal;
  uint16_t y, count;
};

// start(), count() and map() for 16 colours and jpeg() for the JPEG as needed, choose(), plan(), then send()
enum Phase { IDLE, START, COUNT, MAP, JPEG_TRY, CHOOSE, PLAN, SEND };

inline struct {
  std::string want;     // the picture asked for, "" = none
  bool redo = true;     // want is new or its picture changed. True at first so the status gets published
  std::string waiting;  // why the wanted one isn't ready, for the status
  // shown is what the panel has once it's sent. made is the 16-colour version or the one being sent, jpeg the JPEG.
  Pic shown, made, jpeg;
  std::string name;  // the picture being made or sent
  const Source *src = nullptr;
  // sw, sh is the source size, fw, fh fitted to the screen
  int phase = IDLE, y = 0, sw = 0, sh = 0, fw = 0, fh = 0, tries = 0;
  int colours = 0;     // colours in the picture at 4 bits a channel, 17 = more than 16
  bool fits = false;   // the 16-colour version fits the panel
  int lo = 0, hi = 0;  // JPEG quality, lo = best that fits so far (0 = none yet), hi = highest that may fit
  pjpeg_t enc;         // the JPEG being tried
  std::vector<uint8_t> buf;
  std::vector<uint32_t> lines;  // the 8 source lines the encoder is on
  bool head = false, ended = false;
  bool to_clear = false;                               // K_PIC_CLEAR still to send
  uint8_t id = 0;                                      // transfer id of the piece being sent, to match the acks
  uint32_t retry_at = 0, pos = 0, start = 0, len = 0;  // start, len = the piece in made.data, pos = bytes sent
  uint32_t end_ms = 0;                                 // see no_answer()
  std::vector<uint32_t> hist;                          // pixel count per colour, at 4 bits per channel
  std::vector<Job> jobs;
  uint8_t lut[4096];  // colour at 4 bits per channel -> palette index
} S;

template<typename T> void drop(std::vector<T> &v) { std::vector<T>().swap(v); }  // empty, with its RAM freed

inline void status(const std::string &s) {
  if (id(panel_picture_status).state != s)
    id(panel_picture_status).publish_state(s);
}

// K_PIC_CLEAR also stops an animation on its way, so it waits for that in loop()
inline void clear() {
  S.want.clear();
  S.redo = true;
  S.to_clear = true;
  S.shown = Pic();  // so a show() before the next loop sends all of it
  id(panel_picture).publish_state("None");
}

// For panel_show_picture and the Panel Picture select. An online picture is downloaded again. "" is none.
inline void show(const std::string &name) {
  if (name.empty()) {
    clear();
    return;
  }
  if (!sources.count(name)) {
    ESP_LOGW("panel", "No picture called '%s'", name.c_str());
    return;
  }
  S.want = name;
  S.redo = true;
  if (id(panel_picture).has_option(name))
    id(panel_picture).publish_state(name);
}

// A picture changed: convert it again if it's the one wanted, and only the rows that differ are sent. If not,
// free its RAM.
inline void changed(const std::string &name) {
  auto it = sources.find(name);
  if (name == S.want)
    S.redo = true;
  else if (it != sources.end() && it->second.done)
    it->second.done();
}

// online_picture.yaml's on_error. A failed download frees the picture, so one being made starts over, and its
// size() tries again later.
inline void failed(const std::string &name) {
  if (name != S.want)
    return;
  S.waiting = "couldn't download it, trying again later";
  S.redo = true;
}

inline uint32_t dist(uint32_t a, uint32_t b) {
  int dr = (int) (a >> 16) - (int) (b >> 16), dg = (int) (a >> 8 & 255) - (int) (b >> 8 & 255),
      db = (int) (a & 255) - (int) (b & 255);
  return (uint32_t) (2 * dr * dr + 4 * dg * dg + 3 * db * db);
}

// 0xRRGGBB at 4 bits per channel, the bins of the colour counts, and back
inline uint16_t bin(uint32_t c) { return (uint16_t) ((c >> 20 & 15) << 8 | (c >> 12 & 15) << 4 | (c >> 4 & 15)); }
inline uint32_t bin_rgb(int bin) {
  return (uint32_t) ((bin >> 8) * 17) << 16 | (uint32_t) ((bin >> 4 & 15) * 17) << 8 | (uint32_t) ((bin & 15) * 17);
}

inline int nearest(uint32_t c, const uint32_t *pal, int colours = 16) {
  int best = 0;
  uint32_t bd = UINT32_MAX;
  for (int i = 0; i < colours; i++) {
    uint32_t d = dist(c, pal[i]);
    if (d < bd) {
      bd = d;
      best = i;
    }
  }
  return best;
}

// squared difference in Y, Cb and Cr, same units as pjpeg's error
inline float ycc_dist(uint32_t a, uint32_t b) {
  float dr = (float) (a >> 16) - (float) (b >> 16), dg = (float) (a >> 8 & 255) - (float) (b >> 8 & 255),
        db = (float) (a & 255) - (float) (b & 255);
  float y = 0.299f * dr + 0.587f * dg + 0.114f * db, cb = -0.168736f * dr - 0.331264f * dg + 0.5f * db,
        cr = 0.5f * dr - 0.418688f * dg - 0.081312f * db;
  return y * y + cb * cb + cr * cr;
}

// Pixel as sent, with the source fitted to the screen. Smaller sources are blown up smoothly.
inline uint32_t px(int x, int y) {
  if (S.fw <= S.sw)
    return S.src->pixel(x * S.sw / S.fw, y * S.sh / S.fh);
  int fx = std::max(0, (2 * x + 1) * S.sw * 128 / S.fw - 128), fy = std::max(0, (2 * y + 1) * S.sh * 128 / S.fh - 128);
  int x0 = fx >> 8, y0 = fy >> 8, x1 = std::min(x0 + 1, S.sw - 1), y1 = std::min(y0 + 1, S.sh - 1), ax = fx & 255,
      ay = fy & 255;
  uint32_t c00 = S.src->pixel(x0, y0), c10 = S.src->pixel(x1, y0), c01 = S.src->pixel(x0, y1),
           c11 = S.src->pixel(x1, y1), out = 0;
  for (int s = 0; s < 24; s += 8) {
    int top = (int) (c00 >> s & 255) * (256 - ax) + (int) (c10 >> s & 255) * ax,
        bot = (int) (c01 >> s & 255) * (256 - ax) + (int) (c11 >> s & 255) * ax;
    out |= (uint32_t) ((top * (256 - ay) + bot * ay) >> 16) << s;
  }
  return out;
}

// Appends a row packed with the panel's own PackBits
inline void pack(const uint8_t *row, size_t n, std::vector<uint8_t> &data) {
  size_t at = data.size();
  data.resize(at + n + n / 128 + 1);
  data.resize(at + plink_pack_row(row, (unsigned) n, data.data() + at));
}

// Drops what was made for the last request
inline void reset() {
  S.made = Pic();
  S.jpeg = Pic();
  drop(S.hist);
  drop(S.buf);
  drop(S.lines);
  S.jobs.clear();
  S.head = S.ended = false;
  S.phase = IDLE;
}

// Picks up a new request. False if there's nothing to do now.
inline bool ask() {
  if (!S.redo)
    return true;
  S.redo = false;
  if (S.phase == SEND)
    S.shown = Pic();  // some pieces may be in, so the panel's picture is neither the old nor the new one
  reset();
  if (S.want.empty()) {
    S.shown = Pic();
    status("None");
    return false;
  }
  auto it = sources.find(S.want);
  if (it == sources.end()) {
    status(S.want + ": no picture with that name");
    return false;
  }
  S.src = &it->second;
  S.name = S.want;
  S.waiting = "waiting for it";
  S.tries = 0;
  S.retry_at = millis();
  S.phase = START;
  return true;
}

inline void jpeg_start();

// False while it isn't ready, like an online picture still downloading
inline bool start() {
  if ((int32_t) (millis() - S.retry_at) < 0)
    return false;
  if (!S.src->size(S.sw, S.sh) || S.sw <= 0 || S.sh <= 0) {
    status(S.name + ": " + S.waiting);
    S.retry_at = millis() + 1000;
    return false;
  }
  // fitted to the screen, keeping its shape
  if (S.sw * SCREEN_H >= S.sh * SCREEN_W) {
    S.fw = SCREEN_W;
    S.fh = std::max(1, S.sh * SCREEN_W / S.sw);
  } else {
    S.fh = SCREEN_H;
    S.fw = std::max(1, S.sw * SCREEN_H / S.sh);
  }
  S.fits = false;
  S.made.w = (uint16_t) S.fw;
  S.made.h = (uint16_t) S.fh;
  status("Making " + S.name);
  if (S.src->encoding == JPEG) {
    jpeg_start();
    return true;
  }
  S.hist.assign(4096, 0);
  S.y = 0;
  S.phase = COUNT;
  return true;
}

// Median cut down to `colours` colours, max 16, from a 4-bit-per-channel histogram. Keeps splitting the box with
// the biggest spread times pixel count at its median. bins gets the colours that occur. Returns how many it made.
inline int median_cut(const std::vector<uint32_t> &hist, int colours, uint32_t *pal, std::vector<uint16_t> &bins) {
  bins.clear();
  for (int i = 0; i < 4096; i++)
    if (hist[i])
      bins.push_back((uint16_t) i);
  std::vector<std::pair<size_t, size_t>> boxes{{0, bins.size()}};
  auto ch = [](int bin, int c) { return bin >> (8 - 4 * c) & 15; };
  while ((int) boxes.size() < colours) {
    int best = -1, best_c = 0;
    uint64_t best_score = 0;
    for (size_t i = 0; i < boxes.size(); i++) {
      if (boxes[i].second - boxes[i].first < 2)
        continue;
      int lo[3] = {15, 15, 15}, hi[3] = {0, 0, 0};
      uint64_t n = 0;
      for (size_t k = boxes[i].first; k < boxes[i].second; k++) {
        for (int c = 0; c < 3; c++) {
          lo[c] = std::min(lo[c], ch(bins[k], c));
          hi[c] = std::max(hi[c], ch(bins[k], c));
        }
        n += hist[bins[k]];
      }
      int c = hi[0] - lo[0] >= hi[1] - lo[1] && hi[0] - lo[0] >= hi[2] - lo[2] ? 0
              : hi[1] - lo[1] >= hi[2] - lo[2]                                 ? 1
                                                                               : 2;
      uint64_t score = n * (uint64_t) (hi[c] - lo[c]);
      if (score > best_score) {
        best_score = score;
        best = (int) i;
        best_c = c;
      }
    }
    if (best < 0)
      break;
    size_t a = boxes[best].first, b = boxes[best].second;
    std::sort(bins.begin() + a, bins.begin() + b,
              [&](uint16_t p, uint16_t q) { return ch(p, best_c) < ch(q, best_c); });
    uint64_t n = 0, acc = 0;
    for (size_t k = a; k < b; k++)
      n += hist[bins[k]];
    size_t mid = a;
    while (mid < b - 1 && acc + hist[bins[mid]] <= n / 2)
      acc += hist[bins[mid++]];
    if (mid == a)
      mid = a + 1;
    boxes[best] = {a, mid};
    boxes.push_back({mid, b});
  }
  for (size_t i = 0; i < boxes.size(); i++) {
    uint64_t r = 0, g = 0, b = 0, n = 0;
    for (size_t k = boxes[i].first; k < boxes[i].second; k++) {
      uint32_t c = bin_rgb(bins[k]), cnt = hist[bins[k]];
      r += (c >> 16) * cnt;
      g += (c >> 8 & 255) * cnt;
      b += (c & 255) * cnt;
      n += cnt;
    }
    if (n)
      pal[i] = (uint32_t) (r / n) << 16 | (uint32_t) (g / n) << 8 | (uint32_t) (b / n);
  }
  return (int) boxes.size();
}

// Builds the histogram a slice at a time, then picks the 16 colours
inline void count(uint32_t t0) {
  for (; S.y < S.made.h && micros() - t0 < SLICE_US; S.y++)
    for (int x = 0; x < S.made.w; x++)
      S.hist[bin(px(x, S.y))]++;
  if (S.y < S.made.h)
    return;
  std::vector<uint16_t> bins;
  uint32_t pal[16] = {};
  median_cut(S.hist, 16, pal, bins);
  S.colours = (int) std::min<size_t>(bins.size(), 17);
  uint64_t total = 0;
  for (uint16_t bn : bins)
    total += S.hist[bn];
  // Keep the panel's palette if it's nearly as good, so only the changed rows need to go
  const uint32_t *use = pal;
  if (S.shown.format == PIC_PALETTE && S.shown.w == S.made.w && S.shown.h == S.made.h && total) {
    uint64_t e_new = 0, e_old = 0;
    for (uint16_t bn : bins) {
      uint32_t c = bin_rgb(bn);
      e_new += (uint64_t) dist(c, pal[nearest(c, pal)]) * S.hist[bn];
      e_old += (uint64_t) dist(c, S.shown.pal[nearest(c, S.shown.pal)]) * S.hist[bn];
    }
    if (e_old <= e_new + e_new / 4 + total * 30)
      use = S.shown.pal;
  }
  memcpy(S.made.pal, use, sizeof S.made.pal);
  for (int i = 0; i < 4096; i++)
    S.lut[i] = (uint8_t) nearest(bin_rgb(i), S.made.pal);
  drop(S.hist);
  S.made.rows.push_back(0);
  S.y = 0;
  S.phase = MAP;
}

// The 16-colour version, packed row by row while adding up the error
inline void map(uint32_t t0) {
  const int w = S.made.w;
  std::vector<uint8_t> idx((size_t) w + 1, 0), row(((size_t) w + 1) / 2);
  for (; S.y < S.made.h && micros() - t0 < SLICE_US; S.y++) {
    for (int x = 0; x < w; x++) {
      uint32_t c = px(x, S.y);
      uint8_t i = S.lut[bin(c)];
      idx[x] = i;
      S.made.err += ycc_dist(c, S.made.pal[i]);
    }
    for (size_t k = 0; k < row.size(); k++)
      row[k] = (uint8_t) (idx[2 * k] << 4 | idx[2 * k + 1]);
    pack(row.data(), row.size(), S.made.data);
    S.made.rows.push_back((uint16_t) S.made.data.size());
    if (S.made.data.size() > POOL) {  // doesn't fit, a JPEG then
      drop(S.made.data);
      S.fits = false;
      if (S.src->encoding == PALETTE) {
        ESP_LOGW("panel", "Picture %s: too detailed for 16 colours in %u bytes, sending a JPEG", S.name.c_str(),
                 (unsigned) POOL);
      }
      jpeg_start();
      return;
    }
  }
  if (S.y < S.made.h)
    return;
  S.made.err /= (double) S.made.w * S.made.h;
  S.fits = true;
  if (S.src->encoding == PALETTE || S.colours <= 16)
    S.phase = CHOOSE;
  else
    jpeg_start();
}

// The JPEG. Tries qualities until it has the best that fits, a few rows a loop.
inline void jpeg_try(int q) {
  int w = (S.fw + 15) & ~15, h = (S.fh + 7) & ~7;
  S.buf.resize(POOL);
  pjpeg_begin(&S.enc, w, h, q, S.buf.data(), POOL);
  S.lines.assign((size_t) w * 8, 0);
}

inline void jpeg_start() {
  S.lo = 0;
  S.hi = S.src->quality ? S.src->quality : 95;
  jpeg_try((S.lo + S.hi + 1) / 2);
  S.phase = JPEG_TRY;
}

inline void jpeg(uint32_t t0) {
  pjpeg_t &e = S.enc;
  bool more = true;
  while (more && micros() - t0 < SLICE_US) {
    int y0 = e.row * 8;  // the source lines for this row of MCUs, black past the picture's edge
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < e.w; x++)
        S.lines[(size_t) y * e.w + x] = x < S.fw && y0 + y < S.fh ? px(x, y0 + y) : 0;
    more = pjpeg_row(&e, [](void *, int x, int y) { return S.lines[(size_t) (y % 8) * S.enc.w + x]; }, nullptr);
  }
  if (more)
    return;
  int q = (int) (((uint32_t) S.lo + S.hi + 1) / 2);
  uint32_t n = pjpeg_end(&e);
  if (n) {  // fits, the best so far
    S.lo = q;
    S.jpeg.format = PIC_JPEG;
    S.jpeg.w = e.w;
    S.jpeg.h = e.h;
    S.jpeg.quality = q;
    S.jpeg.err = pjpeg_mse(&e);
    S.jpeg.data = std::vector<uint8_t>(S.buf.begin(), S.buf.begin() + n);
  } else {
    S.hi = q - 1;
  }
  if (S.lo < S.hi) {
    jpeg_try((S.lo + S.hi + 1) / 2);
    return;
  }
  drop(S.buf);
  drop(S.lines);
  S.phase = CHOOSE;
}

// 16 colours if it fits and was asked for or isn't clearly worse, else the JPEG. 16 colours is sharp, so it still
// wins with up to twice the JPEG's error.
inline bool choose() {
  bool palette = S.fits && (S.src->encoding == PALETTE || S.colours <= 16 || !S.jpeg.w || S.made.err <= 2 * S.jpeg.err);
  if (S.src->done)
    S.src->done();
  if (!palette && !S.jpeg.w) {
    status(S.name + ": doesn't fit the panel even as a JPEG at quality 1");
    reset();
    return false;
  }
  if (!palette)
    S.made = std::move(S.jpeg);
  S.jpeg = Pic();
  S.made.data.shrink_to_fit();
  S.made.rows.shrink_to_fit();
  if (S.made.format == PIC_JPEG) {
    ESP_LOGI("panel", "Picture %s: %ux%u as a JPEG at quality %d, %u bytes", S.name.c_str(), S.made.w, S.made.h,
             S.made.quality, (unsigned) S.made.data.size());
  } else {
    ESP_LOGI("panel", "Picture %s: %ux%u in 16 colours, %u bytes", S.name.c_str(), S.made.w, S.made.h,
             (unsigned) S.made.data.size());
  }
  S.phase = PLAN;
  return true;
}

inline std::string what(const Pic &p) {
  char b[48];
  if (p.format == PIC_JPEG)
    snprintf(b, sizeof b, " (JPEG, quality %d, %.1f KB)", p.quality, p.data.size() / 1024.0);
  else
    snprintf(b, sizeof b, " (16 colours, %.1f KB)", p.data.size() / 1024.0);
  return b;
}

// made is on the panel now
inline void now_shown() {
  S.shown = std::move(S.made);
  S.made = Pic();
  S.phase = IDLE;
  status("Shown: " + S.name + what(S.shown));
}

// The whole picture, or only the changed rows in pieces. False if nothing has to go.
inline bool plan() {
  S.jobs.clear();
  S.head = S.ended = false;
  Pic &made = S.made, &shown = S.shown;
  if (made.format == PIC_JPEG || shown.format != made.format || shown.w != made.w || shown.h != made.h) {
    S.jobs.push_back({true, true, 0, made.h});
  } else {
    auto differs = [&](int r) {
      size_t a = made.rows[r + 1] - made.rows[r], b = shown.rows[r + 1] - shown.rows[r];
      return a != b || memcmp(made.data.data() + made.rows[r], shown.data.data() + shown.rows[r], a) != 0;
    };
    // The panel keeps a piece behind the picture in its memory until it's applied, so it has to fit there.
    // size is the panel's picture with the earlier pieces applied.
    uint32_t bytes = 0, size = shown.data.size();
    bool fits = true;
    for (int r = 0; r < made.h;) {
      if (!differs(r)) {
        r++;
        continue;
      }
      int s = r;
      while (r < made.h && differs(r) && size + (made.rows[r + 1] - made.rows[s]) <= POOL)
        r++;
      fits = r > s;
      if (!fits)
        break;  // not even one row fits, send the whole picture
      S.jobs.push_back({false, false, (uint16_t) s, (uint16_t) (r - s)});
      bytes += made.rows[r] - made.rows[s];
      size += (made.rows[r] - made.rows[s]) - (shown.rows[r] - shown.rows[s]);
    }
    if (memcmp(made.pal, shown.pal, sizeof made.pal)) {
      if (S.jobs.empty())
        S.jobs.push_back({false, true, 0, 0});  // only the colours changed
      else
        S.jobs[0].pal = true;
    }
    if (!fits || bytes > made.data.size() * 3 / 4) {
      S.jobs.clear();
      S.jobs.push_back({true, true, 0, made.h});
    }
    ESP_LOGI("panel", "Picture %s: %u pieces, %u bytes changed", S.name.c_str(), (unsigned) S.jobs.size(),
             (unsigned) bytes);
  }
  if (S.jobs.empty()) {
    now_shown();
    return false;
  }
  S.phase = SEND;
  status("Sending " + S.name);
  return true;
}

// The panel's K_PIC_ACK = status << 24 | id << 16 | bytes received, from panel.h
inline void ack(uint32_t a) {
  uint8_t st = a >> 24;
  uint32_t n = a & 0xFFFF;
  if (S.phase != SEND || !S.head || (a >> 16 & 0xFF) != S.id)
    return;                  // for the animation or an earlier piece
  if (st == PIC_PROGRESS) {  // another KiB
    if (S.len)
      status("Sending " + S.name + ": " + std::to_string(std::min<uint32_t>(99, n * 100 / S.len)) + " %");
  } else if (st == PIC_DONE) {  // piece applied
    S.jobs.erase(S.jobs.begin());
    S.head = S.ended = false;
    if (S.jobs.empty())
      now_shown();
  } else if (st == PIC_NO_ROOM || st == PIC_NO_PICTURE) {  // piece didn't fit or the panel restarted, send it all
    S.shown = Pic();
    S.phase = PLAN;
  } else if (st != PIC_TOO_BIG && ++S.tries <= 2) {  // retry with the whole picture
    S.shown = Pic();
    S.phase = PLAN;
  } else {
    status(S.name + ": the panel didn't take it (" + std::to_string(st) + ")");
    S.shown = Pic();
    reset();
  }
}

// The panel restarted. It keeps the picture in RAM, so it goes again.
inline void restarted() {
  if (S.phase == IDLE && S.name == S.want && !S.shown.data.empty()) {
    S.made = std::move(S.shown);
    S.phase = PLAN;
  } else if (S.phase == SEND) {
    S.phase = PLAN;
  }
  S.shown = Pic();
}

inline void put16(uint8_t *b, unsigned v) {
  b[0] = (uint8_t) v;
  b[1] = (uint8_t) (v >> 8);
}

inline void put32(uint8_t *b, uint32_t v) {
  put16(b, v & 0xFFFF);
  put16(b + 2, v >> 16);
}

inline void put_palette(uint8_t *b, const uint32_t *pal) {
  for (int i = 0; i < 16; i++, b += 3) {
    b[0] = (uint8_t) (pal[i] >> 16);
    b[1] = (uint8_t) (pal[i] >> 8);
    b[2] = (uint8_t) pal[i];
  }
}

// After a picture's or animation's END: true once the panel has had ACK_MS on a working link and hasn't answered.
// since is when END went, or the link last wasn't working.
inline bool no_answer(bool ended, uint32_t &since) {
  if (!ended || !link::connected || link::tx.refused) {
    since = millis();
    return false;
  }
  return millis() - since > ACK_MS;
}

// After a picture's or animation's header: data[pos..len) as DATA while the link has room, then END with the CRC
inline void send_data(const uint8_t *data, uint32_t len, uint32_t &pos, bool &ended) {
  while (link::waiting() < link::BACKLOG && pos < len) {
    uint8_t b[64] = {};  // DATA must be a multiple of 4 bytes, so the last chunk is padded
    uint32_t n = std::min<uint32_t>(sizeof b, len - pos);
    memcpy(b, data + pos, n);
    if (!link::send(K_PIC_DATA, b, (int) (n + 3) & ~3))
      break;
    pos += n;
  }
  if (pos >= len && !ended) {
    link::send(K_PIC_END, plink_crc32(0, data, len));
    ended = true;
  }
}

// The next piece's header, then its data as the link has room. Done when the panel acks it.
inline void send() {
  if (S.phase != SEND || S.jobs.empty())
    return;
  if (no_answer(S.head && S.ended, S.end_ms)) {  // the panel lost the piece or its answer
    S.shown = Pic();
    if (++S.tries <= 2) {
      ESP_LOGW("panel", "Picture %s: no answer from the panel, sending all of it again", S.name.c_str());
      S.phase = PLAN;
    } else {
      status(S.name + ": the panel didn't answer");
      reset();
    }
    return;
  }
  if (!link::connected)
    return;
  const Pic &made = S.made;
  Job &j = S.jobs.front();
  if (!S.head) {
    if (link::waiting() > link::BACKLOG || anim::sending())
      return;  // let the animation finish first
    bool jpeg = made.format == PIC_JPEG;
    S.start = jpeg ? 0 : made.rows[j.y];
    S.len = jpeg ? (uint32_t) made.data.size() : made.rows[j.y + j.count] - S.start;
    S.pos = 0;
    S.id = ++transfer_ids;
    uint8_t h[58] = {S.id};
    if (j.full) {  // BEGIN: format, w, h, len, the palette for 16 colours
      h[1] = made.format;
      put16(h + 2, made.w);
      put16(h + 4, made.h);
      put32(h + 6, S.len);
      put_palette(h + 10, made.pal);
      link::send(K_PIC_BEGIN, h, jpeg ? 10 : 58);
    } else {  // PATCH: y, count, len, the palette if it changed
      put16(h + 1, j.y);
      put16(h + 3, j.count);
      put32(h + 5, S.len);
      put_palette(h + 9, made.pal);
      link::send(K_PIC_PATCH, h, j.pal ? 57 : 9);
    }
    S.head = true;
  }
  send_data(made.data.data() + S.start, S.len, S.pos, S.ended);
}

inline bool sending() { return S.phase == SEND && S.head; }

// every 20 ms
// A download only starts if the file fits. The server's size minus what's free, with room for the decoded picture.
inline bool fits(const std::string &name, Online &o) {
  size_t n = o.length(o.url), room = RAMAllocator<uint8_t>().get_max_free_block_size();
  if (n && n + 16384 > room) {
    ESP_LOGW("panel", "Picture %s: %u KB, the ESP has %u KB free. Ask for a smaller one", name.c_str(),
             (unsigned) (n / 1024), (unsigned) (room / 1024));
    if (name == S.want)
      S.waiting = std::to_string(n / 1024) + " KB is too big for the ESP";
    return false;
  }
  return true;
}

inline void loop() {
  if (S.to_clear && !anim::sending())
    S.to_clear = !link::send(K_PIC_CLEAR, 0);
  auto o = online.find(S.want);  // the shown online picture downloads again every `every`
  if (o != online.end() && o->second->every && S.phase == IDLE && !S.redo &&
      millis() - o->second->fetched >= o->second->every && fits(S.want, *o->second)) {
    o->second->fetched = millis();
    o->second->fetch();
  }
  if (!ask())
    return;
  uint32_t t0 = micros();
  if (S.phase == START && !start())
    return;
  if (S.phase >= COUNT && S.phase <= JPEG_TRY) {  // an online picture being downloaded again changes under us
    int w, h;
    if (!S.src->size(w, h)) {
      S.redo = true;
      return;
    }
  }
  if (S.phase == COUNT) {
    count(t0);
    return;
  }
  if (S.phase == MAP) {
    map(t0);
    return;
  }
  if (S.phase == JPEG_TRY) {
    jpeg(t0);
    return;
  }
  if (S.phase == CHOOSE && !choose())
    return;
  if (S.phase == PLAN && !plan())
    return;
  send();
}

}  // namespace flexit::panel::picture
