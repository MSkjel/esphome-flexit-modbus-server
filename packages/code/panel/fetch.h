// HTTPS requests for the packages that read the web: met.h, the space pictures, the update check, and online pictures'
// question before a download. Each package's Asker asks its server again and again, as Schedule has it. Each request
// runs in a task of its own, so the main loop keeps answering the bus through TLS's handshake and the answer. One at a
// time, the rest wait their turn, so only one request's TLS buffers are out. The answer streams through the request's
// feed() in that task, and its caller has it in the loop.
#pragma once
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <strings.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "dates.h"
#include "util.h"

namespace flexit::panel::fetch {

// The servers want to know who's asking
inline constexpr const char *USER_AGENT =
    "esphome-flexit-modbus-server (github.com/MSkjel/esphome-flexit-modbus-server)";
// The request's task: TLS's handshake, then read()'s buffer in the same room. As big as ESPHome's main loop's, which
// also does HTTPS.
inline constexpr uint32_t STACK = 8192;

struct Request;

// Who has a request's answer, in the loop. It deletes the request.
struct Caller {
  virtual void answered(Request *r) = 0;
};

struct Request {
  virtual ~Request() = default;
  // A piece of a 2xx answer, in the request's task. False once it has had enough.
  virtual bool feed(const char *, size_t) { return false; }
  Caller *by = nullptr;
  std::string url;
  const char *header = nullptr;  // one to send, like "If-Modified-Since", unless its value is empty
  std::string value;
  bool body = true;   // false hangs up after the headers
  int tx_buffer = 0;  // for the request's first line, 0 = the client's 512 bytes
  int timeout_ms = 15000;
  // The answer
  int status = 0;                                         // 0 = no answer
  bool no_ram = false;                                    // no RAM for its task, so it wasn't asked
  size_t length = 0;                                      // Content-Length, 0 if it doesn't say
  std::string type, last_modified, expires, retry_after;  // Content-Type and the rest
  unsigned stack_left = 0;
  std::atomic<bool> done{false};
};

inline esp_err_t on_header(esp_http_client_event_t *e) {
  Request &r = *(Request *) e->user_data;
  if (e->event_id == HTTP_EVENT_ON_HEADER) {
    if (!strcasecmp(e->header_key, "content-type"))
      r.type = e->header_value;
    else if (!strcasecmp(e->header_key, "last-modified"))
      r.last_modified = e->header_value;
    else if (!strcasecmp(e->header_key, "expires"))
      r.expires = e->header_value;
    else if (!strcasecmp(e->header_key, "retry-after"))
      r.retry_after = e->header_value;
  }
  return ESP_OK;
}

// The answer into r.feed() until it has had enough. False if the connection failed or the answer was cut short
// before that. Not inlined, so its buffer takes the stack the handshake had.
__attribute__((noinline)) inline bool read(esp_http_client_handle_t c, Request &r) {
  char buf[512];
  int n;
  bool more = true;
  while (more && (n = esp_http_client_read(c, buf, sizeof buf)) > 0)
    more = r.feed(buf, (size_t) n);
  return !more || (n == 0 && esp_http_client_is_complete_data_received(c));
}

inline void task(void *arg) {
  Request &r = *(Request *) arg;
  esp_http_client_config_t config = {};
  config.url = r.url.c_str();
  config.user_agent = USER_AGENT;
  config.timeout_ms = r.timeout_ms;
  config.buffer_size_tx = r.tx_buffer;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.event_handler = on_header;
  config.user_data = &r;
  if (esp_http_client_handle_t c = esp_http_client_init(&config)) {
    if (r.header && !r.value.empty())
      esp_http_client_set_header(c, r.header, r.value.c_str());
    for (int hops = 0; esp_http_client_open(c, 0) == ESP_OK; hops++) {
      int64_t n = esp_http_client_fetch_headers(c);
      if (n < 0)
        break;
      r.status = esp_http_client_get_status_code(c);
      r.length = (size_t) n;
      bool moved = r.status == 301 || r.status == 302 || r.status == 307 || r.status == 308;
      if (!moved || hops == 3 || esp_http_client_set_redirection(c) != ESP_OK)
        break;
    }
    if (r.status >= 200 && r.status < 300 && r.body && !read(c, r))
      r.status = 0;              // counts as no answer, its Last-Modified isn't kept
    esp_http_client_cleanup(c);  // TLS's buffers go with it
  }
  r.stack_left = uxTaskGetStackHighWaterMark(nullptr);
  r.done.store(true);  // r is the loop's from here on
  requests--;
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- HTTP's dates and when to ask again

// An HTTP date like "Mon, 19 Oct 2026 11:34:38 GMT" as Unix time, 0 if it isn't one
inline uint32_t http_date(const std::string &s) {
  const char *p = s.c_str();
  int d = next_int(p);
  while (*p == ' ')
    p++;
  int m = month_of(p);
  int y = next_int(p), h = next_int(p), mi = next_int(p), sec = next_int(p);
  if (d < 0 || !m || sec < 0)
    return 0;
  return unix_of(y, m, d, h, mi, sec);
}

// Retry-After's seconds from now, given as seconds or as an HTTP date. 0 without one.
inline uint32_t retry_after_s(const std::string &v, uint32_t now) {
  if (v.empty())
    return 0;
  if (isdigit((unsigned char) v[0]))
    return (uint32_t) strtoul(v.c_str(), nullptr, 10);
  uint32_t t = http_date(v);
  return t > now ? t - now : 0;
}

inline uint32_t now_s() { return (uint32_t) id(flexit_panel_time).now().timestamp; }

// When to ask a server next. Right after boot the network often isn't quite there yet and the ESP is busy, so until
// the server first answers a few tries go again EARLY_RETRY_S later. After that no answer waits ten minutes, then
// twice as long each time up to an hour. A server error is usually over soon: a minute, then twice as long each time
// up to half an hour. Refused, an hour, then twice as long up to 12. Longer if Retry-After says so.
struct Schedule {
  static constexpr uint32_t EARLY_RETRY_S = 30;
  uint32_t not_before = 0;  // no request before this (Unix time)
  uint32_t backoff_s = 0;   // doubles with each failure in a row
  bool answered = false;    // the server has answered since boot
  uint8_t early_tries = 0;  // quick retries before that

  // Offline it waits, a failed request would back off
  bool due() const {
    return network::is_connected() && id(flexit_panel_time).now().is_valid() && now_s() >= not_before;
  }
  // After a failure, wait first, then twice as long each time up to most
  void back_off(uint32_t first, uint32_t most) {
    backoff_s = std::clamp(2 * backoff_s, first, most);
    not_before = now_s() + backoff_s;
  }
  void unreachable() {
    if (!answered && early_tries < 3) {
      early_tries++;
      not_before = now_s() + EARLY_RETRY_S;
    } else {
      back_off(600, 3600);
    }
  }
  // An answer: ok asks again next_s later. Else a server error, 5xx or a 2xx without what was asked for, or a
  // refusal.
  void got(const Request &r, bool ok, uint32_t next_s) {
    answered = true;
    uint32_t now = now_s();
    if (ok) {
      backoff_s = 0;
      not_before = now + next_s;
      return;
    }
    if (r.status >= 500 || r.status < 300)
      back_off(60, 1800);
    else
      back_off(3600, 12 * 3600);  // 429 is too many requests, 403 refused
    not_before = std::max(not_before, now + std::min<uint32_t>(retry_after_s(r.retry_after, now), 12 * 3600));
  }
};

// ---------------------------------------------------------------- the askers

// A server asked again and again: every every_s, sooner again after a failure as Schedule has it, by tick() once
// it's due. A round is steps requests, one after the other, each once the one before had what was asked for. make()
// gives the request for a step, nullptr without the RAM, and got() takes its 2xx or 304 answer in the loop, true if
// it had what was asked for. The status sensor says how it went, and got() what came.
struct Asker : Caller {
  const char *what = "";     // "forecast", for the status
  const char *server = "";   // "MET Norway"
  uint32_t every_s = 86400;  // from an answer to the next request, got() can change it
  // TLS's handshake with the full bundle takes a few seconds on a busy ESP, more than the HTTP client's 4.5 s
  int timeout_ms = 15000;
  int steps = 1;
  text_sensor::TextSensor *status = nullptr;
  Schedule schedule;
  Request *asking = nullptr;
  int step = 0;
  bool have = false;  // a round's answers since boot

  virtual Request *make(int step) = 0;
  virtual bool got(Request &r) = 0;
  void say(const std::string &s) {
    if (status)
      panel::status(*status, s);
  }
  void ask(int s = 0);
  void no_ram();
  void answered(Request *r) override;
};

inline std::vector<Asker *> askers;
inline std::deque<Request *> queue;
inline Request *running;

inline void poll();
inline void (*polling)() = nullptr;

// The next one's task, if none runs. Without the RAM for it, it's answered with no_ram.
inline void start_next() {
  if (running || queue.empty())
    return;
  Request *r = queue.front();
  queue.pop_front();
  running = r;
  requests++;
  if (xTaskCreate(task, "fetch", STACK, r, 1, nullptr) == pdPASS)
    return;
  requests--;
  running = nullptr;
  r->no_ram = true;
  r->by->answered(r);
}

// r once the ones before it are done, or first, before the others waiting. Its caller has the answer.
inline void send(Request *r, bool first = false) {
  polling = poll;
  if (first)
    queue.push_front(r);
  else
    queue.push_back(r);
  start_next();
}

// Asks for the request for step s
inline void Asker::ask(int s) {
  step = s;
  Request *r = make(s);
  if (!r) {
    no_ram();
    return;
  }
  r->by = this;
  r->timeout_ms = timeout_ms;
  asking = r;
  send(r);
}

inline void Asker::no_ram() {
  ESP_LOGW("fetch", "No RAM to ask %s", server);
  schedule.not_before = now_s() + 600;
  say(std::string("No RAM for the ") + what);
}

// In the loop, once r is done: no answer or one cut off is more likely our own network than the server
inline void Asker::answered(Request *r) {
  asking = nullptr;
  bool next = false;
  if (r->no_ram) {
    no_ram();
  } else if (!r->status) {
    schedule.unreachable();
    ESP_LOGW("fetch", "Couldn't reach %s", server);
    say(std::string("Couldn't reach ") + server);
  } else {
    bool ok = ((r->status >= 200 && r->status < 300) || r->status == 304) && got(*r);
    next = ok && step + 1 < steps;
    if (!next)
      schedule.got(*r, ok, every_s);
    if (ok) {
      have = have || !next;
    } else {
      ESP_LOGW("fetch", "No %s from %s (HTTP %d), asking again in %u s", what, server, r->status,
               (unsigned) (schedule.not_before - now_s()));
      say(std::string(have ? "Kept the last " : "No ") + what + " (HTTP " + std::to_string(r->status) + ")");
    }
  }
  delete r;
  if (next)
    ask(step + 1);
}

// The answer of the one that ran, and the next one started
inline void poll() {
  if (running && running->done.load()) {
    Request *r = running;
    running = nullptr;
    ESP_LOGD("fetch", "%s left %u bytes of its stack", r->url.c_str(), r->stack_left);
    r->by->answered(r);
  }
  start_next();
}

// Each asker that's due asked, unless busy
inline void ask_due(bool busy) {
  for (Asker *a : askers)
    if (!busy && !a->asking && a->schedule.due())
      a->ask();
}

// poll() once a request is sent and ask_due() once add() has an asker, so a build without either leaves the HTTP
// client out
inline void (*asking_due)(bool busy) = nullptr;

// Every loop from panel.h
inline void loop() {
  if (polling)
    polling();
}
// Every second from panel.yaml. busy: the panel's picture is being made or sent, which would starve TLS's handshake.
inline void tick(bool busy) {
  if (asking_due)
    asking_due(busy);
}

// From a package's setup: a is asked from now on
inline void add(Asker &a) {
  askers.push_back(&a);
  asking_due = ask_due;
}

}  // namespace flexit::panel::fetch
