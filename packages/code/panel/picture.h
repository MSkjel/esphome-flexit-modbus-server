// Screensaver pictures from picture.yaml, picture_online.h and met.h. They go to the panel as 16 colours with each
// row packed on its own, or as a JPEG at the best quality that fits the panel's 14 KB.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "panel_link.h"
#include "picture_jpeg.h"
#include "picture_plan.h"
#include "plink.h"
#include "quant.h"
#include "rgb.h"
#include "transfer.h"
#include "util.h"

namespace flexit::panel::picture {

inline constexpr uint32_t POOL = PIC_POOL;  // the panel's picture memory
// RAM for a JPEG try and its source lines, and to make a picture: the peak is a JPEG with the 16-colour version kept
inline constexpr uint32_t JPEG_RAM = POOL + PIC_MAX_W * 8 * 3, MAKE_RAM = POOL + PIC_MAX_W + JPEG_RAM;
static_assert(Transfer::CHUNK <= Zip::MAX, "a chunk of pieces fits Zip's buffer");

enum Encoding { AUTO, PALETTE, JPEG };

// A picture as the packages register it
struct Source {
  std::function<bool(int &w, int &h)> size;  // false while it isn't ready, like an online one still downloading
  std::function<uint32_t(int, int)> pixel;   // 0xRRGGBB
  std::function<void()> done;                // optional, its pixels aren't needed now
  // Optional, for a source with its own 16 colours at the screen's size, like met.h's: pal, and row(y, idx) with
  // row y as indexes into it. Then nothing is counted or mapped.
  const uint32_t *pal = nullptr;
  std::function<void(int y, uint8_t *idx)> row;
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

// start(), count() and map() for 16 colours and jpeg() for the JPEG as needed, choose(), plan(), then send()
enum Phase { IDLE, START, COUNT, MAP, JPEG_TRY, CHOOSE, PLAN, SEND };

struct State {
  std::string want;     // the picture asked for, "" = none
  bool redo = true;     // want is new or its picture changed. True at first so the status gets published
  std::string waiting;  // why the wanted one isn't ready, for the status
  bool away = false;    // the status says it's waiting for the panel
  // shown is what the panel has once it's sent. made is the 16-colour version or the one being sent, jpeg the JPEG.
  Pic shown, made, jpeg;
  std::string name;  // the picture being made or sent
  const Source *src = nullptr;
  // sw, sh is the source size, fw, fh fitted to the screen
  int phase = IDLE, y = 0, sw = 0, sh = 0, fw = 0, fh = 0;
  int colours = 0;            // colours in the picture at 4 bits a channel, 17 = more than 16
  bool palette_fits = false;  // the 16-colour version fits the panel
  bool kept = false;          // made.pal is the panel's, kept so only the changed rows go
  Sums sums;                  // while mapping
  Search search;
  Transfer t;                                          // the piece being sent
  bool to_clear = false;                               // K_PIC_CLEAR still to send
  uint32_t retry_at = 0, piece_at = 0, piece_len = 0;  // the piece's bytes in made.data
  std::vector<uint32_t> hist;                          // pixel count per colour, at 4 bits per channel
  std::vector<Job> jobs;
  std::vector<uint8_t> lut;  // colour at 4 bits per channel -> palette index, while mapping
  Zip zip;
};
inline State S;
inline void (*on_shown)() = nullptr;  // what the panel shows changed, for snapshot.h's camera

inline void status(const std::string &s) { status(id(panel_picture_status), s); }

// The Panel Picture select's set_action. An online picture is downloaded again. "None" or "" is none.
inline void picked(const std::string &name) {
  if (name.empty() || name == "None") {
    S.want.clear();
    S.redo = true;
    S.to_clear = true;  // K_PIC_CLEAR also stops an animation on its way, so loop() waits for that
    S.shown = Pic();    // so a show() before the next loop sends all of it
    return;
  }
  if (!sources.count(name)) {
    ESP_LOGW("panel", "No picture called '%s'", name.c_str());
    return;
  }
  S.want = name;
  S.redo = true;
}

// panel_show_picture. Goes through the select when it has the name, so restore_value keeps it. "" is none.
inline void show(const std::string &name) {
  std::string o = name.empty() ? "None" : name;
  if (id(panel_picture).has_option(o))
    id(panel_picture).make_call().set_option(o).perform();
  else
    picked(o);
}

inline void clear() { show(""); }

// A picture changed: convert it again if it's the one wanted, and only the rows that differ are sent. If not,
// free its RAM.
inline void changed(const std::string &name) {
  auto it = sources.find(name);
  if (name == S.want)
    S.redo = true;
  else if (it != sources.end() && it->second.done)
    it->second.done();
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
  drop(S.lut);
  S.search.drop();
  drop(S.zip.table);
  S.jobs.clear();
  S.t.head = false;
  S.phase = IDLE;
}

// Not enough RAM to make it. Tried again when it's picked again or changes.
inline bool no_ram() {
  status(S.name + ": the ESP doesn't have the RAM to make it");
  if (S.src->done)
    S.src->done();
  reset();
  return false;
}

// Picks up a new request. False if there's nothing to do now.
inline bool ask() {
  if (!S.redo)
    return true;
  S.redo = false;
  S.away = false;
  // The pieces the panel has applied are in shown. One on its way is dropped once another comes, unless it's whole
  // and the panel has dropped its picture for it, or its END went and it may be in.
  if (S.phase == SEND && S.t.head && (S.t.ended || S.jobs.front().full))
    S.shown = Pic();
  if (S.src && S.name != S.want && S.src->done)
    S.src->done();  // the one being made isn't wanted now
  reset();
  if (S.want.empty()) {
    S.shown = Pic();
    if (on_shown)
      on_shown();
    status("None");
    return false;
  }
  auto it = sources.find(S.want);
  if (it == sources.end()) {
    status(S.want + ": no picture with that name");
    return false;
  }
  S.src = &it->second;
  if (S.name != S.want)
    S.waiting = "waiting for it";  // the same one keeps its reason
  S.name = S.want;
  S.t.tries = 0;
  S.retry_at = millis();
  S.phase = START;
  return true;
}

inline bool jpeg_start();

// The source gives its own 16 colours, see Source
inline bool own() { return S.src->row && S.src->encoding != JPEG && S.fw == S.sw && S.fh == S.sh; }

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
  if (S.sw * PIC_MAX_H >= S.sh * PIC_MAX_W) {
    S.fw = PIC_MAX_W;
    S.fh = std::max(1, S.sh * PIC_MAX_W / S.sw);
  } else {
    S.fh = PIC_MAX_H;
    S.fw = std::max(1, S.sw * PIC_MAX_H / S.sh);
  }
  // Without exceptions a failed allocation restarts the ESP, so each step checks for room first
  size_t need = own() ? POOL + PIC_MAX_W : COUNT_RAM;  // its own colours only need the room for its rows
  if (S.src->encoding != JPEG && !has_ram(need, own() ? need : BINS * 4))
    return no_ram();
  S.palette_fits = false;
  S.made.w = (uint16_t) S.fw;
  S.made.h = (uint16_t) S.fh;
  status("Making " + S.name);
  if (S.src->encoding == JPEG)
    return jpeg_start();
  if (own()) {  // nothing to count, and its colours are exact
    memcpy(S.made.pal, S.src->pal, sizeof S.made.pal);
    S.colours = 16;
    S.kept = true;
    S.sums = Sums();
    S.made.data.reserve(POOL + PIC_MAX_W);
    S.made.rows.push_back(0);
    S.y = 0;
    S.phase = MAP;
    return true;
  }
  S.hist.assign(BINS, 0);
  S.lut.resize(BINS);
  S.y = 0;
  S.phase = COUNT;
  return true;
}

// The histogram a slice at a time, then the 16 colours. The panel's palette stays if it's nearly as good, so only
// the changed rows go.
inline void count(uint32_t deadline_us) {
  for (; S.y < S.made.h && before(deadline_us); S.y++)
    for (int x = 0; x < S.made.w; x++)
      S.hist[bin(px(x, S.y))]++;
  if (S.y < S.made.h)
    return;
  std::vector<uint16_t> bins;
  uint32_t pal[16] = {};
  median_cut(S.hist, 16, pal, bins);
  S.colours = (int) std::min<size_t>(bins.size(), 17);
  S.kept = S.shown.format == PIC_PALETTE && S.shown.w == S.made.w && S.shown.h == S.made.h &&
           nearly_as_good(S.shown.pal, pal, S.hist, bins);
  memcpy(S.made.pal, S.kept ? S.shown.pal : pal, sizeof S.made.pal);
  fill_lut(S.lut, S.hist, S.made.pal, 16);
  drop(S.hist);
  S.sums = Sums();
  S.made.data.reserve(POOL + PIC_MAX_W);  // all it needs, past POOL it's a JPEG anyway
  S.made.rows.push_back(0);
  S.y = 0;
  S.phase = MAP;
}

// The 16-colour version, packed row by row while adding up what its colours and error need
inline void map(uint32_t deadline_us) {
  const int w = S.made.w;
  uint8_t idx[PIC_MAX_W + 1] = {}, row[(PIC_MAX_W + 1) / 2];
  const size_t row_n = ((size_t) w + 1) / 2;
  bool mine = own();
  for (; S.y < S.made.h && before(deadline_us); S.y++) {
    uint32_t sq[6] = {};  // the row's, 320 * 255 * 255 at most
    if (mine)
      S.src->row(S.y, idx);
    for (int x = 0; x < w && !mine; x++) {
      uint32_t c = px(x, S.y), r = c >> 16, g = c >> 8 & 255, b = c & 255;
      uint8_t i = S.lut[bin(c)];
      idx[x] = i;
      uint32_t *s = S.sums.sum[i];
      s[0] += r;
      s[1] += g;
      s[2] += b;
      s[3]++;
      sq[0] += r * r;
      sq[1] += g * g;
      sq[2] += b * b;
      sq[3] += r * g;
      sq[4] += r * b;
      sq[5] += g * b;
    }
    for (int k = 0; k < 6; k++)
      S.sums.sq[k] += sq[k];
    for (size_t k = 0; k < row_n; k++)
      row[k] = (uint8_t) (idx[2 * k] << 4 | idx[2 * k + 1]);
    pack(row, row_n, S.made.data);
    S.made.rows.push_back((uint16_t) S.made.data.size());
    if (S.made.data.size() > POOL) {  // doesn't fit, a JPEG then
      drop(S.made.data);
      drop(S.lut);
      S.palette_fits = false;
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
  drop(S.lut);
  if (!S.kept)  // the panel's palette stays as it is
    S.sums.means(S.made.pal);
  S.made.err = S.sums.err(S.made.pal) / ((double) S.made.w * S.made.h);
  S.palette_fits = true;
  if (S.src->encoding == PALETTE || S.colours <= 16)
    S.phase = CHOOSE;
  else
    jpeg_start();
}

// Pixel of the JPEG, black around the picture. A bigger source is averaged over up to 3x3 of the pixels each one
// covers, as the nearest ones make a photo grainy. 16 colours keep the nearest, which keeps flat colours flat.
inline uint32_t jpx(int x, int y) {
  x -= S.search.ox;
  y -= S.search.oy;
  if (x < 0 || y < 0 || x >= S.fw || y >= S.fh)
    return 0;
  if (S.fw >= S.sw)
    return px(x, y);
  int x0 = x * S.sw / S.fw, y0 = y * S.sh / S.fh, w = (x + 1) * S.sw / S.fw - x0, h = (y + 1) * S.sh / S.fh - y0,
      nx = std::min(w, 3), ny = std::min(h, 3);
  uint32_t r = 0, g = 0, b = 0, n = (uint32_t) (nx * ny);
  for (int j = 0; j < ny; j++)
    for (int i = 0; i < nx; i++) {
      uint32_t c = S.src->pixel(x0 + (2 * i + 1) * w / (2 * nx), y0 + (2 * j + 1) * h / (2 * ny));
      r += c >> 16;
      g += c >> 8 & 255;
      b += c & 255;
    }
  return (r + n / 2) / n << 16 | (g + n / 2) / n << 8 | (b + n / 2) / n;
}

// No RAM for a JPEG: the 16-colour version if it fits, else false. One the panel has as a JPEG starts at its quality.
inline bool jpeg_start() {
  if (!has_ram(JPEG_RAM, POOL)) {
    if (!S.palette_fits)
      return no_ram();
    ESP_LOGW("panel", "Picture %s: no RAM for a JPEG, sending 16 colours", S.name.c_str());
    S.phase = CHOOSE;
    return true;
  }
  bool again = S.shown.name == S.name && S.shown.format == PIC_JPEG;
  S.jpeg.data.resize(POOL);
  S.search.start(S.fw, S.fh, S.src->quality ? S.src->quality : TOP_QUALITY, again ? S.shown.quality : 0,
                 S.jpeg.data.data());
  S.phase = JPEG_TRY;
  return true;
}

// A source line or an MCU at a time until deadline_us. Done once the best quality that fits is in jpeg.data.
inline void jpeg(uint32_t deadline_us) {
  Search &js = S.search;
  bool more = true;
  while (more && before(deadline_us))
    more = js.step(jpx);
  if (more || js.next())
    return;
  if (js.best) {
    S.jpeg.format = PIC_JPEG;
    S.jpeg.w = js.enc.w;
    S.jpeg.h = js.enc.h;
    S.jpeg.quality = js.best;
    S.jpeg.err = js.err();
    S.jpeg.data.resize(js.enc.o[0].len);
  } else {
    S.jpeg = Pic();
  }
  js.drop();
  S.phase = CHOOSE;
}

// 16 colours if it fits and was asked for or isn't clearly worse, else the JPEG. 16 colours is sharp, so it still
// wins with up to twice the JPEG's error.
inline bool choose() {
  bool palette =
      S.palette_fits && (S.src->encoding == PALETTE || S.colours <= 16 || !S.jpeg.w || S.made.err <= 2 * S.jpeg.err);
  if (S.src->done)
    S.src->done();
  if (!palette && !S.jpeg.w) {
    status(S.name + ": doesn't fit the panel even as a JPEG at quality 1");
    reset();
    return false;
  }
  if (!palette)
    S.made = std::move(S.jpeg);
  S.made.name = S.name;
  S.jpeg = Pic();
  S.made.data.shrink_to_fit();
  S.made.rows.shrink_to_fit();
  if (S.made.format == PIC_JPEG) {
    ESP_LOGI("panel", "Picture %s: %ux%u as a JPEG at quality %d (%d encodes), %u bytes", S.name.c_str(), S.made.w,
             S.made.h, S.made.quality, S.search.encodes, (unsigned) S.made.data.size());
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
  drop(S.zip.table);
  S.shown = std::move(S.made);
  S.made = Pic();
  S.phase = IDLE;
  S.t.tries = 0;
  if (on_shown)
    on_shown();
  status("Shown: " + S.name + what(S.shown));
}

// The whole picture, or only the changed rows in pieces. False if nothing has to go.
inline bool plan() {
  S.jobs.clear();
  const Pic &made = S.made, &shown = S.shown;
  bool same = shown.name == made.name && shown.format == made.format && shown.w == made.w && shown.h == made.h;
  if (same && made.format != PIC_JPEG) {
    [[maybe_unused]] uint32_t bytes = patches(made, shown, S.jobs);  // only logged, unused when ESP_LOGI compiles out
    ESP_LOGI("panel", "Picture %s: %u pieces, %u bytes", S.name.c_str(), (unsigned) S.jobs.size(), (unsigned) bytes);
  } else if (!same || shown.data != made.data) {  // a JPEG goes whole, and not at all if it's the same
    S.jobs.push_back({true, true, 0, made.h});
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
  if (S.phase != SEND || !S.t.answers(a))
    return;                  // for the animation or an earlier piece
  if (st == PIC_PROGRESS) {  // another KiB
    if (S.piece_len)
      status(progress(S.name, a, S.piece_len));
  } else if (st == PIC_DONE) {  // piece applied
    Job j = S.jobs.front();
    S.jobs.erase(S.jobs.begin());
    S.t.head = false;
    if (S.jobs.empty()) {
      now_shown();
    } else {
      apply(S.shown, S.made, j);
    }
  } else if (st == PIC_NO_ROOM || st == PIC_NO_PICTURE) {  // piece didn't fit or the panel restarted, send it all
    S.shown = Pic();
    S.t.head = false;
    S.phase = PLAN;
  } else if (S.t.refused(st, !S.jobs.front().full)) {
    ESP_LOGW("panel", "Picture %s: the panel refused a piece (%d), sending all of it", S.name.c_str(), st);
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
  S.t.reset();
}

// The next piece's header, and for 16 colours pzip set up to pack its rows
inline void begin_piece() {
  const Pic &made = S.made;
  const Job &j = S.jobs.front();
  bool jpeg = made.format == PIC_JPEG;
  S.piece_at = jpeg ? 0 : made.rows[j.y];
  S.piece_len = jpeg ? (uint32_t) made.data.size() : made.rows[j.y + j.count] - S.piece_at;
  S.t.open(made.data.data() + S.piece_at, S.piece_len);
  uint8_t h[PIC_BEGIN_HEAD + PIC_PAL_LEN];
  if (j.full) {  // BEGIN, the palette for 16 colours
    int n = plink_pic_begin(h, S.t.id, made.format, made.w, made.h, S.piece_len, jpeg ? nullptr : made.pal);
    link::send(K_PIC_BEGIN, h, n);
  } else {  // PATCH, the palette if it changed
    link::send(K_PIC_PATCH, h, plink_pic_patch(h, S.t.id, j.y, j.count, S.piece_len, j.pal ? made.pal : nullptr));
  }
  if (jpeg)
    return;
  if (S.zip.table.empty() && has_ram(PZIP_TABLE * 2, PZIP_TABLE * 2))
    S.zip.table.resize(PZIP_TABLE);
  S.zip.begin(made, S.shown, j, S.piece_at, S.piece_len);
}

// The next piece's header, then its data as the link has room until deadline_us. Done when the panel acks it.
inline void send(uint32_t deadline_us) {
  if (S.phase != SEND || S.jobs.empty())
    return;
  Transfer::Pumped p =
      S.made.format == PIC_JPEG
          ? S.t.pump(deadline_us, begin_piece)
          : S.t.pump(deadline_us, begin_piece, [](uint8_t *b, unsigned max) { return S.zip.next(b, max); });
  if (p == Transfer::GOING)
    return;
  S.shown = Pic();  // the panel lost the piece or its answer
  if (p == Transfer::AGAIN) {
    ESP_LOGW("panel", "Picture %s: no answer from the panel, sending all of it again", S.name.c_str());
    S.phase = PLAN;
  } else {
    status(S.name + ": the panel didn't answer");
    reset();
  }
}

// Every 20 ms, until deadline_us, with picture_online.h's refresh() first
inline void loop(uint32_t deadline_us) {
  if (S.to_clear && !S.t.blocked())
    S.to_clear = !link::send(K_PIC_CLEAR, 0);
  if (!ask())
    return;
  if (!link::connected) {  // nothing is made or downloaded without the panel
    if (S.phase >= COUNT && S.phase <= JPEG_TRY) {
      if (S.src->done)
        S.src->done();
      reset();
      S.redo = true;
    }
    if (S.phase == START && !S.away) {
      status(S.name + ": waiting for the panel");
      S.away = true;
    }
    return;
  }
  S.away = false;
  if (S.phase == START && !start())
    return;
  if (S.phase >= COUNT && S.phase <= JPEG_TRY) {  // an online picture being downloaded again changes under us
    int w, h;
    if (!S.src->size(w, h)) {
      S.redo = true;
      return;
    }
  }
  switch (S.phase) {
    case COUNT:
      count(deadline_us);
      break;
    case MAP:
      map(deadline_us);
      break;
    case JPEG_TRY:
      jpeg(deadline_us);
      break;
    case CHOOSE:
      if (!choose())
        break;
      [[fallthrough]];
    case PLAN:
      if (!plan())
        break;
      [[fallthrough]];
    case SEND:
      send(deadline_us);
      break;
  }
}

}  // namespace flexit::panel::picture
