#include "picture_gif.h"
#include <algorithm>
#include <cstring>
#include <numeric>
#include "pdeflate.h"
#include "plink.h"

namespace flexit::panel::picture {

// Below 128 that many + 1 bytes as they are, else that many - 126 of the next byte
void unpack_row(const uint8_t *in, uint8_t *out, size_t n) {
  size_t o = 0;
  while (o < n) {
    unsigned h = *in++;
    if (h < 128) {
      size_t k = std::min<size_t>(h + 1, n - o);
      memcpy(out + o, in, k);
      in += h + 1;
      o += k;
    } else {
      size_t k = std::min<size_t>(h - 126, n - o);
      memset(out + o, *in++, k);
      o += k;
    }
  }
}

// Where each record of s starts, then its end, checked as the panel's anim_close does. Empty if one is bad.
static std::vector<uint32_t> records(const Scene::Slot &s) {
  std::vector<uint32_t> at;
  const unsigned rb = (unsigned) s.w / 2;
  for (uint32_t o = 0; o < s.len;) {
    const uint8_t *r = s.data + o;
    if (s.len - o < ANIM_RECORD_HEAD || r[ANIM_RECORD_X] + r[ANIM_RECORD_W] > rb ||
        r[ANIM_RECORD_Y] + r[ANIM_RECORD_H] > s.h || !r[ANIM_RECORD_W] != !r[ANIM_RECORD_H])
      return {};
    uint32_t n = 0;
    if (r[ANIM_RECORD_W]) {
      n = pinflate(r + ANIM_RECORD_HEAD, s.len - o - ANIM_RECORD_HEAD, nullptr, r[ANIM_RECORD_W], rb,
                   (uint32_t) r[ANIM_RECORD_W] * r[ANIM_RECORD_H], 0);
      if (!n)
        return {};
    }
    at.push_back(o);
    o += ANIM_RECORD_HEAD + n;
  }
  if (!at.empty())
    at.push_back(s.len);
  return at;
}

// Applies record r of s to frame. Its rectangle in pixels into x, y, w, h, w = 0 if it's empty.
static void apply(const Scene::Slot &s, uint32_t o, uint8_t *frame, int &x, int &y, int &w, int &h) {
  const unsigned rb = (unsigned) s.w / 2;
  const uint8_t *r = s.data + o;
  x = r[ANIM_RECORD_X] * 2;
  y = r[ANIM_RECORD_Y];
  w = r[ANIM_RECORD_W] * 2;
  h = r[ANIM_RECORD_H];
  if (w)
    pinflate(r + ANIM_RECORD_HEAD, s.len - o - ANIM_RECORD_HEAD, frame + y * rb + r[ANIM_RECORD_X], r[ANIM_RECORD_W],
             rb, (uint32_t) r[ANIM_RECORD_W] * r[ANIM_RECORD_H], 0);
}

uint32_t Scene::period(int s, size_t i) const {
  uint32_t ms = std::max<uint32_t>(slots[s].data[at[s][i]] * ANIM_TICK_MS, ANIM_FRAME_MIN_MS);
  return std::max<uint32_t>(1, ms * 100 / slots[s].rate);
}

static uint32_t cs(uint32_t ms) { return (ms + 5) / 10; }
// The delay from then to t, frames are MIN_CS apart at least
static unsigned cs_between(uint32_t then, uint32_t t) { return cs(t) - cs(then); }

void Scene::start(std::vector<uint8_t> &out) {
  uint32_t pal[BLACK + 1] = {};
  if (pic) {
    memcpy(pal, pic->pal, sizeof pic->pal);
    ox = PIC_MAX_W / 2 - pic->w / 2;  // as LVGL centres it
    oy = PIC_MAX_H / 2 - pic->h / 2;
    row.resize((pic->w + 1) / 2);
  }
  row_y = -1;
  for (int s = 0; s < ANIM_SLOTS; s++) {
    at[s].clear();
    if (slots[s].data && slots[s].pal && slots[s].w >= 2 && slots[s].h >= 1) {
      memcpy(pal + 16 + 16 * s, slots[s].pal, 16 * sizeof *pal);
      at[s] = records(slots[s]);
    }
  }
  // A player for each slot shown in step and each copy with a frame of its own, set as the panel sets them up
  players.clear();
  player_of.assign(copies.size(), -1);
  boxes.clear();
  for (const Copy &c : copies) {
    const Slot &s = slots[c.slot < 0 || c.slot >= ANIM_SLOTS ? 0 : c.slot];
    boxes.push_back({c.x, c.y, c.x + s.w * s.scale, c.y + s.h * s.scale});
  }
  int shared[ANIM_SLOTS];
  std::fill(std::begin(shared), std::end(shared), -1);
  uint64_t loop = 1;  // 64 bits, so the lcm can't wrap before it's capped
  bool any = false;
  for (size_t k = 0; k < copies.size(); k++) {
    const Copy &c = copies[k];
    if (c.slot < 0 || c.slot >= ANIM_SLOTS || at[c.slot].empty())
      continue;
    if (!c.own && shared[c.slot] >= 0) {
      player_of[k] = shared[c.slot];
      continue;
    }
    const Slot &s = slots[c.slot];
    const size_t n = at[c.slot].size() - 1;  // records
    Player p{c.slot, std::vector<uint8_t>((size_t) s.w / 2 * s.h, s.transp < 16 ? s.transp * 0x11 : 0), 1, 0};
    int x, y, w, h;
    apply(s, 0, p.frame.data(), x, y, w, h);
    bool plays = n > 1 && s.rate > 0;
    size_t last = n - 1;  // the record applied last: one round ends on the first frame again
    for (int i = 0; plays && c.own && i < c.start; i++) {
      apply(s, at[c.slot][p.next], p.frame.data(), x, y, w, h);
      last = p.next;
      p.next = p.next + 1 < n ? p.next + 1 : 1;
    }
    p.due = plays ? period(c.slot, last) : UINT32_MAX;
    if (plays) {
      uint32_t round = 0;
      for (size_t i = 1; i < n; i++)
        round += period(c.slot, i);
      loop = std::lcm(loop, (uint64_t) round);
      any = true;
      if (loop > MAX_MS)
        loop = MAX_MS + 1;
    }
    player_of[k] = (int) players.size();
    if (!c.own)
      shared[c.slot] = (int) players.size();
    players.push_back(std::move(p));
  }
  looped = any && loop <= MAX_MS;
  end_ms = any ? (uint32_t) std::min<uint64_t>(loop, MAX_MS) : 0;
  now = 0;
  gif.begin(PIC_MAX_W, PIC_MAX_H, pal, BLACK + 1, out.data(), (uint32_t) out.size());
  gif.frame(0, 0, PIC_MAX_W, PIC_MAX_H);
}

// On to the next frame: shown when the screen next changes, or MIN_CS after the last frame if that's later, with
// all that changed by then. False at the end.
bool Scene::advance() {
  int x0 = PIC_MAX_W, y0 = PIC_MAX_H, x1 = 0, y1 = 0;  // what changed on the screen
  uint32_t shows = UINT32_MAX;                         // when the frame shows, once something changed
  for (;;) {
    uint32_t t = UINT32_MAX;
    for (auto &p : players)
      t = std::min(t, p.due);
    if (t >= end_ms || t > shows)
      break;
    for (size_t i = 0; i < players.size(); i++) {
      Player &p = players[i];
      if (p.due != t)
        continue;
      const Slot &s = slots[p.slot];
      const size_t n = at[p.slot].size() - 1;
      int x, y, w, h;
      apply(s, at[p.slot][p.next], p.frame.data(), x, y, w, h);
      p.due += period(p.slot, p.next);
      p.next = p.next + 1 < n ? p.next + 1 : 1;
      for (size_t k = 0; k < copies.size() && w; k++) {
        if (player_of[k] != (int) i)
          continue;
        const Copy &c = copies[k];
        int fx = c.flip_x ? s.w - x - w : x, fy = c.flip_y ? s.h - y - h : y;
        x0 = std::min(x0, std::max(0, c.x + fx * s.scale));
        y0 = std::min(y0, std::max(0, c.y + fy * s.scale));
        x1 = std::max(x1, std::min(PIC_MAX_W, c.x + (fx + w) * s.scale));
        y1 = std::max(y1, std::min(PIC_MAX_H, c.y + (fy + h) * s.scale));
      }
    }
    if (shows == UINT32_MAX && x1 > x0 && y1 > y0)
      shows = std::max(t, now + MIN_CS * 10);  // until then nothing on the screen changed, the frame shown went on
  }
  if (shows == UINT32_MAX || shows + MIN_CS * 10 > end_ms) {  // the end, or a change too close to it to show for MIN_CS
    if (end_ms)
      gif.set_delay(cs_between(now, end_ms));
    return false;
  }
  gif.set_delay(cs_between(now, shows));
  now = shows;
  gif.frame(x0, y0, x1 - x0, y1 - y0);
  return true;
}

bool Scene::step() {
  if (gif.step(pixel, this, STEP_PX))
    return true;
  return !gif.full() && advance();
}

uint32_t Scene::finish() {
  uint32_t n = gif.end();
  players.clear();
  players.shrink_to_fit();
  for (auto &a : at)
    std::vector<uint32_t>().swap(a);
  std::vector<int>().swap(player_of);
  std::vector<Box>().swap(boxes);
  std::vector<uint8_t>().swap(row);
  return n;
}

uint8_t Scene::pixel(void *ctx, int x, int y) {
  auto *s = (Scene *) ctx;
  uint8_t c = BLACK;
  if (const Pic *p = s->pic) {
    int px = x - s->ox, py = y - s->oy;
    if (px >= 0 && py >= 0 && px < p->w && py < p->h) {
      if (py != s->row_y) {
        unpack_row(p->data.data() + p->rows[py], s->row.data(), s->row.size());
        s->row_y = py;
      }
      uint8_t b = s->row[px / 2];
      c = px & 1 ? b & 15 : b >> 4;
    }
  }
  for (size_t k = 0; k < s->copies.size(); k++) {
    const Box &b = s->boxes[k];
    if (s->player_of[k] < 0 || x < b.x0 || y < b.y0 || x >= b.x1 || y >= b.y1)
      continue;
    const Copy &cp = s->copies[k];
    const Slot &sl = s->slots[cp.slot];
    int sx = (x - cp.x) / sl.scale, sy = (y - cp.y) / sl.scale;
    if (cp.flip_x)
      sx = sl.w - 1 - sx;
    if (cp.flip_y)
      sy = sl.h - 1 - sy;
    uint8_t two = s->players[s->player_of[k]].frame[(size_t) sy * (sl.w / 2) + sx / 2];
    uint8_t i = sx & 1 ? two & 15 : two >> 4;
    if (i != sl.transp)
      c = (uint8_t) (16 + 16 * cp.slot + i);
  }
  return c;
}

}  // namespace flexit::panel::picture
