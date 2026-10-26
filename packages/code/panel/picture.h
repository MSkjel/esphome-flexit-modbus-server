// Screensaver pictures from picture.yaml, online_picture.yaml and met.h. They go to the panel as 16 colours with
// each row packed on its own, or as a JPEG at the best quality that fits the panel's 14 KB.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "panel_link.h"
#include "pdeflate.h"
#include "pjpeg.h"
#include "plink.h"
#include "rgb.h"

namespace flexit::panel::picture {

// ---- shared with animation.h: the transfer to the panel and what goes with it

static constexpr uint32_t POOL = PIC_POOL;  // the panel's picture memory
static constexpr uint32_t SLICE_US = 5000;  // max conversion time per loop, so the main loop keeps running
static constexpr uint32_t ACK_MS = 10000;   // the panel answers END within a second or so
static constexpr int BINS = 4096;           // colours at 4 bits per channel, see bin()
// RAM to make a picture: counting colours (the counts, the bins, the colour table), then a JPEG (the one being
// tried, its lines). The peak is a JPEG with the 16-colour version still kept.
static constexpr uint32_t COUNT_RAM = BINS * 4 + BINS * 2 + BINS, JPEG_RAM = POOL + PIC_MAX_W * 8 * 3,
                          MAKE_RAM = POOL + PIC_MAX_W + JPEG_RAM;

template<typename T> void drop(std::vector<T> &v) { std::vector<T>().swap(v); }  // empty, with its RAM freed

// Whether the ESP has `bytes` free with some to spare, `block` of it in one piece
inline bool has_ram(size_t bytes, size_t block) {
  RAMAllocator<uint8_t> ram;
  return ram.get_free_heap_size() >= bytes + 8192 && ram.get_max_free_block_size() >= block;
}

// The colour table: each bin that has pixels to the nearest of pal's n colours
inline void fill_lut(std::vector<uint8_t> &lut, const std::vector<uint32_t> &hist, const uint32_t *pal, int n) {
  for (int i = 0; i < BINS; i++)
    lut[i] = hist[i] ? (uint8_t) nearest(bin_rgb(i), pal, n) : 0;
}

// A status text sensor, published only when it changes
template<typename T> void status(T &sensor, const std::string &s) {
  if (sensor.state != s)
    sensor.publish_state(s);
}

// The status for a K_PIC_ACK's progress, len bytes in all
inline std::string progress(const std::string &name, uint32_t ack, uint32_t len) {
  return "Sending " + name + ": " + std::to_string(std::min<uint32_t>(99, (ack & 0xFFFF) * 100 / len)) + " %";
}

// The panel takes one picture or animation at a time. Each transfer gets a new id to match the acks.
inline uint8_t transfer_ids;

// A picture's piece or an animation on its way: its header went, then its DATA up to pos, then its END
struct Transfer {
  static constexpr int TRIES = 2;         // goes again this often when lost or refused
  static constexpr unsigned CHUNK = 64;   // bytes of DATA at a time
  static inline const Transfer *on_link;  // the last one opened, the one on its way while its head is set
  uint8_t id = 0;
  bool head = false, ended = false;
  uint32_t pos = 0, end_ms = 0;  // end_ms, see no_answer()
  int tries = 0;
  void open() {  // with its header
    id = ++transfer_ids;
    head = true;
    ended = false;
    pos = 0;
    on_link = this;
  }
  void reset() {
    head = false;
    tries = 0;
  }
  // To go again from its header, false once it had its tries
  bool retry() {
    head = false;
    return ++tries <= TRIES;
  }
  bool answers(uint32_t ack) const { return head && (ack >> 16 & 0xFF) == id; }  // a K_PIC_ACK for it
  bool blocked() const { return on_link && on_link != this && on_link->head; }   // the other one is on its way

  // True once the panel has had ACK_MS on a working link since END and hasn't answered. end_ms is when END went,
  // or the link last wasn't working.
  bool no_answer() {
    if (!head || !ended || !link::connected || link::tx.refused) {
      end_ms = millis();
      return false;
    }
    return millis() - end_ms > ACK_MS;
  }

  // DATA as the link has room, then END with the CRC of data. next(b, max) gives up to max more bytes of what goes,
  // fewer only at the end. Each turn has room for a chunk and the END's 4 bytes, so neither send fails.
  template<typename Next> void send_data(const uint8_t *data, uint32_t len, Next next) {
    uint32_t t0 = micros();
    while (!ended && link::waiting() < link::BACKLOG && link::fits(CHUNK + 4, 2) && micros() - t0 < SLICE_US) {
      uint8_t b[CHUNK] = {};  // DATA must be a multiple of 4 bytes, so the last chunk is padded
      unsigned n = next(b, sizeof b);
      if (n)
        link::send(K_PIC_DATA, b, (int) (n + 3) & ~3);
      if (n < sizeof b) {
        link::send(K_PIC_END, plink_crc32(0, data, len));
        ended = true;
      }
    }
  }

