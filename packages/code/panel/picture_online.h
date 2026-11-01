// Pictures from a URL (online_picture.yaml, ha_picture.yaml). Downloaded when shown and every `every` while
// shown, their pixels freed once converted. A failed download goes again 30 s later, then twice as long each time
// up to 30 min, since each try holds up the main loop until the server answers or http_request times out. Before a
// download a task of its own asks the server about the file, hanging up after the headers, so fits() can tell
// whether it fits. The download then starts right after the CX60's status broadcast, so a quick server has
// answered before the link's next window.
#pragma once
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "picture.h"

namespace flexit::panel::picture {

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
  // head()'s answer, from its task. The task has url and the rest while asking is ASKING, the loop once ANSWERED.
  struct Answer {
    std::string url;
    int code = 0;
    size_t size = 0;
    std::string type;
  } answer;
  enum { IDLE, ASKING, ANSWERED };
  std::atomic<int> asking{IDLE};
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

// Whether the ESP has the RAM to download, decode and make an online picture. online_image keeps a JPEG whole
// in a buffer it only grows, PNG and BMP stream through it. A new decoder: JPEG 17 KB, PNG 44 KB in one piece.
inline bool fits(const std::string &name, Online &o) {
  size_t n = o.answer.size;
  std::string type = o.answer.type, why, ram;  // ram: what the ESP has, for the log
  int code = o.answer.code;
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

inline constexpr uint32_t ASK_STACK = 8192;  // for TLS's handshake, like met.h's request

inline void ask(Online &o) {
  o.answer.code = o.head(o.answer.url, o.answer.size, o.answer.type);
  o.asking.store(Online::ANSWERED);  // o.answer is the loop's from here on
}

inline void ask_task(void *arg) {
  ask(*(Online *) arg);
  vTaskDelete(nullptr);
}

// Asks the server about the file, and refresh() starts the download once it has answered, if it fits. The wait
// before size() tries again doubles until one comes.
inline void download(const std::string &name, Online &o) {
  o.fetched = millis();
  if (o.busy() || o.asking != Online::IDLE)  // the last one still runs, no need to ask the server
    return;
  o.wait = o.wait ? std::min<uint32_t>(2 * o.wait, 30 * 60000) : 30000;
  o.at = millis() + o.wait;
  if (name == S.want)
    S.waiting = "asking the server about it";
  o.answer.url = o.url;
  o.asking.store(Online::ASKING);
  if (xTaskCreate(ask_task, "picture", ASK_STACK, &o, 1, nullptr) != pdPASS)
    ask(o);  // no RAM for its stack, so it holds up the loop
}

// The server has answered: the download starts if it fits, false if not
inline bool answered(const std::string &name, Online &o) {
  o.asking.store(Online::IDLE);
  if (!fits(name, o))
    return false;
  if (name == S.want)
    S.waiting = "waiting for it";
  if (o.fetch())
    o.kept = o.next;  // online_image grows its buffer and makes the decoder for it
  return true;
}

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

// Whether a download can start now: right after the CX60's status broadcast, with most of a cycle before the next
// one. The link has sent in the window after it by then, in the same read. Any time when the bus can't tell.
inline constexpr uint32_t QUIET_AHEAD_US = 80000;
inline bool quiet_ahead() {
  uint32_t until = id(server).until_status_end_us();
  return until == UINT32_MAX || until >= QUIET_AHEAD_US;
}

// A download starts once the server has answered about it, and the shown online picture downloads again every
// `every`. panel.h calls it before loop().
inline void refresh() {
  for (auto &it : online) {
    Online &o = *it.second;
    if (o.asking != Online::ANSWERED)
      continue;
    if (it.first != S.want) {
      o.asking.store(Online::IDLE);  // picked away meanwhile, asked again right away once it's shown
      o.at = millis();
    } else if (quiet_ahead() && (!answered(it.first, o) || S.phase != IDLE)) {
      status(S.want + ": " + S.waiting);  // a shown one stays while the next downloads
    }
  }
  auto o = online.find(S.want);
  if (link::connected && o != online.end() && o->second->every && S.phase == IDLE && !S.redo &&
      millis() - o->second->fetched >= o->second->every)
    download(S.want, *o->second);
}

// online_picture.yaml's on_error. A shown picture stays until the next refresh, else size() tries again later.
inline void failed(const std::string &name) {
  if (name != S.want || (S.phase == IDLE && !S.redo && !S.shown.data.empty()))
    return;
  S.waiting = "couldn't download it, trying again later";
  if (S.phase == IDLE)
    S.redo = true;
}

}  // namespace flexit::panel::picture
