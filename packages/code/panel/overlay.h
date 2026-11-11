// Screensaver overlays: the animations from animation.yaml and the weather's. All frames share 16 colours, each with
// its own opacity: the source's own, or counted from the frames with one clear if any pixel is. One frame goes as a
// still, which the panel keeps packed. More go as an animation, each frame after the first the deflated rectangle
// that changed. A field has no frames, the panel works out what it shows. The panel keeps up to OVERLAY_SLOTS of
// them in OVERLAY_POOL and shows each in up to OVERLAY_PLACES places, the copies of one in step unless a copy gets a
// frame of its own. A copy can run a script there, see overlay_script.h.
#pragma once
#include <strings.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "overlay_script.h"
#include "panel_link.h"
#include "pdeflate.h"
#include "plink.h"
#include "quant.h"
#include "rgb.h"
#include "transfer.h"
#include "util.h"

namespace flexit::panel::overlay {

// x, y is where it goes on the screen, pixel(frame, x, y) gives 0xAARRGGBB, vx, vy a drift in px/s. on_shown sets
// its switch, so it follows what the actions show and clear. Its copies fade in and out over fade_ms, and their light
// below 100 takes its colours toward shade, but for those that glow.
struct Source {
  int w, h, frames, frame_ms, x, y, scale;
  std::function<uint32_t(int, int, int)> pixel;
  int vx = 0, vy = 0;
  std::function<void(bool)> on_shown = {};
  int fade_ms = 0;
  std::vector<uint16_t> delays = {};  // ms each frame shows, if not all frame_ms
  // Colours of its own, 0xAARRGGBB, 16 at most, each pixel the nearest. Counted from the frames if there are none.
  std::vector<uint32_t> colours = {};
  Clips clips = {};                  // for its scripts
  Program script = {};               // copy 0's when it's shown
  std::vector<uint32_t> glows = {};  // 0xRRGGBB, its pixels in them glow
  uint32_t shade = 0;                // 0xRRGGBB
  // What Panel Animation Status calls it, its name without. Those with one label are one entry, with how many.
  std::string label = {};
  std::vector<uint32_t> cycles = {};  // 0xRRGGBB of its own colours, those a copy's phase takes round
  // Not frames but an OVERLAY_FIELD_* of w x h, -1 for none, with what it takes: the panel draws how much of it
  // there is at each pixel as that far along its 16 colours
  int field = -1, field_a = 0, field_b = 0;
  size_t ram = 0;  // what pixel() takes of the ESP's RAM while it's made, like a frame it draws
};

// ms frame f of s shows
inline uint32_t frame_ms(const Source &s, int f) { return s.delays.empty() ? s.frame_ms : s.delays[f]; }
inline std::map<std::string, Source> sources;

// frame_ms goes in OVERLAY_TICK_MS steps, and the panel shows a frame OVERLAY_FRAME_MIN_MS at least
inline constexpr int MAX_FRAME_MS = OVERLAY_DELAY_MAX * OVERLAY_TICK_MS;
inline void add(const std::string &name, Source s) {
  int ms = std::clamp(s.frame_ms, OVERLAY_FRAME_MIN_MS, MAX_FRAME_MS);
  if (ms != s.frame_ms) {
    ESP_LOGW("panel", "Animation %s: frame_ms %d is outside %d..%d, it plays at %d", name.c_str(), s.frame_ms,
             OVERLAY_FRAME_MIN_MS, MAX_FRAME_MS, ms);
  }
  s.frame_ms = ms;
  sources[name] = std::move(s);
}

// A field of w x h for add(): an OVERLAY_FIELD_* with what it takes, along colours, 0xAARRGGBB, 16 of them
inline Source field(int w, int h, int kind, int a, int b, std::vector<uint32_t> colours) {
  Source s{w, h, 1, MAX_FRAME_MS, 0, 0, 1, nullptr};
  s.colours = std::move(colours);
  s.field = kind;
  s.field_a = a;
  s.field_b = b;
  return s;
}

// A copy on the panel: one of its places
struct Copy {
  bool used = false, placed = false;  // placed: the panel has it, sent once the animation is in
  uint8_t place = 0;                  // the panel's place
  int16_t x = 0, y = 0;
  bool own = false;                 // a frame of its own, out of step with the others
  uint8_t depth = OVERLAY_NEAREST;  // its OVERLAY_DEPTH, sent with its PLACE
  int fade = -1;                    // ms it fades in and out, -1 for the animation's
  std::string doing;                // how it moves, for the status
  Program script;                   // what it runs on the panel, sent after what it was told
  uint8_t tag = 0;                  // its script's END's, the panel's OVERLAY_GONE has it. 0 without one.
  // What it was told since it was placed, the last of each kind and channel, sent again after a panel restart
  struct Told {
    uint16_t key;
    uint32_t at;  // millis(), so a glide sent again ends on time
    std::vector<uint8_t> m;
  };
  std::vector<Told> told;
};

enum State { WANT, SENT, FAILED };  // WANT: to be made and sent, or sent again after a panel restart

// A slot on the panel, the index is the slot
struct Overlay {
  std::string name;  // "" = free slot
  const Source *src = nullptr;
  int state = WANT, w = 0, h = 0, scale = 1;
  bool half = false, still = false;  // still: one frame, OVERLAY_STILL
  bool sheet = false;                // its frames as a still's pieces each, OVERLAY_SHEET
  bool field = false;                // no frames, OVERLAY_FIELD
  uint32_t pal[16] = {};             // 0xAARRGGBB
  uint16_t glows = 0, cycles = 0;    // a bit for each of pal that glows, and that a copy's phase takes round
  std::vector<uint8_t> data;         // the frame records, or a still's pieces
  std::vector<Copy> copies;
  uint16_t frame_rate = 100;  // percent, 0 = paused
  std::string why;            // when it failed
  bool lasting = false;       // it would fail again, so it waits until it's shown again
  uint8_t looks = 0;          // times in a row the ESP hadn't the RAM to make it, 0 when it isn't waiting for that
  uint32_t looked = 0;        // millis() at the last of them
  size_t frame_bytes() const { return (size_t) (w + 1) / 2 * h; }
  size_t panel_frame() const { return still || sheet ? 0 : frame_bytes(); }  // what the panel keeps of its frame
};
inline Overlay overlays[OVERLAY_SLOTS];

// count() builds the palette from all frames, make() the records, send() sends them. One overlay at a time.
enum Phase { IDLE, COUNT, MAKE, SEND };
inline constexpr int ROWS_PX = 256;  // pixels a call takes at least, a row or a few, so the time is looked at seldom
// One the ESP hasn't the RAM to make waits, and the RAM is looked at again every RAM_LOOK_MS: a picture being made
// or a request holds some for a while. After RAM_LOOKS it's made at half size, if there's the RAM for that.
inline constexpr uint32_t RAM_LOOK_MS = 2000;
inline constexpr int RAM_LOOKS = 15;
inline constexpr uint32_t FNV_START = 2166136261u, FNV_PRIME = 16777619u;  // FNV-1a, for the frames' hashes

struct Work {
  Overlay *a = nullptr;
  int phase = IDLE, f = 0, y = 0;  // y: the row of frame f
  bool any_transp = false;
  Transfer t;
  std::vector<uint32_t> hist, hash;         // hash: each frame's pixels, to find a GIF that repeats itself
  uint32_t sum = 0;                         // frame f's hash so far
  int frames = 0;                           // to make, a repeating GIF's first round only
  std::vector<uint8_t> frame, prev, first;  // 2 pixels per byte
  std::vector<uint8_t> sheet;               // its frames as a sheet, made too while it has copies with their own
  int own = 0;                              // those copies
  std::vector<uint8_t> lut;                 // colour at 4 bits per channel -> palette index, 0xFF until looked up
  std::vector<uint32_t> enc, tok;           // pdeflate's work for a frame, see pdeflate_work_t
  std::vector<uint16_t> rest;
  int colours = 0;                  // in the palette
  uint32_t record_us = UINT32_MAX;  // what a record takes, as fits() has it
};
inline Work W;

inline Overlay *find(const std::string &name) {
  for (auto &a : overlays)
    if (!a.name.empty() && a.name == name)
      return &a;
  return nullptr;
}

// What a copy takes of the panel's pool besides a frame: its script, what it was told is bound, and its wander or
// face
inline size_t pool_bytes(const Copy &c) {
  size_t n = c.script.bytes.empty() ? 0 : OVERLAY_SCRIPT_HEAD + 4 * c.script.regs + c.script.bytes.size();
  bool roams = c.script.roams;
  for (auto &t : c.told) {
    n += t.key == K_OVERLAY_BIND ? t.m.size() - OVERLAY_BIND_VALUE + 1 : 0;
    roams |= t.key == K_OVERLAY_WANDER || t.key == K_OVERLAY_FACE;
  }
  return n + (roams ? OVERLAY_ROAM : 0) + c.script.gives;
}

// Bytes of the panel's OVERLAY_POOL that the overlays it has or is getting take, with their copies' own frames and
// scripts, other than a's
inline size_t used_by_others(const Overlay *a) {
  size_t n = 0;
  for (auto &o : overlays) {
    if (&o == a || !(o.state == SENT || (&o == W.a && W.t.head)))
      continue;
    n += o.panel_frame() + o.data.size();
    for (auto &c : o.copies)
      n += c.placed ? (c.own ? o.panel_frame() : 0) + pool_bytes(c) : 0;
  }
  return n;
}

// What's left of the pool for the one being made
inline size_t room() { return OVERLAY_POOL - std::min<size_t>(OVERLAY_POOL, used_by_others(W.a)); }

inline std::string label(const Overlay &a) { return a.src && !a.src->label.empty() ? a.src->label : a.name; }

// What's on its way to the panel, making or sending one, for the status. "" for nothing.
inline std::string under_way;

// For Panel Animation Status: how full the panel is, what it shows, what's on its way and what failed. No longer
// than Home Assistant takes, the list cut short with how many more there are.
inline constexpr size_t STATUS_MAX = 255;
inline std::string shown() {
  int slots = 0, places = 0;
  std::vector<std::pair<std::string, int>> items;  // what it shows, and how many with that label
  std::string after = under_way.empty() ? "" : "; " + under_way;
  for (auto &a : overlays) {
    if (a.name.empty())
      continue;
    slots++;
    if (a.state == FAILED)
      after += "; " + label(a) + ": " + a.why;
    if (a.state == WANT && a.looks) {
      std::string waits = "; " + label(a) + ": waiting for the ESP's RAM";
      if (after.find(waits) == std::string::npos)  // once for those with one label
        after += waits;
    }
    if (a.state != SENT)
      continue;
    int n = 0;
    std::string doing;
    for (auto &c : a.copies)
      if (c.used) {
        n++;
        places += c.placed;
        doing = c.doing;
      }
    std::string l = label(a);
    bool grouped = a.src && !a.src->label.empty();
    auto it = std::find_if(items.begin(), items.end(), [&](const auto &i) { return i.first == l; });
    if (grouped && it != items.end())
      it->second++;
    else
      items.push_back({grouped ? l : l + (n > 1 ? " ×" + std::to_string(n) : n ? doing : ""), 1});
  }
  if (!slots)
    return "None";
  char head[80];
  snprintf(head, sizeof head, "%d of %d, %d of %d places, %.1f of %.1f KB: ", slots, OVERLAY_SLOTS, places,
           OVERLAY_PLACES, (double) used_by_others(nullptr) / 1024, OVERLAY_POOL / 1024.0);
  for (size_t keep = items.size();; keep--) {
    std::string list;
    for (size_t i = 0; i < keep; i++)
      list +=
          (i ? ", " : "") + items[i].first + (items[i].second > 1 ? " (" + std::to_string(items[i].second) + ")" : "");
    if (keep < items.size())
      list += (keep ? ", +" : "+") + std::to_string(items.size() - keep) + " more";
    std::string s = head + list + (list.empty() && !after.empty() ? after.substr(2) : after);
    if (s.size() <= STATUS_MAX || !keep)
      return s.substr(0, STATUS_MAX);
  }
}

inline void status(const std::string &s) { panel::status(id(panel_animation_status), s); }

inline bool opaque(uint32_t c) { return c >> 24 >= 128; }  // 0xAARRGGBB, at least half opaque

// 0xAARRGGBB of the animation being made. At half size the 2x2 average, by alpha with colours of its own, else
// transparent if 3 or more of the 4 are, an odd width's last column taken twice.
inline uint32_t px(int f, int x, int y) {
  const Source &s = *W.a->src;
  if (!W.a->half)
    return s.pixel(f, x, y);
  bool soft = !s.colours.empty();
  uint32_t r = 0, g = 0, b = 0, al = 0, n = 0;
  for (int dy = 0; dy < 2; dy++)
    for (int dx = 0; dx < 2; dx++) {
      int sx = std::min(2 * x + dx, s.w - 1), sy = 2 * y + dy;
      uint32_t c = sy < s.h ? s.pixel(f, sx, sy) : 0, k = soft ? c >> 24 : 255;
      if (!soft && !opaque(c))
        continue;
      r += (c >> 16 & 255) * k;
      g += (c >> 8 & 255) * k;
      b += (c & 255) * k;
      al += k;
      n++;
    }
  if (!al || (!soft && n < 2))
    return 0;
  return (soft ? al / 4 : 255) << 24 | (r / al) << 16 | (g / al) << 8 | b / al;
}

// The source's own colour nearest c, as they look over anything: each channel by its alpha, and the alpha
inline uint8_t nearest_own(uint32_t c, const uint32_t *pal, int n) {
  auto by_alpha = [](uint32_t v, int at) { return (int) ((v >> at & 255) * (v >> 24) / 255); };
  int best = 0;
  uint32_t bd = UINT32_MAX;
  for (int i = 0; i < n; i++) {
    int dr = by_alpha(c, 16) - by_alpha(pal[i], 16), dg = by_alpha(c, 8) - by_alpha(pal[i], 8),
        db = by_alpha(c, 0) - by_alpha(pal[i], 0), da = (int) (c >> 24) - (int) (pal[i] >> 24);
    uint32_t d = (uint32_t) (2 * dr * dr + 4 * dg * dg + 3 * db * db + 3 * da * da);
    if (d < bd) {
      bd = d;
      best = i;
    }
  }
  return (uint8_t) best;
}

// The colours the panel draws none of, at an opacity that rounds to 0 of its 15ths
inline uint16_t clear_colours(const uint32_t *pal) {
  uint16_t m = 0;
  for (int i = 0; i < 16; i++)
    m |= (uint16_t) (((pal[i] >> 24) * 15 + 127) / 255 == 0) << i;
  return m;
}

// Frees what count() and make() work in
inline void drop_work() {
  drop(W.hist);
  drop(W.hash);
  drop(W.lut);
  drop(W.frame);
  drop(W.prev);
  drop(W.first);
  drop(W.enc);
  drop(W.tok);
  drop(W.rest);
  drop(W.sheet);
}

// Stops making or sending the one in hand. What was made of one being made goes.
inline void stop() {
  under_way.clear();
  if (W.phase == COUNT || W.phase == MAKE)
    drop(W.a->data);
  W.t.head = false;
  W.phase = IDLE;
  W.a = nullptr;
  drop_work();
}

// Gives up on the one in hand with the reason in the status, always false. A lasting failure isn't tried again
// after a hello.
inline bool fail(const std::string &why, bool lasting = false) {
  Overlay *a = W.a;
  stop();
  drop(a->data);
  a->why = why;
  a->state = FAILED;
  a->lasting = lasting;
  status(shown());
  return false;
}

// The panel draws 2x at most, so only scale 1 can go to half size
inline bool can_halve() { return !W.a->half && W.a->src->scale == 1; }

inline std::string too_big(size_t n) {
  return (W.a->half ? "too big even at half size (over " : "too big at scale 2 (over ") + std::to_string(n) + " bytes)";
}

// ms in OVERLAY_TICK_MS, 1 at least
inline uint32_t ticks_of(uint32_t ms) { return std::max<uint32_t>(1, (ms + OVERLAY_TICK_MS / 2) / OVERLAY_TICK_MS); }

// The records after a frame's own that make it last ms, OVERLAY_DELAY_MAX each
inline uint32_t more_records(uint32_t ms) {
  return (std::max<uint32_t>(ticks_of(ms), OVERLAY_DELAY_MAX) - 1) / OVERLAY_DELAY_MAX;
}

// The most a frame takes as a still's pieces, as plink_overlay_pieces() counts them
inline size_t pieces_max() {
  size_t row = (size_t) (W.a->w + 1) / 2, pieces = (size_t) (W.a->h + OVERLAY_PIECE_ROWS - 1) / OVERLAY_PIECE_ROWS;
  return pieces * OVERLAY_RECORD_HEAD + W.a->h * (row + row / 128 + 1);
}

// The most its records take while it's made, or those of its frames as a sheet: a record a frame with those that
// make it last, and the one back to the first, but no more than the room there can be and a record over. A still's
// pieces.
inline size_t data_max(bool sheet = false) {
  if (W.a->still)
    return std::min<size_t>(OVERLAY_POOL, pieces_max());
  const Source &s = *W.a->src;
  size_t f = W.a->frame_bytes(), record = OVERLAY_RECORD_HEAD + (sheet ? pieces_max() : PDEFLATE_BOUND(f));
  size_t n = sheet ? 0 : record;
  for (int i = 0; i < s.frames; i++)
    n += record + more_records(frame_ms(s, i)) * OVERLAY_RECORD_HEAD;
  return std::min<size_t>(n, sheet ? OVERLAY_POOL : OVERLAY_POOL - f + record);
}

// What making it takes of the ESP's RAM besides its pixels' own, and the biggest piece of that. First the colour
// counts with the colours that occur. Then, with `counted`, the colour table, three frames, the records and
// pdeflate's work for a frame, in three blocks: a still has one frame and no deflate.
struct Ram {
  size_t bytes, block;
};
inline Ram ram_for(bool counted) {
  const Overlay &a = *W.a;
  size_t f = a.frame_bytes(), data = data_max(), px = (size_t) a.src->frames * a.w * a.h;
  if (!counted)  // W.hist, median_cut()'s bins and W.hash
    return {BINS * 4 + 2 * std::min<size_t>(BINS, px) + 4 * a.src->frames, BINS * 4};
  if (a.still)
    return {BINS + f + data, std::max<size_t>({BINS, data, f})};
  size_t work = PDEFLATE_ENC + PDEFLATE_TOK(f) + PDEFLATE_REST(f), sheet = W.own ? data_max(true) : 0;
  return {BINS + 3 * f + data + sheet + work, std::max<size_t>({BINS, data, PDEFLATE_ENC, PDEFLATE_TOK(f), sheet})};
}
// All of it, from start to end
inline Ram ram_for() {
  Ram count = ram_for(false), make = ram_for(true);
  return {W.a->src->ram + std::max(count.bytes, make.bytes), std::max(count.block, make.block)};
}

// Whether the ESP has that now: all of it before it starts, the rest once it's counted, since a picture or a
// request may have taken some meanwhile. Without exceptions a failed allocation restarts the ESP.
inline bool has_ram(bool counted = false) {
  Ram r = counted ? ram_for(true) : ram_for();
  return panel::has_ram(r.bytes, r.block);
}

// The one in hand waits for the ESP's RAM: it stays wanted, and loop() starts it again after RAM_LOOK_MS. Always
// false.
inline bool short_of_ram() {
  Overlay &a = *W.a;
  if (!a.looks) {
    [[maybe_unused]] Ram r = ram_for();  // only logged, unused when ESP_LOGW compiles out
    ESP_LOGW("panel", "Animation %s: waiting for the RAM to make it, %u KB with %u KB in one piece. The ESP has %s",
             a.name.c_str(), (unsigned) ((r.bytes + ram_spare() + 1023) / 1024), (unsigned) ((r.block + 1023) / 1024),
             ram_left().c_str());
  }
  a.looks += a.looks < UINT8_MAX;
  a.looked = millis();
  stop();
  status(shown());
  return false;
}

// ---- the link's messages

// A glide sent again goes on for the time it had left
inline void send_told(const Copy::Told &t) {
  std::vector<uint8_t> m = t.m;
  if (t.key == K_OVERLAY_GLIDE) {
    uint32_t ms = plink_get16(m.data() + OVERLAY_GLIDE_MS), gone = millis() - t.at;
    msg::put16(m, OVERLAY_GLIDE_MS, (int) (gone < ms ? ms - gone : 0));
  }
  link::send(t.key, m.data(), (int) m.size());
}

// What the panel has bound at a time, of its OVERLAY_BINDS: what the copies were told and their scripts bind
inline int bound() {
  int n = 0;
  for (auto &a : overlays)
    for (auto &c : a.copies) {
      n += c.used ? c.script.binds + c.script.gives_binds : 0;
      for (auto &t : c.told)
        n += t.key == K_OVERLAY_BIND;
    }
  return n;
}

// Sends m to copy c of a with its place filled in, now if the panel has the copy and again after a restart. Takes
// the place of the earlier ones of key it replaces, see msg::replaces(). A BIND that frees is only sent: a
// restarted panel has nothing bound there either.
inline void tell(Overlay &a, Copy &c, uint16_t key, std::vector<uint8_t> m) {
  m[OVERLAY_MSG_PLACE] = c.place;
  c.told.erase(std::remove_if(c.told.begin(), c.told.end(),
                              [&](const Copy::Told &t) { return t.key == key && msg::replaces(key, m, t.m); }),
               c.told.end());
  bool binds = key == K_OVERLAY_BIND && !msg::frees(m);
  if (binds && bound() >= OVERLAY_BINDS) {
    ESP_LOGW("panel", "Animation %s: the panel binds %d at a time", a.name.c_str(), OVERLAY_BINDS);
    return;
  }
  Copy::Told t{key, millis(), std::move(m)};
  if (a.state == SENT && c.placed)
    send_told(t);
  if (key != K_OVERLAY_BIND || binds)
    c.told.push_back(std::move(t));
}

// Copy c of a told what statement says, the script statements that tell a copy something, see overlay_script.h.
// False with the reason in error if it can't be.
inline bool tell_text(Overlay &a, Copy &c, const std::string &statement, std::string &error) {
  Program p = compile(statement, error, a.src->clips);
  if (error.empty() && !p.bytes.empty())
    error = "it can't be told, it's a script";
  if (!error.empty())
    return false;
  for (auto &t : p.told)
    tell(a, c, t.key, std::move(t.m));
  return true;
}

inline void send_frame_rate(const Overlay &a) {
  uint8_t m[OVERLAY_FRAMES_LEN];
  m[OVERLAY_FRAMES_SLOT] = (uint8_t) (&a - overlays);
  plink_put16(m + OVERLAY_FRAMES_RATE, a.frame_rate);
  link::send(K_OVERLAY_FRAMES, m, sizeof m);
}

// Whether the panel's pool has room for a frame of a's own, once c's own one, if it has one there, is gone.
// Warns if not.
inline bool own_room(const Overlay &a, const Copy *c) {
  if (used_by_others(nullptr) + a.panel_frame() - (c && c->placed && c->own ? a.panel_frame() : 0) <= OVERLAY_POOL)
    return true;
  ESP_LOGW("panel", "Animation %s: no room in the panel's %u bytes for a copy with a frame of its own", a.name.c_str(),
           (unsigned) OVERLAY_POOL);
  return false;
}

inline int fade_of(const Overlay &a, const Copy &c) { return c.fade >= 0 ? c.fade : a.src->fade_ms; }

inline void send_depth(const Copy &c) {
  std::vector<uint8_t> m = msg::glide({{OVERLAY_DEPTH, c.depth}}, 0, 0);
  m[OVERLAY_MSG_PLACE] = c.place;
  link::send(K_OVERLAY_GLIDE, m.data(), (int) m.size());
}

// The place copy i of the animation called name has, for a script that looks at it or tells it something. Nobody's
// if it isn't shown, so the script sees nothing there and tells no one.
inline uint8_t place_of(const std::string &name, int i) {
  for (auto &a : overlays)
    if (a.name == name && i >= 0 && i < (int) a.copies.size() && a.copies[i].used)
      return a.copies[i].place;
  return OVERLAY_NOBODY;
}

// Copy c's script to the panel, the places it looks at, its END's tag and fade filled in
inline void send_script(const Overlay &a, const Copy &c) {
  std::vector<uint8_t> b = c.script.bytes;
  for (auto &p : c.script.patches) {
    if (p.kind == Program::PLACE)
      b[p.at] = place_of(p.name, p.copy);
    else if (p.kind == Program::TAG)
      b[p.at] = c.tag;
    else
      plink_put32(&b[p.at], (uint32_t) fade_of(a, c));
  }
  if (used_by_others(nullptr) + (c.placed ? 0 : pool_bytes(c) + (c.own ? a.panel_frame() : 0)) > OVERLAY_POOL) {
    ESP_LOGW("panel", "Animation %s: no room in the panel's %u bytes for copy %d's script", a.name.c_str(),
             (unsigned) OVERLAY_POOL, (int) (&c - a.copies.data()));
  }
  size_t at = 0;
  do {
    uint8_t m[PBULK_MSG_MAX] = {c.place};
    plink_put16(m + OVERLAY_SCRIPT_AT, (uint16_t) at);
    plink_put16(m + OVERLAY_SCRIPT_LEN, (uint16_t) b.size());
    m[OVERLAY_SCRIPT_REGS] = (uint8_t) c.script.regs;
    size_t n = std::min(b.size() - at, sizeof m - OVERLAY_SCRIPT_BYTES);
    std::copy_n(b.begin() + (long) at, n, m + OVERLAY_SCRIPT_BYTES);
    link::send(K_OVERLAY_SCRIPT, m, (int) (OVERLAY_SCRIPT_BYTES + n));
    at += n;
  } while (at < b.size());
}

// Puts a copy on the panel with what it was told
inline void send_place(Overlay &a, Copy &c) {
  uint8_t m[OVERLAY_PLACE_LEN];
  m[OVERLAY_MSG_PLACE] = c.place;
  m[OVERLAY_PLACE_SLOT] = (uint8_t) (&a - overlays);
  // one on the screen stays on it, the sizes round up at half size
  const Source &s = *a.src;
  bool in = c.x >= 0 && c.y >= 0 && c.x + s.w * s.scale <= PIC_MAX_W && c.y + s.h * s.scale <= PIC_MAX_H;
  plink_put16(m + OVERLAY_PLACE_X, (uint16_t) (in ? std::min<int>(c.x, PIC_MAX_W - a.w * a.scale) : c.x));
  plink_put16(m + OVERLAY_PLACE_Y, (uint16_t) (in ? std::min<int>(c.y, PIC_MAX_H - a.h * a.scale) : c.y));
  m[OVERLAY_PLACE_OWN] = c.own;
  plink_put16(m + OVERLAY_PLACE_FADE, (uint16_t) fade_of(a, c));
  link::send(K_OVERLAY_PLACE, m, sizeof m);
  if (c.depth != OVERLAY_NEAREST)
    send_depth(c);
  for (auto &t : c.told)
    send_told(t);
  if (!c.script.bytes.empty())
    send_script(a, c);
  c.placed = true;
}

// A slot, OVERLAY_PLACE_BIT | a place, or OVERLAY_ALL, faded out over ms first
inline void send_clear(unsigned what, int ms = 0) { link::send(K_OVERLAY_CLEAR, (uint32_t) what | (uint32_t) ms << 8); }

// ---- making one

inline void made(bool ok, size_t left, size_t need);

// A bit for each of pal that's one of colours, 0xRRGGBB
inline uint16_t bits_of(const uint32_t *pal, const std::vector<uint32_t> &colours) {
  uint16_t bits = 0;
  for (int i = 0; i < 16; i++)
    bits |= (uint16_t) ((pal[i] >> 24 && std::find(colours.begin(), colours.end(), pal[i] & 0xFFFFFF) != colours.end())
                        << i);
  return bits;
}

// False if it doesn't fit on the screen or next to the others, or it waits for the ESP's RAM. Straight to half size
// if a frame alone is over what's left and it can, or the ESP has had the RAM for no more through RAM_LOOKS. A field
// is sent as it is.
inline bool start() {
  Overlay &a = *W.a;
  const Source &s = *a.src;
  if (s.scale != 1 && s.scale != 2)
    return fail("scale has to be 1 or 2", true);
  if (s.frames < 1 || s.x < 0 || s.y < 0 || s.x + s.w * s.scale > PIC_MAX_W || s.y + s.h * s.scale > PIC_MAX_H)
    return fail("doesn't fit on the screen at " + std::to_string(s.x) + "," + std::to_string(s.y), true);
  size_t left = room();
  a.field = s.field >= 0;
  if (a.field) {  // nothing to make: its colours, and what it is
    a.scale = s.scale;
    a.w = s.w;
    a.h = s.h;
    a.still = true;
    a.sheet = false;
    a.half = true;  // as small as it gets
    std::fill_n(a.pal, 16, 0);
    std::copy_n(s.colours.begin(), std::min<size_t>(s.colours.size(), 16), a.pal);
    a.glows = bits_of(a.pal, s.glows);
    a.cycles = 0;
    a.data = {(uint8_t) s.field, (uint8_t) s.field_a, (uint8_t) s.field_b, (uint8_t) (s.field_b >> 8)};
    made(true, left, a.data.size());
    return W.a == &a;
  }
  a.sheet = false;
  W.own = 0;
  for (const Copy &c : a.copies)
    W.own += c.used && c.own;
  bool was_half = a.half, short_of = false;  // short_of: the RAM for it as it was
  for (;;) {
    a.scale = s.scale * (a.half ? 2 : 1);
    a.w = a.half ? (s.w + 1) / 2 : s.w;
    a.h = a.half ? (s.h + 1) / 2 : s.h;
    a.still = s.frames == 1;
    bool fits = a.panel_frame() <= left;
    if (fits && has_ram())
      break;
    if (!fits && !can_halve())
      return fail(too_big(a.frame_bytes()), true);
    if (fits && (a.looks < RAM_LOOKS || !can_halve())) {
      a.half = was_half;
      return short_of_ram();
    }
    short_of |= fits;
    a.half = true;
  }
  if (short_of) {
    ESP_LOGW("panel", "Animation %s: the ESP has the RAM to make it at half size only", a.name.c_str());
  }
  a.looks = 0;
  W.hist.assign(BINS, 0);
  W.hash.clear();
  W.sum = FNV_START;
  W.any_transp = false;
  W.f = W.y = 0;
  W.phase = COUNT;
  under_way = "making " + label(a);
  status(shown());
  return true;
}

// Counts the colours of a row or a few, and at the end of a frame its hash
inline void count() {
  Overlay &a = *W.a;
  uint32_t h = W.sum;
  for (int n = 0; n < ROWS_PX && W.y < a.h; n += a.w, W.y++)
    for (int x = 0; x < a.w; x++) {
      uint32_t c = px(W.f, x, W.y);
      h = (h ^ c) * FNV_PRIME;
      if (opaque(c))
        W.hist[bin(c)]++;
      else
        W.any_transp = true;
    }
  W.sum = h;
  if (W.y < a.h)
    return;
  W.y = 0;
  W.hash.push_back(W.sum);
  W.sum = FNV_START;
  W.f++;
}

// After the last frame is counted: the palette, at once, and the RAM to make the frames
inline void palette() {
  Overlay &a = *W.a;
  int n = a.src->frames;
  W.frames = n;
  const std::vector<uint16_t> &d = a.src->delays;
  for (int p = 1; p < n && W.frames == n; p++)
    if (n % p == 0 && std::equal(W.hash.begin() + p, W.hash.end(), W.hash.begin()) &&
        (d.empty() || std::equal(d.begin() + p, d.end(), d.begin())))
      W.frames = p;
  drop(W.hash);
  a.still = W.frames == 1;
  std::vector<uint16_t> bins;
  memset(a.pal, 0, sizeof a.pal);
  const std::vector<uint32_t> &own = a.src->colours;
  if (!own.empty()) {
    W.colours = (int) std::min<size_t>(own.size(), 16);
    std::copy_n(own.begin(), W.colours, a.pal);
  } else {
    W.colours = median_cut(W.hist, W.any_transp ? 15 : 16, a.pal, bins);
    for (int i = 0; i < W.colours; i++)
      a.pal[i] |= 0xFF000000u;  // and 15 clear if any pixel is
  }
  a.glows = 0;  // as make() meets them
  a.cycles = bits_of(a.pal, a.src->cycles);
  drop(W.hist);
  drop(bins);
  if (!has_ram(true)) {
    short_of_ram();
    return;
  }
  W.lut.assign(BINS, 0xFF);  // filled as make() meets the colours
  a.data.clear();
  a.data.reserve(data_max());  // so it never moves
  W.sheet.clear();
  if (W.own && !a.still)
    W.sheet.reserve(data_max(true));
  W.frame.resize(a.frame_bytes());
  if (!a.still) {
    W.prev.resize(a.frame_bytes());
    W.enc.resize(PDEFLATE_ENC / 4);
    W.tok.resize(PDEFLATE_TOK(a.frame_bytes()) / 4);
    W.rest.resize((PDEFLATE_REST(a.frame_bytes()) + 1) / 2);
  }
  W.f = 0;
  W.record_us = UINT32_MAX;
  W.phase = MAKE;
}

// An empty record that only waits. x says what for: OVERLAY_RECORD_MORE or OVERLAY_RECORD_FRAME.
inline void waits(std::vector<uint8_t> &d, uint8_t delay, uint8_t x) {
  d.resize(d.size() + OVERLAY_RECORD_HEAD);
  plink_overlay_head(d.data() + d.size() - OVERLAY_RECORD_HEAD, delay, x, 0, 0, 0, 0);
}

// Records after one that waits its delay, so it waits ticks more, OVERLAY_DELAY_MAX each. False if they don't fit d's
// capacity.
inline bool longer(std::vector<uint8_t> &d, uint32_t ticks) {
  for (uint8_t delay; ticks; ticks -= delay) {
    delay = (uint8_t) std::min<uint32_t>(ticks, OVERLAY_DELAY_MAX);
    if (d.size() + OVERLAY_RECORD_HEAD > d.capacity())
      return false;
    waits(d, delay, OVERLAY_RECORD_MORE);
  }
  return true;
}

// Appends the record that turns `from` into `to`, or the whole frame if from is null, then waits ms: a frame of the
// panel's for each of the source's, so a script's frame numbers are the source's. A record waits MAX_FRAME_MS at
// most, the rest goes in OVERLAY_RECORD_MORE records after it. The record back to the first frame waits as long as
// the first frame's own, the first frame's MORE ones after that are in the loop. False without the memory to
// compress it. Stops where data would have to grow, which only happens past the pool, where made() turns it down
// anyway.
inline bool record(const std::vector<uint8_t> *from, const std::vector<uint8_t> &to, uint32_t ms, bool back = false) {
  Overlay &a = *W.a;
  const size_t most = OVERLAY_RECORD_HEAD + PDEFLATE_BOUND(a.frame_bytes());  // what a record takes while it's made
  uint32_t ticks = ticks_of(ms);
  uint8_t delay = (uint8_t) std::min<uint32_t>(ticks, OVERLAY_DELAY_MAX);
  size_t at = a.data.size();
  if (at + most > a.data.capacity())
    return true;  // it would move, a peak has_ram() didn't count
  a.data.resize(at + most);
  pdeflate_work_t work = {W.enc.data(), W.tok.data(), W.rest.data()};
  unsigned n =
      plink_overlay_record_in(&work, from ? from->data() : nullptr, to.data(), a.w, a.h, delay, a.data.data() + at);
  a.data.resize(at + n);
  if (!n)
    return false;
  if (!back)
    longer(a.data, ticks - delay);
  return true;
}

// Frame W.f on the sheet: the record that starts it, its pieces, and those that make it last ms. The sheet goes if
// it would grow past the pool.
inline void sheet_frame(uint32_t ms) {
  Overlay &a = *W.a;
  std::vector<uint8_t> &d = W.sheet;
  uint32_t ticks = ticks_of(ms), at = (uint32_t) d.size() + OVERLAY_RECORD_HEAD, n = 0;
  uint8_t delay = (uint8_t) std::min<uint32_t>(ticks, OVERLAY_DELAY_MAX);
  if (at <= d.capacity()) {
    waits(d, delay, OVERLAY_RECORD_FRAME);
    d.resize(d.capacity());
    n = plink_overlay_pieces(W.frame.data(), a.w, a.h, clear_colours(a.pal), 0, d.data() + at,
                             (uint32_t) d.size() - at);
    d.resize(at + n);
  }
  if (!n || !longer(d, ticks - delay)) {
    drop(d);
    W.own = 0;
  }
}

// Made again at half size
inline void halve() {
  W.t.head = false;
  drop(W.a->data);
  W.a->half = true;
  start();
}

// For make(): all frames are in, or what they need is over the room left or there was no memory to compress them.
// Then it's sent, or made again at half size if it can.
inline void made(bool ok, size_t left, size_t need) {
  Overlay &a = *W.a;
  drop_work();
  if (!ok || need > left) {
    if (!can_halve()) {
      fail(ok ? too_big(need) : "no memory to compress it", ok);
      return;
    }
    if (ok) {
      ESP_LOGI("panel", "Animation %s: over %u bytes after %d of %d frames, making it at half size", a.name.c_str(),
               (unsigned) left, W.f, W.frames);
    } else {
      ESP_LOGW("panel", "Animation %s: no memory to compress it, making it at half size", a.name.c_str());
    }
    halve();
    return;
  }
  a.data.shrink_to_fit();
  ESP_LOGI("panel", "Animation %s: %dx%d%s, %s%s, %u bytes", a.name.c_str(), a.w, a.h,
           a.half && !a.field ? " drawn 2x" : "",
           a.field   ? "a field"
           : a.still ? "still"
                     : (std::to_string(W.frames) + (a.sheet ? " frames as a sheet" : " frames")).c_str(),
           !a.field && W.frames < a.src->frames ? " (the rest repeat them)" : "", (unsigned) need);
  W.t.reset();
  W.phase = SEND;
  under_way = "sending " + label(a);
  status(shown());
}

// Maps a row of the frame or a few to the palette. Once the frame is in, adds its record, and after the last frame
// the one back to the first. A still's pieces instead. A record goes in one go, so it waits for the next slice
// unless this one is fresh or has the time the last record took. False when it waits.
inline bool make(uint32_t deadline_us, bool fresh) {
  Overlay &a = *W.a;
  if (W.y < a.h) {
    bool own = !a.src->colours.empty();
    uint8_t *lut = W.lut.data();
    const std::vector<uint32_t> &glows = a.src->glows;
    for (int n = 0; n < ROWS_PX && W.y < a.h; n += a.w, W.y++) {
      uint8_t *row = W.frame.data() + (size_t) W.y * ((a.w + 1) / 2);
      for (int x = 0; x < a.w; x++) {
        uint32_t c = px(W.f, x, W.y);
        uint8_t i = own ? nearest_own(c, a.pal, W.colours) : opaque(c) ? lut_index(lut, c, a.pal, W.colours) : 15;
        if (opaque(c) && std::find(glows.begin(), glows.end(), c & 0xFFFFFF) != glows.end())
          a.glows |= (uint16_t) (1u << i);
        row[x / 2] = (uint8_t) (x & 1 ? row[x / 2] | i : i << 4);
      }
      if (a.w & 1)  // the half byte past an odd width, never drawn, as the last so it packs with it
        row[a.w / 2] |= row[a.w / 2] >> 4;
    }
    return true;
  }
  if (!fits(deadline_us, W.record_us, fresh))
    return false;
  W.y = 0;
  size_t left = room();
  if (a.still) {
    size_t cap = std::min(left, data_max());
    a.data.resize(cap);
    uint8_t delay = (uint8_t) std::min<uint32_t>(ticks_of(frame_ms(*a.src, 0)), OVERLAY_DELAY_MAX);
    uint32_t n =
        plink_overlay_pieces(W.frame.data(), a.w, a.h, clear_colours(a.pal), delay, a.data.data(), (uint32_t) cap);
    a.data.resize(n);
    W.f = 1;
    made(true, left, n ? n : left + 1);
    return true;
  }
  uint32_t t0 = micros();
  if (W.own)
    sheet_frame(frame_ms(*a.src, W.f));
  bool ok = record(W.f ? &W.prev : nullptr, W.frame, frame_ms(*a.src, W.f));
  if (!W.f)
    W.first = W.frame;
  W.prev.swap(W.frame);
  bool last = ++W.f >= W.frames, over = a.frame_bytes() + a.data.size() > left;
  if (ok && last && W.f > 1 && !over)
    ok = record(&W.prev, W.first, frame_ms(*a.src, 0), true);
  over = a.frame_bytes() + a.data.size() > left;
  // As a sheet when its copies with frames of their own make that smaller, or it alone fits
  bool sheet =
      W.own && W.sheet.size() <= left && (over || W.sheet.size() < (W.own + 1) * a.frame_bytes() + a.data.size());
  if (!ok || last || (over && !sheet)) {
    if (ok && last && sheet) {
      a.data.swap(W.sheet);
      a.sheet = true;
    }
    made(ok, left, a.panel_frame() + a.data.size());
  }
  uint32_t us = micros() - t0;  // a frame that changed little goes quicker, the next may not
  W.record_us = W.record_us == UINT32_MAX ? us : std::max(us, W.record_us / 4 * 3);
  return true;
}

// The header once the link is free, then the records as it has room until deadline_us, then END
inline void send(uint32_t deadline_us) {
  Overlay &a = *W.a;
  Transfer::Pumped p = W.t.pump(deadline_us, [&a] {
    W.t.open(a.data.data(), (uint32_t) a.data.size());
    uint8_t h[OVERLAY_BEGIN_LEN];
    link::send(K_OVERLAY_BEGIN, h,
               plink_overlay_begin(h, W.t.id, (uint8_t) (&a - overlays),
                                   a.field   ? OVERLAY_FIELD
                                   : a.still ? OVERLAY_STILL
                                   : a.sheet ? OVERLAY_SHEET
                                             : OVERLAY_ANIMATED,
                                   (uint16_t) a.w, (uint16_t) a.h, (uint8_t) a.scale, (uint32_t) a.data.size(), a.pal,
                                   a.glows, a.src->shade, a.cycles));
  });
  if (p == Transfer::AGAIN) {  // the panel lost it or its answer
    ESP_LOGW("panel", "Animation %s: no answer from the panel, sending it again", a.name.c_str());
  } else if (p == Transfer::GAVE_UP) {
    fail("the panel didn't answer");
  }
}

// The panel has it: its copies go on
inline void done() {
  Overlay &a = *W.a;
  under_way.clear();
  W.phase = IDLE;
  W.t.reset();
  W.a = nullptr;
  a.state = SENT;
  if (a.frame_rate != 100)
    send_frame_rate(a);
  for (auto &c : a.copies) {
    if (c.used && c.own && !own_room(a, &c))
      c = Copy();
    if (c.used)
      send_place(a, c);
  }
  status(shown());
}

// The panel's K_PIC_ACK = status << 24 | id << 16 | bytes received, see plink_keys.h
inline void ack(uint32_t v) {
  if (W.phase != SEND || !W.t.answers(v))
    return;  // for a picture or an earlier transfer
  Overlay &a = *W.a;
  uint8_t st = v >> 24;
  if (st == PIC_PROGRESS) {
    under_way = "sending " + label(a) + " " + std::to_string(percent(v, (uint32_t) a.data.size())) + " %";
    status(shown());
  } else if (st == PIC_DONE) {
    done();
  } else if (st == PIC_TOO_BIG && can_halve()) {
    halve();
  } else if (!W.t.refused(st)) {
    fail("the panel didn't take it (" + std::to_string(st) + ")");
  }
}

// To be sent again, and made again from scratch if it failed
inline void again(Overlay &a) {
  if (a.state == FAILED)
    a.half = false;
  a.state = WANT;
  for (auto &c : a.copies)
    c.placed = false;
}

// After a hello the panel has none of them: it restarted, or cleared them for our hello. They all go again, but
// for failures that would happen again.
inline void restarted() {
  stop();
  for (auto &a : overlays)
    if (!(a.state == FAILED && a.lasting))
      again(a);
}

// ---- what to show

// A free place on the panel, -1 with a warning about name's copy if all are taken
inline int free_place(const std::string &name) {
  for (int p = 0; p < OVERLAY_PLACES; p++) {
    bool taken = false;
    for (auto &a : overlays)
      for (auto &c : a.copies)
        taken |= c.used && c.place == p;
    if (!taken)
      return p;
  }
  ESP_LOGW("panel", "Animation %s: the panel shows %d copies at most", name.c_str(), OVERLAY_PLACES);
  return -1;
}

// Copy i of a, made if it wasn't there. nullptr if the panel has no place left for it.
inline Copy *copy_of(Overlay &a, int i) {
  if ((int) a.copies.size() <= i)
    a.copies.resize(i + 1);
  Copy &c = a.copies[i];
  if (!c.used) {
    int p = free_place(a.name);
    if (p < 0)
      return nullptr;
    c = Copy();
    c.used = true;
    c.place = (uint8_t) p;
  }
  return &c;
}

inline const Source *source(const std::string &name) {
  auto it = sources.find(name);
  if (it != sources.end())
    return &it->second;
  ESP_LOGW("panel", "No animation called '%s'", name.c_str());
  return nullptr;
}

// The slot for name, taken if it hasn't one, and tried again if it failed. nullptr if there's no such animation, or
// no slot or place left for it.
inline Overlay *want(const std::string &name) {
  if (Overlay *a = find(name)) {
    if (a->state == FAILED)
      again(*a);
    return a;
  }
  const Source *s = source(name);
  if (!s)
    return nullptr;
  Overlay *a = nullptr;
  for (auto &o : overlays)
    if (o.name.empty() && !a)
      a = &o;
  if (!a) {
    ESP_LOGW("panel", "No room for animation %s: the panel holds %d at a time", name.c_str(), OVERLAY_SLOTS);
    return nullptr;
  }
  if (free_place(name) < 0)
    return nullptr;
  *a = Overlay();
  a->name = name;
  a->src = s;
  if (s->on_shown)
    s->on_shown(true);
  return a;
}

inline void free_slot(Overlay &a, bool send = true) {
  if (send && (a.state == SENT || (W.a == &a && W.t.head)))
    send_clear((unsigned) (&a - overlays), a.src->fade_ms);
  if (W.a == &a)
    stop();
  if (a.src->on_shown)
    a.src->on_shown(false);
  a = Overlay();
}

// Clears one animation and all its copies, or all of them with no name, fading them out
inline void clear(const std::string &name) {
  if (name.empty() || name == "None") {
    int ms = 0;
    for (auto &a : overlays)
      if (!a.name.empty()) {
        ms = std::max(ms, a.src->fade_ms);
        free_slot(a, false);
      }
    send_clear(OVERLAY_ALL, ms);
  } else if (Overlay *a = find(name)) {
    free_slot(*a);
  }
  status(shown());
}

inline uint8_t tags;  // so an old OVERLAY_GONE doesn't take a copy placed again

// Copy c runs p from now on, the one it ran before gone, told what p sends before it does anything else
inline void set_script(Overlay &a, Copy &c, Program p) {
  for (auto &t : p.told)
    tell(a, c, t.key, std::move(t.m));
  p.told.clear();
  c.script = std::move(p);
  c.tag = 0;
  if (c.script.binds && bound() > OVERLAY_BINDS) {
    ESP_LOGW("panel", "Animation %s: its script binds more than the panel's %d at a time", a.name.c_str(),
             OVERLAY_BINDS);
  }
  if (c.script.ends() && !(c.tag = ++tags))
    c.tag = ++tags;  // 0 is none
  if (a.state == SENT && c.placed)
    send_script(a, c);
}

// panel_show_animation and the switches: shows it where its yaml puts it, alongside the others, and copy 0 there
// with its drift and its script again if it went. A failed one is tried again.
inline void show(const std::string &name) {
  Overlay *a = want(name);
  if (!a)
    return;
  Copy *c = !a->copies.empty() && a->copies[0].used ? nullptr : copy_of(*a, 0);
  if (c) {
    const Source &s = *a->src;
    c->x = (int16_t) s.x;
    c->y = (int16_t) s.y;
    c->own = s.script.frames;  // its script plays them
    std::string error;
    if ((s.vx || s.vy) && tell_text(*a, *c, "drift " + std::to_string(s.vx) + ", " + std::to_string(s.vy), error))
      c->doing = ", drifting";
    set_script(*a, *c, s.script);
    if (a->state == SENT)
      send_place(*a, *c);
  }
  status(shown());
}

// An animated GIF from animation.yaml and its switch, shown if the switch was restored on. clips: "sail 0-3, tack
// 4-8", script: what copy 0 runs, see overlay_script.h.
template<typename Img, typename Sw>
void add_gif(const std::string &name, Img &&image, Sw &&sw, int frame_ms, int x, int y, int scale, int vx, int vy,
             int fade_ms, const std::string &clips = "", const std::string &script = "") {
  auto *img = ptr_of(image);
  auto *s = ptr_of(sw);
  Source src{img->get_width(),
             img->get_height(),
             (int) img->get_animation_frame_count(),
             frame_ms,
             x,
             y,
             scale,
             [img](int f, int px, int py) -> uint32_t {
               img->set_frame(f);
               auto c = img->get_pixel(px, py);
               return (uint32_t) c.w << 24 | (uint32_t) c.r << 16 | (uint32_t) c.g << 8 | c.b;
             },
             vx,
             vy,
             [s](bool on) { s->publish_state(on); },
             clamp_u16(fade_ms)};
  std::string error;
  src.clips = parse_clips(clips, error);
  if (error.empty())
    src.script = compile(script, error, src.clips);
  if (!error.empty()) {
    ESP_LOGW("panel", "Animation %s: %s", name.c_str(), error.c_str());
  }
  add(name, std::move(src));
  if (s->state)
    show(name);
}

// "00FFF4D8, FFFFF4D8" as the 16 colours a field goes along: 2 to 16 of them in hex, AARRGGBB or an opaque RRGGBB,
// spread evenly with those between worked out. The reason in error if they aren't.
inline std::vector<uint32_t> ramp(const std::string &stops, std::string &error) {
  std::vector<uint32_t> at;
  error.clear();
  for (size_t k = 0; k <= stops.size();) {
    size_t e = std::min(stops.find(',', k), stops.size()), from = stops.find_first_not_of(" \t\n#", k);
    std::string hex = from < e ? stops.substr(from, stops.find_last_not_of(" \t\n", e - 1) + 1 - from) : "";
    bool is =
        (hex.size() == 6 || hex.size() == 8) && std::all_of(hex.begin(), hex.end(), [](char c) { return isxdigit(c); });
    if (!is || at.size() == 16) {
      error = is ? "16 colours at most" : "'" + hex + "' isn't a colour like FFF4D8 or 80FFF4D8";
      return {};
    }
    at.push_back((hex.size() == 6 ? 0xFF000000u : 0) | (uint32_t) strtoul(hex.c_str(), nullptr, 16));
    k = e + 1;
  }
  if (at.size() < 2) {
    error = "a field goes along 2 colours or more";
    return {};
  }
  std::vector<uint32_t> out(16);
  for (size_t i = 0; i < 16; i++) {
    float f = (float) (i * (at.size() - 1)) / 15;
    size_t j = std::min((size_t) f, at.size() - 2);
    for (int b = 0; b < 32; b += 8) {
      float lo = (float) (at[j] >> b & 255), hi = (float) (at[j + 1] >> b & 255);
      out[i] |= (uint32_t) lroundf(lo + (hi - lo) * (f - (float) j)) << b;
    }
  }
  return out;
}

// A field from field.yaml and its switch, shown if the switch was restored on: kind linear, radial, cone or noise,
// w x h at x, y, along colours, see ramp(). script: what copy 0 runs, see overlay_script.h.
template<typename Sw>
void add_field(const std::string &name, Sw &&sw, const std::string &kind, int w, int h, int x, int y, bool mirror,
               bool down, int apex, int layers, int grain, const std::string &colours, int fade_ms,
               const std::string &script) {
  static constexpr const char *KINDS[] = {"linear", "radial", "cone", "noise"};  // OVERLAY_FIELD_*
  auto *s = ptr_of(sw);
  std::string error;
  int k = panel::find(KINDS, kind);
  std::vector<uint32_t> cols = ramp(colours, error);
  if (k < 0)
    error = "'" + kind + "' isn't one of linear, radial, cone, noise";
  int a = k == OVERLAY_FIELD_LINEAR ? (mirror ? OVERLAY_LINEAR_MIRROR : 0) | (down ? OVERLAY_LINEAR_DOWN : 0)
          : k == OVERLAY_FIELD_CONE ? std::clamp(apex, 0, 100) * 255 / 100
                                    : std::clamp(layers, 1, OVERLAY_NOISE_LAYERS);
  Source src = field(w, h, k, a, std::clamp(grain, OVERLAY_NOISE_MIN, OVERLAY_NOISE_MAX), std::move(cols));
  src.x = x;
  src.y = y;
  src.on_shown = [s](bool on) { s->publish_state(on); };
  src.fade_ms = clamp_u16(fade_ms);
  if (error.empty())
    src.script = compile(script, error);
  if (!error.empty()) {
    ESP_LOGW("panel", "Animation %s: %s", name.c_str(), error.c_str());
  }
  if (k < 0 || src.colours.empty())
    return;
  add(name, std::move(src));
  if (s->state)
    show(name);
}

// animation.yaml's switch turned on. If that's before add_gif(), add_gif() shows it.
inline void switched_on(const std::string &name) {
  if (sources.count(name))
    show(name);
}

// The script of start frames in, for a copy with a frame of its own
inline Program started(int start) {
  std::string error;
  return start > 0 ? compile("frame " + std::to_string(start), error) : Program();
}

// Copy i of name at x, y, which can be off the screen, running p, with a frame of its own if own or p plays frames.
// Shows the animation if it isn't yet, a failed one is tried again. fade: ms it fades in and out, -1 for the
// animation's. depth: its OVERLAY_DEPTH. False if it isn't placed.
inline bool place(const std::string &name, int i, int x, int y, Program p, bool own = false, int fade = -1,
                  uint8_t depth = OVERLAY_NEAREST) {
  if (i < 0 || i >= OVERLAY_PLACES) {
    ESP_LOGW("panel", "Animation %s: copy %d isn't one of 0 to %d", name.c_str(), i, OVERLAY_PLACES - 1);
    return false;
  }
  Overlay *a = want(name);
  if (!a)
    return false;
  own |= p.frames;
  if (own && a->state == SENT && !own_room(*a, i < (int) a->copies.size() ? &a->copies[i] : nullptr))
    return false;
  Copy *c = copy_of(*a, i);
  if (!c)
    return false;
  c->x = clamp16(x);
  c->y = clamp16(y);
  c->own = own;
  c->fade = std::min(65535, fade);
  c->depth = depth;
  c->doing.clear();
  c->told.clear();
  c->placed = false;
  set_script(*a, *c, std::move(p));
  if (a->state == SENT)
    send_place(*a, *c);
  status(shown());
  return true;
}
// panel_place_animation: the same with own a frame of its own started start frames in
inline bool place(const std::string &name, int i, int x, int y, bool own, int start, int fade = -1,
                  uint8_t depth = OVERLAY_NEAREST) {
  return place(name, i, x, y, own ? started(start) : Program(), own, fade, depth);
}

// Copy i of name's animation and the animation, nulls if it isn't shown. what is the action, panel_<what>_animation,
// for the warning, none without it.
inline std::pair<Overlay *, Copy *> copy_for(const std::string &name, int i, const char *what = nullptr) {
  Overlay *a = find(name);
  if (a && i >= 0 && i < (int) a->copies.size() && a->copies[i].used)
    return {a, &a->copies[i]};
  if (what) {
    ESP_LOGW("panel", "panel_%s_animation: no copy %d of animation '%s' shown", what, i, name.c_str());
  }
  return {};
}

// panel_remove_animation: copy i of name goes, fading out over fade ms, -1 for its own. The animation stays on
// the panel.
inline void remove(const std::string &name, int i, int fade = -1) {
  auto [a, c] = copy_for(name, i);
  if (!c)
    return;
  if (a->state == SENT && c->placed)
    send_clear(OVERLAY_PLACE_BIT | c->place, fade >= 0 ? std::min(65535, fade) : fade_of(*a, *c));
  *c = Copy();
  status(shown());
}

// Copy i of name goes to depth, its OVERLAY_DEPTH
inline void depth(const std::string &name, int i, uint8_t d) {
  auto [a, c] = copy_for(name, i);
  if (!c || c->depth == d)
    return;
  c->depth = d;
  if (a->state == SENT && c->placed)
    send_depth(*c);
}

// Which of names s is, in any case, or -1 with a warning from panel_<what>_animation
template<size_t N> int option(const char *const (&names)[N], const std::string &s, const char *what) {
  int i = panel::find(names, s);
  if (i < 0) {
    std::string all;
    for (size_t k = 0; k < N; k++)
      all += (k ? ", " : "") + std::string(names[k]);
    ESP_LOGW("panel", "panel_%s_animation: '%s' isn't one of %s", what, s.c_str(), all.c_str());
  }
  return i;
}

// For the panel_<what>_animation actions: copy i of name told statement, see tell_text(). The copy, null with a
// warning if it isn't shown or can't be told that.
inline Copy *act(const std::string &name, int i, const std::string &statement, const char *what) {
  auto [a, c] = copy_for(name, i, what);
  std::string error;
  if (!c || tell_text(*a, *c, statement, error))
    return c;
  ESP_LOGW("panel", "panel_%s_animation: %s", what, error.substr(error.rfind("line 1: ", 0) == 0 ? 8 : 0).c_str());
  return nullptr;
}
inline std::string num(int v) { return std::to_string(v); }

// For panel_glide_animation: one of copy i's channels to v over ms, eased. A turn goes the short way round, the
// shares are percent.
inline void glide(const std::string &name, int i, const std::string &what, int to, int ms, const std::string &ease) {
  int ch = option(CHANNELS, what, __func__);
  Copy *c = ch >= 0 && option(EASES, ease, __func__) >= 0
                ? act(name, i, "glide " + what + " " + num(to) + " over " + num(ms) + " ease " + ease, __func__)
                : nullptr;
  if (c && ch <= OVERLAY_Y) {
    c->doing = ", moved";
    status(shown());
  }
}

// For panel_move_animation: glide copy i to x, y over ms, 0 jumps
inline void move(const std::string &name, int i, int x, int y, int ms, const std::string &ease) {
  Copy *c = option(EASES, ease, __func__) >= 0
                ? act(name, i, "move " + num(x) + ", " + num(y) + " over " + num(ms) + " ease " + ease, __func__)
                : nullptr;
  if (c) {
    c->doing = ", moved to " + num(x) + "," + num(y);
    status(shown());
  }
}

// For panel_drift_animation: vx, vy in px/s, to a hundredth, wrapping round the screen. 0, 0 stops it where it is.
inline void drift(const std::string &name, int i, float vx, float vy) {
  if (Copy *c = act(name, i, "drift " + decimal(vx) + ", " + decimal(vy), __func__)) {
    c->doing = vx || vy ? ", drifting" : "";
    status(shown());
  }
}

// For panel_tint_animation: copy i's colours as shades of colour, like FF77BB or #FF77BB, but for those that glow.
// "" or none for its own.
inline void tint(const std::string &name, int i, const std::string &colour) {
  std::string hex = colour.substr(!colour.empty() && colour[0] == '#');
  bool none = hex.empty() || strcasecmp(hex.c_str(), "none") == 0;
  if (!none && (hex.size() != 6 || !std::all_of(hex.begin(), hex.end(), [](char k) { return isxdigit(k); }))) {
    ESP_LOGW("panel", "panel_%s_animation: '%s' isn't a colour like FF77BB", __func__, colour.c_str());
    return;
  }
  act(name, i, none ? "tint none" : "tint 0x" + hex, __func__);
}

// For panel_area_animation: where a drift or wander keeps and a map goes across. Each edge is screen, cross or
// bounce.
inline void area(const std::string &name, int i, int left, int top, int right, int bottom, const std::string &edge_x,
                 const std::string &edge_y) {
  if (option(EDGES, edge_x, __func__) >= 0 && option(EDGES, edge_y, __func__) >= 0)
    act(name, i,
        "area " + num(left) + ", " + num(top) + ", " + num(right) + ", " + num(bottom) + " " + edge_x + " " + edge_y,
        __func__);
}

// For panel_wander_animation: legs of speed_min..speed_max px/s toward heading +- spread for leg_min..leg_max ms,
// with rests of rest_min..rest_max ms between. The panel keeps the heading, spread and speeds in range.
inline void wander(const std::string &name, int i, float speed_min, float speed_max, int heading, int spread,
                   int leg_min, int leg_max, int rest_min, int rest_max) {
  if (Copy *c =
          act(name, i,
              "wander " + decimal(speed_min) + ", " + decimal(speed_max) + ", " + num(heading) + ", " + num(spread) +
                  ", " + num(leg_min) + ", " + num(leg_max) + ", " + num(rest_min) + ", " + num(rest_max),
              __func__)) {
    c->doing = ", wandering";
    status(shown());
  }
}

// For panel_face_animation: turns toward where it goes, at most tilt degrees from facing, at rate degrees per second
inline void face(const std::string &name, int i, int facing, bool mirror, int tilt, int rate, bool level) {
  act(name, i,
      "face " + num(facing) + (mirror ? " mirror" : "") + (level ? " level" : "") + " tilt " + num(tilt) + " rate " +
          num(rate),
      __func__);
}

// For panel_tell_animation: copy i told a statement, what moves, shows or binds it, like spin 90, blend add, wave
// size 90, 110, 800, 800 or opacity *= 50 + sin(ms / 9, 50), see overlay_script.h
inline void told(const std::string &name, int i, const std::string &statement) { act(name, i, statement, "tell"); }

// Copy c of a runs the script in text, with numbers by name in consts. False with a warning from
// panel_<what>_animation if it can't.
inline bool run_script(Overlay &a, Copy &c, const std::string &text, const Consts &consts, const char *what) {
  std::string error;
  Program p = compile(text, error, a.src->clips, consts);
  if (error.empty() && p.frames && !c.own)
    error = "it plays frames, and copy " + std::to_string(&c - a.copies.data()) + " has no frame of its own";
  if (!error.empty()) {
    ESP_LOGW("panel", "panel_%s_animation: %s's script, %s", what, a.name.c_str(), error.c_str());
    return false;
  }
  set_script(a, c, std::move(p));
  return true;
}

// For panel_script_animation: copy i runs the script in text, see overlay_script.h, with numbers by name in consts.
// One that plays frames needs a frame of its own. "" stops the one it runs.
inline bool script(const std::string &name, int i, const std::string &text, const Consts &consts = {}) {
  auto [a, c] = copy_for(name, i, __func__);
  return c && run_script(*a, *c, text, consts, __func__);
}

// For panel_rounds_animation: plays to the end of rounds rounds, then fades out and goes. 0 plays on.
inline void rounds(const std::string &name, int i, int n) {
  if (auto [a, c] = copy_for(name, i, __func__); c)
    run_script(*a, *c, n > 0 ? "wait rounds " + std::to_string(clamp_u16(n)) + "; end" : "", {}, __func__);
}

// For panel_frame_rate_animation: how fast name's frames go, percent, 0 holds the frame it's on
inline void frame_rate(const std::string &name, int percent) {
  Overlay *a = find(name);
  if (!a) {
    ESP_LOGW("panel", "panel_%s_animation: animation '%s' isn't shown", __func__, name.c_str());
    return;
  }
  a->frame_rate = (uint16_t) std::clamp(percent, 0, OVERLAY_RATE_MAX);
  if (a->state == SENT)
    send_frame_rate(*a);
}

// Called with the animation when its script ended the last copy it had
inline std::function<void(const std::string &)> emptied;

// The panel's K_OVERLAY_GONE = place | tag << 8: a copy's script ended it
inline void gone(uint32_t v) {
  for (auto &a : overlays)
    for (auto &c : a.copies)
      if (c.used && c.placed && c.tag && c.place == (v & 0xFF) && c.tag == (v >> 8 & 0xFF)) {
        c = Copy();
        status(shown());
        if (emptied && std::none_of(a.copies.begin(), a.copies.end(), [](const Copy &o) { return o.used; }))
          emptied(std::string(a.name));  // which may clear it
        return;
      }
}

// Called with the animation, the copy and the number when a copy's script gets to a mark
inline std::function<void(const std::string &, int, int)> marked;

// The panel's K_OVERLAY_MARK = place | mark << 8
inline void mark(uint32_t v) {
  for (auto &a : overlays)
    for (size_t i = 0; i < a.copies.size(); i++)
      if (a.copies[i].used && a.copies[i].placed && a.copies[i].place == (v & 0xFF)) {
        ESP_LOGD("panel", "Animation %s: copy %d at mark %u", a.name.c_str(), (int) i, (unsigned) (v >> 8 & 0xFF));
        if (marked)
          marked(a.name, (int) i, (int) (v >> 8 & 0xFF));
      }
}

// Every 20 ms, until deadline_us
inline void loop(uint32_t deadline_us) {
  if (!link::connected && (W.phase == COUNT || W.phase == MAKE))  // nothing is made without the panel
    stop();
  if (W.phase == IDLE) {
    if (!link::connected)
      return;
    for (auto &a : overlays)
      if (!a.name.empty() && a.state == WANT && (!a.looks || millis() - a.looked >= RAM_LOOK_MS)) {
        W.a = &a;
        if (!a.data.empty()) {  // made already, sent again after a hello. It fit before, so it fits again.
          W.t.reset();
          W.phase = SEND;
        } else if (!start()) {
          return;
        }
        break;
      }
    if (W.phase == IDLE)
      return;
  }
  while (W.phase == COUNT && W.f < W.a->src->frames && before(deadline_us))
    count();
  if (W.phase == COUNT && W.f == W.a->src->frames && fits(deadline_us, UINT32_MAX, true))
    palette();
  for (bool fresh = true; W.phase == MAKE && before(deadline_us) && make(deadline_us, fresh); fresh = false) {
  }
  if (W.phase == SEND)
    send(deadline_us);
}

}  // namespace flexit::panel::overlay