  // send_data() with data as it is, from pos on
  void send_raw(const uint8_t *data, uint32_t len) {
    send_data(data, len, [&](uint8_t *b, unsigned max) {
      unsigned k = std::min<uint32_t>(max, len - pos);
      memcpy(b, data + pos, k);
      pos += k;
      return k;
    });
  }
};

// ---- pictures

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
// times out. Before a download fits() asks the server about the file, hanging up after the headers.
struct Online {
  std::string url;
  // the file's HTTP status (0 = no answer), size (0 = unknown) and Content-Type
  std::function<int(const std::string &url, size_t &size, std::string &type)> head;
  std::function<bool()> busy;   // a download still runs
  std::function<bool()> fetch;  // starts the download, true if it did
  std::string format;           // the yaml's, upper case: JPEG, JPG, PNG, BMP or AUTO
  uint32_t pixels = 0;          // the decoded picture's bytes at the yaml's resize
  // What online_image keeps between downloads: its buffer, buffer_size until a JPEG grows it, and its decoder
  // ('J', 'P' or 'B'). next becomes kept once a download that passed fits() starts.
  struct Kept {
    size_t buffer = 4096;
    char decoder = 0;
  } kept, next;
  uint32_t every = 0;         // ms between downloads while shown, 0 = only when shown
  uint32_t fetched = 0;       // when the last one started
  uint32_t at = 0, wait = 0;  // when to try again after a failure, and the wait after that one
};
inline std::map<std::string, std::shared_ptr<Online>> online;

// "30s", "10min", "1.5h", "1d" as ESPHome writes them. 0 = never, also for anything else.
inline uint32_t interval_ms(const char *s) {
  static const struct {
    const char *unit;
    double ms;
  } units[] = {{"ms", 1},        {"milliseconds", 1}, {"s", 1e3},       {"sec", 1e3},  {"seconds", 1e3}, {"min", 6e4},
               {"minutes", 6e4}, {"h", 3.6e6},        {"hours", 3.6e6}, {"d", 8.64e7}, {"days", 8.64e7}};
  char *end;
  double n = strtod(s, &end);
  while (*end == ' ')
    end++;
  for (const auto &u : units)
    if (end != s && !strcmp(end, u.unit))
      return n > 0 ? (uint32_t) std::min(n * u.ms, 4e9) : 0;
  if (strcmp(s, "never"))
    ESP_LOGE("panel", "update_interval '%s' isn't a time like 10min, so it's only downloaded when shown", s);
  return 0;
}

inline bool download(const std::string &name, Online &o);  // below, it needs S

// No request headers for http_request's get(), where {} would match its old std::list overload too
struct NoHeaders {
  template<typename T> operator std::vector<T>() const { return {}; }
};

// http is online_picture.yaml's http_request
template<typename Img, typename Http>
void add_online_image(const std::string &name, Img &&image, Http &&http, const char *url, const char *every,
                      const char *format, const char *resize, const char *encoding, const char *quality) {
  auto *img = ptr_of(image);
  auto *web = ptr_of(http);
  auto o = std::make_shared<Online>();
  o->url = url;
  o->every = interval_ms(every);
  for (const char *c = format; *c; c++)
    o->format += (char) toupper((unsigned char) *c);
  int rw = 0, rh = 0;
  if (sscanf(resize, "%dx%d", &rw, &rh) == 2 && rw > 0 && rh > 0)
    o->pixels = (uint32_t) rw * rh * 2;  // RGB565
  o->head = [web](const std::string &url, size_t &size, std::string &type) {
    auto c = web->get(url, NoHeaders(), {"content-type"});
    if (!c)
      return 0;
    int code = c->status_code;
    size = c->content_length;
    type = c->get_response_header("content-type");
    c->end();  // hangs up after the headers
    return code;
  };
  o->busy = [img]() { return img->is_decoding(); };
  o->fetch = [img]() {
    img->update();
    return img->is_decoding();
  };
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
    if (!o->wait || (int32_t) (millis() - o->at) >= 0)
      download(name, *o);
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
  std::string name;
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

struct Zip {  // a 16-colour piece's bytes as pzip pieces against what the panel holds
  pzip_t z;
  std::vector<uint16_t> table;  // while sending, if there's the RAM
  uint8_t buf[64 + PZIP_MAX];
  unsigned n;    // bytes in buf, not sent yet
  uint16_t row;  // like_row()'s row
};

// The JPEG at the best quality that fits
struct Search {
  std::vector<std::pair<int, uint32_t>> sizes;  // qualities tried and their sizes
  int encodes = 0;                              // JPEGs made for this picture, for the log
  pjpeg_t enc;                                  // the JPEG being tried, written into jpeg.data
  int ox = 0, oy = 0;                           // where the picture is in it, see place()
  std::vector<uint8_t> lines;                   // the 8 lines the encoder is on, RGB
  int line = 0;                                 // how many of them are filled
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
  int phase = IDLE, y = 0, sw = 0, sh = 0, fw = 0, fh = 0;
  int colours = 0;    // colours in the picture at 4 bits a channel, 17 = more than 16
  bool fits = false;  // the 16-colour version fits the panel
  bool kept = false;  // made.pal is the panel's, kept so only the changed rows go
  // while mapping: each colour's pixels added up, R, G, B and how many, and all of them as R*R, G*G, B*B, R*G, R*B
  // and G*B, which is all the error needs
  uint32_t sum[16][4] = {};
  uint64_t sq[6] = {};
  Search search;
  Transfer t;                                          // the piece being sent
  bool to_clear = false;                               // K_PIC_CLEAR still to send
  uint32_t retry_at = 0, piece_at = 0, piece_len = 0;  // the piece's bytes in made.data
  std::vector<uint32_t> hist;                          // pixel count per colour, at 4 bits per channel
  std::vector<Job> jobs;
  std::vector<uint8_t> lut;  // colour at 4 bits per channel -> palette index, while mapping
  Zip zip;
} S;

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

// online_picture.yaml's on_error. A shown picture stays until the next refresh, else size() tries again later.
inline void failed(const std::string &name) {
  if (name != S.want || (S.phase == IDLE && !S.redo && !S.shown.data.empty()))
    return;
  S.waiting = "couldn't download it, trying again later";
  if (S.phase == IDLE)
    S.redo = true;
}

// For RGB a and b, the sum of their Y's, Cb's and Cr's products. ycc(d, d) is the squared difference d makes in
// Y, Cb and Cr, the units of pjpeg's error.
inline double ycc(const double *a, const double *b) {
  static const double M[3][3] = {{0.299, 0.587, 0.114}, {-0.168736, -0.331264, 0.5}, {0.5, -0.418688, -0.081312}};
  double s = 0;
  for (const auto &m : M)
    s += (m[0] * a[0] + m[1] * a[1] + m[2] * a[2]) * (m[0] * b[0] + m[1] * b[1] + m[2] * b[2]);
  return s;
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
  drop(S.search.lines);
  drop(S.search.sizes);
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
  // The pieces the panel has applied are in shown. One on its way is dropped once another comes, unless it's whole
  // and the panel has dropped its picture for it, or its END went and it may be in.
  if (S.phase == SEND && S.t.head && (S.t.ended || S.jobs.front().full))
    S.shown = Pic();
  if (S.src && S.name != S.want && S.src->done)
    S.src->done();  // the one being made isn't wanted now
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
  S.fits = false;
  S.made.w = (uint16_t) S.fw;
  S.made.h = (uint16_t) S.fh;
  status("Making " + S.name);
  if (S.src->encoding == JPEG)
    return jpeg_start();
  if (own()) {  // nothing to count, and its colours are exact
    memcpy(S.made.pal, S.src->pal, sizeof S.made.pal);
    S.colours = 16;
    S.kept = true;
    memset(S.sum, 0, sizeof S.sum);
    memset(S.sq, 0, sizeof S.sq);
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

// Median cut down to `colours` colours, max 16, from a 4-bit-per-channel histogram. Keeps splitting the box with
// the biggest spread times pixel count at its median. bins gets the colours that occur. Returns how many it made.
inline int median_cut(const std::vector<uint32_t> &hist, int colours, uint32_t *pal, std::vector<uint16_t> &bins) {
  bins.clear();
  for (int i = 0; i < BINS; i++)
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
  // Keep the panel's palette if it's nearly as good, so only the changed rows need to go. Both are taken to the bins'
  // centres for that, as the panel's are its pixels' means and would lose to colours made from the centres.
  S.kept = false;
  if (S.shown.format == PIC_PALETTE && S.shown.w == S.made.w && S.shown.h == S.made.h && total) {
    auto err = [&](const uint32_t *p) {
      uint64_t e = 0;
      for (uint16_t bn : bins) {
        uint32_t c = bin_rgb(bn);
        e += (uint64_t) colour_dist(c, bin_rgb(bin(p[nearest(c, p)]))) * S.hist[bn];
      }
      return e;
    };
    uint64_t e_new = err(pal);
    S.kept = err(S.shown.pal) <= e_new + e_new / 4 + total * 30;
  }
  memcpy(S.made.pal, S.kept ? S.shown.pal : pal, sizeof S.made.pal);
  fill_lut(S.lut, S.hist, S.made.pal, 16);
  drop(S.hist);
  memset(S.sum, 0, sizeof S.sum);
  memset(S.sq, 0, sizeof S.sq);
  S.made.data.reserve(POOL + PIC_MAX_W);  // all it needs, past POOL it's a JPEG anyway
  S.made.rows.push_back(0);
  S.y = 0;
  S.phase = MAP;
}

// Each colour moved to the mean of its pixels from its bins' centres, unless it's the panel's palette. The pixels
// keep their colour, and the error can only drop.
inline void means() {
  if (S.kept)
    return;
  for (int i = 0; i < 16; i++) {
    const uint32_t *s = S.sum[i];
    if (s[3])
      S.made.pal[i] = (s[0] + s[3] / 2) / s[3] << 16 | (s[1] + s[3] / 2) / s[3] << 8 | (s[2] + s[3] / 2) / s[3];
  }
}

// The 16-colour version's squared error over all its pixels. For pixels c of a colour p, ycc(c - p, c - p) adds up
// to the sum of ycc(c, c), less 2 ycc(sum of c, p), plus how many times ycc(p, p).
inline double err16() {
  const uint64_t *q = S.sq;
  double c[3][3] = {{(double) q[0], (double) q[3], (double) q[4]},
                    {(double) q[3], (double) q[1], (double) q[5]},
                    {(double) q[4], (double) q[5], (double) q[2]}},
         e = 0;
  for (int k = 0; k < 3; k++) {
    double unit[3] = {};
    unit[k] = 1;
    e += ycc(unit, c[k]);
  }
  for (int i = 0; i < 16; i++) {
    const uint32_t *s = S.sum[i];
    uint32_t p = S.made.pal[i];
    double sum[3] = {(double) s[0], (double) s[1], (double) s[2]},
           rgb[3] = {(double) (p >> 16), (double) (p >> 8 & 255), (double) (p & 255)};
    e += s[3] * ycc(rgb, rgb) - 2 * ycc(sum, rgb);
  }
  return e;
}

// The 16-colour version, packed row by row while adding up what its colours and error need
inline void map(uint32_t t0) {
  const int w = S.made.w;
  std::vector<uint8_t> idx((size_t) w + 1, 0), row(((size_t) w + 1) / 2);
  bool mine = own();
  for (; S.y < S.made.h && micros() - t0 < SLICE_US; S.y++) {
    uint32_t sq[6] = {};  // the row's, 320 * 255 * 255 at most
    if (mine)
      S.src->row(S.y, idx.data());
    for (int x = 0; x < w && !mine; x++) {
      uint32_t c = px(x, S.y), r = c >> 16, g = c >> 8 & 255, b = c & 255;
      uint8_t i = S.lut[bin(c)];
      idx[x] = i;
      uint32_t *s = S.sum[i];
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
      S.sq[k] += sq[k];
    for (size_t k = 0; k < row.size(); k++)
      row[k] = (uint8_t) (idx[2 * k] << 4 | idx[2 * k + 1]);
    pack(row.data(), row.size(), S.made.data);
    S.made.rows.push_back((uint16_t) S.made.data.size());
    if (S.made.data.size() > POOL) {  // doesn't fit, a JPEG then
      drop(S.made.data);
      drop(S.lut);
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
  drop(S.lut);
  means();
  S.made.err = err16() / ((double) S.made.w * S.made.h);
  S.fits = true;
  if (S.src->encoding == PALETTE || S.colours <= 16)
    S.phase = CHOOSE;
  else
    jpeg_start();
}

inline int top_quality() { return S.src->quality ? S.src->quality : 95; }  // the highest JPEG quality to try

// Where n pixels go in the JPEG's p, as near the middle as they can with an edge on its 8-pixel blocks. Exactly in
// the middle both edges can be inside blocks, and those sharp edges cost 3-7% more bytes.
inline int place(int n, int p) {
  int at = 0;
  for (int o = 1; o <= p - n; o++)
    if ((o % 8 == 0 || (o + n) % 8 == 0) && abs(2 * o - (p - n)) < abs(2 * at - (p - n)))
      at = o;
  return at;
}

// The JPEG at quality q into jpeg.data, with its size at the qualities in also counted along. Padded to whole MCUs
// around the picture, to a height in 16s if at all, so the panel's 8-line rows centre it.
inline void jpeg_try(int q, std::initializer_list<int> also = {}) {
  Search &js = S.search;
  int w = (S.fw + 15) & ~15, h = S.fh % 8 ? (S.fh + 15) & ~15 : S.fh;
  js.ox = place(S.fw, w);
  js.oy = place(S.fh, h);
  pjpeg_begin(&js.enc, w, h, q, S.jpeg.data.data(), POOL);
  js.encodes++;
  for (int a : also)
    if (a >= 1 && a <= top_quality() && a != q)
      pjpeg_also(&js.enc, a);
  js.lines.resize((size_t) w * 8 * 3);
  js.line = 0;
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

// Without the RAM for a JPEG it's the 16-colour version if that fits, else false. The first try is at the quality
// it had last time if the panel has it as a JPEG, likely the one for a picture downloaded again, else at 50 with 30
// counted along for the size's slope.
inline bool jpeg_start() {
  if (!has_ram(JPEG_RAM, POOL)) {
    if (!S.fits)
      return no_ram();
    ESP_LOGW("panel", "Picture %s: no RAM for a JPEG, sending 16 colours", S.name.c_str());
    S.phase = CHOOSE;
    return true;
  }
  bool again = S.shown.name == S.name && S.shown.format == PIC_JPEG;
  int q = std::min(top_quality(), again ? S.shown.quality : 50);
  S.search.sizes.clear();
  S.search.encodes = 0;
  S.jpeg.data.resize(POOL);
  jpeg_try(q, {q + 1, again ? 0 : q - 20});
  S.phase = JPEG_TRY;
  return true;
}

// The best quality that fits so far (0 = none) and the lowest above it that doesn't (past the highest = none)
inline void jpeg_bounds(int &lo, int &hi) {
  lo = 0;
  hi = top_quality() + 1;
  for (auto &s : S.search.sizes)
    if (s.second <= POOL)
      lo = std::max(lo, s.first);
  for (auto &s : S.search.sizes)
    if (s.second > POOL && s.first > lo)
      hi = std::min(hi, s.first);
}

// The next quality to try, the one whose JPEG should just fit. Its size less the headers goes about as libjpeg's
// quantizer scale to a power, found from lo's and hi's sizes, or from the nearest try at least 3 off the one there is.
inline int jpeg_guess(int lo, int hi) {
  auto x = [](int q) { return logf(std::max(1.0f, q < 50 ? 5000.0f / q : 200.0f - 2 * q)); };
  auto y = [](uint32_t n) { return logf(std::max(1.0f, (float) n - 600)); };
  const std::pair<int, uint32_t> *a = nullptr, *b = nullptr;
  const auto &sizes = S.search.sizes;
  for (auto &s : sizes)
    if (s.first == (lo ? lo : hi))
      a = &s;
    else if (s.first == hi)
      b = &s;
  if (!lo || hi > top_quality()) {
    b = nullptr;
    for (auto &s : sizes)
      if (abs(s.first - a->first) >= 3 && (!b || abs(s.first - a->first) < abs(b->first - a->first)))
        b = &s;
  }
  float slope = -0.65f;
  if (b && x(b->first) != x(a->first))
    slope = std::min(-0.2f, std::max(-1.5f, (y(b->second) - y(a->second)) / (x(b->first) - x(a->first))));
  float scale = expf(x(a->first) + (logf(POOL - 600) - y(a->second)) / slope);
  int q = (int) (scale > 100 ? 5000 / scale : 100 - scale / 2);
  return std::min(hi - 1, std::max(std::max(lo, 1), q));
}

// A source line or an MCU at a time, so the loop gets back to the bus within about SLICE_US. Then the best quality
// that fits once it's known that the next one up doesn't, with its bytes in jpeg.data.
inline void jpeg(uint32_t t0) {
  Search &js = S.search;
  pjpeg_t &e = js.enc;
  bool more = true;
  while (more && micros() - t0 < SLICE_US) {
    if (js.line < 8) {  // the next line for this row of MCUs
      int y = e.row * 8 + js.line;
      uint8_t *l = &js.lines[(size_t) js.line * e.w * 3];
      for (int x = 0; x < e.w; x++, l += 3) {
        uint32_t c = jpx(x, y);
        l[0] = (uint8_t) (c >> 16);
        l[1] = (uint8_t) (c >> 8);
        l[2] = (uint8_t) c;
      }
      js.line++;
      continue;
    }
    more = pjpeg_mcu(
        &e,
        [](void *, int x, int y) {
          const uint8_t *l = &S.search.lines[((size_t) (y % 8) * S.search.enc.w + x) * 3];
          return (uint32_t) l[0] << 16 | (uint32_t) l[1] << 8 | l[2];
        },
        nullptr);
    if (!e.col)
      js.line = 0;  // on to the next row
  }
  if (more)
    return;
  pjpeg_end(&e);
  for (int i = 0; i < e.n; i++)
    js.sizes.push_back({e.o[i].quality, e.o[i].len});
  int lo, hi, q = e.o[0].quality;
  jpeg_bounds(lo, hi);
  if (lo + 1 < hi) {
    q = jpeg_guess(lo, hi);
    jpeg_try(q, {q + 1 < hi ? q + 1 : 0});
    return;
  }
  if (lo && q != lo) {  // lo is the best, but jpeg.data has another try's bytes
    jpeg_try(lo);
    return;
  }
  if (lo) {
    S.jpeg.format = PIC_JPEG;
    S.jpeg.w = e.w;
    S.jpeg.h = e.h;
    S.jpeg.quality = lo;
    S.jpeg.err = pjpeg_mse(&e) * e.w * e.h / ((double) S.fw * S.fh);  // per pixel of the picture
    S.jpeg.data.resize(e.o[0].len);
  } else {
    S.jpeg = Pic();
  }
  drop(js.lines);
  drop(js.sizes);
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
  status("Shown: " + S.name + what(S.shown));
}

// The changed rows of a 16-colour picture the panel has at the same size, in pieces. All of it if they don't fit.
inline void patches() {
  const Pic &made = S.made, &shown = S.shown;
  auto differs = [&](int r) {
    size_t a = made.rows[r + 1] - made.rows[r], b = shown.rows[r + 1] - shown.rows[r];
    return a != b || memcmp(made.data.data() + made.rows[r], shown.data.data() + shown.rows[r], a) != 0;
  };
  // The panel keeps a piece behind the picture in its memory until it's applied, so it has to fit there.
  // size is the panel's picture with the earlier pieces applied. Unchanged rows up to GAP between changed ones go
  // along: deflated against the panel's picture they cost a few bytes, another piece a round trip.
  static constexpr int GAP = 16;
  uint32_t bytes = 0, size = shown.data.size();
  bool fits = true;
  for (int r = 0; r < made.h;) {
    if (!differs(r)) {
      r++;
      continue;
    }
    int s = r, end = r;  // end: past the last changed row in this piece
    for (; r < made.h && r - end < GAP && size + (made.rows[r + 1] - made.rows[s]) <= POOL; r++)
      if (differs(r))
        end = r + 1;
    r = end;
    fits = end > s;
    if (!fits)
      break;  // not even one row fits, send the whole picture
    S.jobs.push_back({false, false, (uint16_t) s, (uint16_t) (end - s)});
    bytes += made.rows[end] - made.rows[s];
    size += (made.rows[end] - made.rows[s]) - (shown.rows[end] - shown.rows[s]);
  }
  if (memcmp(made.pal, shown.pal, sizeof made.pal)) {
    // The panel takes new colours at their piece's END, so rows a later piece changes would show in them with
    // their old indices till it comes. One piece from the first changed row to the last then, if it fits.
    if (S.jobs.size() > 1) {
      uint16_t s = S.jobs.front().y, end = S.jobs.back().y + S.jobs.back().count;
      bytes = made.rows[end] - made.rows[s];
      fits = shown.data.size() + bytes <= POOL;
      S.jobs.assign(1, {false, false, s, (uint16_t) (end - s)});
    }
    if (S.jobs.empty())
      S.jobs.push_back({false, true, 0, 0});  // only the colours changed
    else
      S.jobs[0].pal = true;
  }
  if (!fits)
    S.jobs.assign(1, {true, true, 0, made.h});
  ESP_LOGI("panel", "Picture %s: %u pieces, %u bytes", S.name.c_str(), (unsigned) S.jobs.size(), (unsigned) bytes);
}

// The whole picture, or only the changed rows in pieces. False if nothing has to go.
inline bool plan() {
  S.jobs.clear();
  const Pic &made = S.made, &shown = S.shown;
  bool same = shown.name == made.name && shown.format == made.format && shown.w == made.w && shown.h == made.h;
  if (same && made.format != PIC_JPEG)
    patches();
  else if (!same || shown.data != made.data)  // a JPEG goes whole, and not at all if it's the same
    S.jobs.push_back({true, true, 0, made.h});
  if (S.jobs.empty()) {
    now_shown();
    return false;
  }
  S.phase = SEND;
  status("Sending " + S.name);
  return true;
}

inline void applied(const Job &j);

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
    if (S.jobs.empty())
      now_shown();
    else
      applied(j);
  } else if (st == PIC_NO_ROOM || st == PIC_NO_PICTURE) {  // piece didn't fit or the panel restarted, send it all
    S.shown = Pic();
    S.t.head = false;
    S.phase = PLAN;
  } else if (st != PIC_TOO_BIG && S.t.retry()) {  // retry with the whole picture
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

// For send_data(): S.zip's pieces
inline unsigned pieces(uint8_t *b, unsigned max) {
  Zip &z = S.zip;
  for (unsigned k; z.n < max && (k = pzip(&z.z, z.buf + z.n));)
    z.n += k;
  unsigned k = std::min(z.n, max);
  memcpy(b, z.buf, k);
  memmove(z.buf, z.buf + k, z.n - k);
  z.n -= k;
  return k;
}

// pzip's like() for a patch: the same place in the row it replaces on the panel
inline int32_t like_row(void *, uint32_t i) {
  const Pic &m = S.made, &p = S.shown;
  while (S.zip.row + 1u < m.h && m.rows[S.zip.row + 1] <= S.piece_at + i)
    S.zip.row++;
  uint32_t at = p.rows[S.zip.row] + (S.piece_at + i - m.rows[S.zip.row]);
  return at < p.rows[S.zip.row + 1] ? (int32_t) at : -1;
}

// The panel spliced the patch into its picture, so shown does too and the next one matches what it has
inline void applied(const Job &j) {
  Pic &p = S.shown;
  const Pic &m = S.made;
  uint32_t a = p.rows[j.y], b = p.rows[j.y + j.count];
  int32_t delta = (int32_t) (m.rows[j.y + j.count] - m.rows[j.y]) - (int32_t) (b - a);
  p.data.erase(p.data.begin() + a, p.data.begin() + b);
  p.data.insert(p.data.begin() + a, m.data.begin() + m.rows[j.y], m.data.begin() + m.rows[j.y + j.count]);
  for (unsigned r = j.y + 1u; r <= j.y + j.count; r++)
    p.rows[r] = (uint16_t) (a + m.rows[r] - m.rows[j.y]);
  for (unsigned r = j.y + j.count + 1u; r <= p.h; r++)
    p.rows[r] = (uint16_t) (p.rows[r] + delta);
  if (j.pal)
    memcpy(p.pal, m.pal, sizeof p.pal);
}

// The next piece's header, and for 16 colours pzip set up to pack its rows
inline void begin_piece() {
  const Pic &made = S.made;
  const Job &j = S.jobs.front();
  bool jpeg = made.format == PIC_JPEG;
  S.piece_at = jpeg ? 0 : made.rows[j.y];
  S.piece_len = jpeg ? (uint32_t) made.data.size() : made.rows[j.y + j.count] - S.piece_at;
  S.t.open();
  uint8_t h[PIC_BEGIN_HEAD + PIC_PAL_LEN];
  if (j.full) {  // BEGIN, the palette for 16 colours
    int n = plink_pic_begin(h, S.t.id, made.format, made.w, made.h, S.piece_len, jpeg ? nullptr : made.pal);
    link::send(K_PIC_BEGIN, h, n);
  } else {  // PATCH, the palette if it changed
    link::send(K_PIC_PATCH, h, plink_pic_patch(h, S.t.id, j.y, j.count, S.piece_len, j.pal ? made.pal : nullptr));
  }
  if (jpeg)
    return;
  // a whole one only matches itself, a patch the panel's picture too
  if (S.zip.table.empty() && has_ram(PZIP_TABLE * 2, PZIP_TABLE * 2))
    S.zip.table.resize(PZIP_TABLE);
  S.zip.z = {j.full ? nullptr : S.shown.data.data(),
             j.full ? 0 : (uint32_t) S.shown.data.size(),
             made.data.data() + S.piece_at,
             S.piece_len,
             0,
             S.zip.table.empty() ? nullptr : S.zip.table.data(),
             0,
             j.full ? nullptr : like_row,
             nullptr};
  S.zip.n = 0;
  S.zip.row = j.y;
}

// The next piece's header, then its data as the link has room. Done when the panel acks it.
inline void send() {
  if (S.phase != SEND || S.jobs.empty())
    return;
  if (S.t.no_answer()) {  // the panel lost the piece or its answer
    S.shown = Pic();
    if (S.t.retry()) {
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
  if (!S.t.head) {
    if (link::waiting() > link::BACKLOG || S.t.blocked())
      return;  // let the animation finish first
    begin_piece();
  }
  const uint8_t *data = S.made.data.data() + S.piece_at;
  if (S.made.format == PIC_JPEG)
    S.t.send_raw(data, S.piece_len);
  else
    S.t.send_data(data, S.piece_len, pieces);
}

// Whether the ESP has the RAM to download, decode and make an online picture. online_image keeps a JPEG whole
// in a buffer it only grows, PNG and BMP stream through it. A new decoder: JPEG 17 KB, PNG 44 KB in one piece.
inline bool fits(const std::string &name, Online &o) {
  size_t n = 0;
  std::string type, why, ram;  // ram: what the ESP has, for the log
  int code = o.head(o.url, n, type);
  for (auto &c : type)
    c = (char) tolower((unsigned char) c);
  auto is = [&](const char *f, const char *t) {
    return o.format == f || (o.format == "AUTO" && type.find(t) != std::string::npos);
  };
  char kind = is("JPEG", "jpeg") || is("JPG", "jpg") ? 'J' : is("PNG", "png") ? 'P' : 'B';
  bool jpeg = kind == 'J';
  size_t dec = kind == o.kept.decoder || kind == 'B' ? 0 : jpeg ? 17 * 1024 : 44 * 1024;  // a new decoder
  size_t grow = jpeg && n > o.kept.buffer ? n : 0, need = grow + dec + o.pixels + MAKE_RAM;
  if (code != 200) {
    why = code > 0 ? "the server says " + std::to_string(code) + ", trying again later"
                   : "couldn't reach the server, trying again later";
  } else if (jpeg && !n) {
    why = "the server doesn't say how big it is, which a JPEG needs";
  } else if (!has_ram(need, std::max({grow, dec, (size_t) o.pixels}))) {
    RAMAllocator<uint8_t> a;
    why = "too big for the ESP, it needs " + std::to_string(need / 1024) + " KB of RAM";
    ram = ". It has " + std::to_string(a.get_free_heap_size() / 1024) + " KB free, " +
          std::to_string(a.get_max_free_block_size() / 1024) + " KB in one piece";
  }
  if (why.empty()) {
    o.next = {std::max(o.kept.buffer, grow), kind};
    return true;
  }
  ESP_LOGW("panel", "Picture %s: %s%s", name.c_str(), why.c_str(), ram.c_str());
  if (name == S.want)
    S.waiting = why;
  return false;
}

// Starts a download if it fits, false if not. The wait before size() tries again doubles until one comes.
inline bool download(const std::string &name, Online &o) {
  o.fetched = millis();
  if (o.busy())  // the last one still runs, no need to ask the server
    return true;
  o.wait = o.wait ? std::min<uint32_t>(2 * o.wait, 30 * 60000) : 30000;
  o.at = millis() + o.wait;
  if (!fits(name, o))
    return false;
  if (name == S.want)
    S.waiting = "waiting for it";
  if (o.fetch())
    o.kept = o.next;  // online_image grows its buffer and makes the decoder for it
  return true;
}

// The shown online picture downloads again every `every`, if it still fits
inline void refresh() {
  auto o = online.find(S.want);
  if (link::connected && o != online.end() && o->second->every && S.phase == IDLE && !S.redo &&
      millis() - o->second->fetched >= o->second->every && !download(S.want, *o->second))
    status(S.want + ": " + S.waiting);
}

// every 20 ms
inline void loop() {
  if (S.to_clear && !S.t.blocked())
    S.to_clear = !link::send(K_PIC_CLEAR, 0);
  refresh();
  if (!ask())
    return;
  if (!link::connected) {  // nothing is made or downloaded without the panel
    if (S.phase >= COUNT && S.phase <= JPEG_TRY) {
      if (S.src->done)
        S.src->done();
      reset();
      S.redo = true;
    }
    if (S.phase == START)
      status(S.name + ": waiting for the panel");
    return;
  }
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
  switch (S.phase) {
    case COUNT:
      count(t0);
      break;
    case MAP:
      map(t0);
      break;
    case JPEG_TRY:
      jpeg(t0);
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
      send();
      break;
  }
}

}  // namespace flexit::panel::picture
