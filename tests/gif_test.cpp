// The camera's GIF of a 16-colour picture with animations: its rows unpacked as plink_pack_row packed them, and the
// GIF decoded again: every frame is checked against the screen made straight from the animations' frames at its time,
// and so is the moment before the next one.
//   gif_test [out.gif]  writes the test's GIF to look at
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include "check.h"
#include "panel/pdeflate.h"
#include "panel/picture_gif.h"
#include "panel/plink.h"

using namespace flexit::panel::picture;

static constexpr int W = PIC_MAX_W, H = PIC_MAX_H;

// Rows with runs and literals of every length, packed and unpacked again
static void round_trip() {
  srand(1);
  for (int t = 0; t < 2000; t++) {
    size_t n = 1 + rand() % 200;
    std::vector<uint8_t> row(n), packed(n + n / 128 + 1), back(n);
    for (size_t i = 0; i < n;) {
      size_t run = 1 + rand() % (rand() % 4 ? 3 : 140);
      uint8_t v = (uint8_t) rand();
      for (size_t k = 0; k < run && i < n; k++, i++)
        row[i] = rand() % 3 ? v : (uint8_t) rand();
    }
    packed.resize(plink_pack_row(row.data(), (unsigned) n, packed.data()));
    unpack_row(packed.data(), back.data(), n);
    CHECK(back == row);
  }
}

// A w x h picture in 16 colours: bands of colour with a diagonal across them
static Pic picture(int w, int h) {
  Pic p;
  p.w = (uint16_t) w;
  p.h = (uint16_t) h;
  for (int i = 0; i < 16; i++)
    p.pal[i] = (uint32_t) (i * 16) << 16 | (uint32_t) (255 - i * 16) << 8 | (uint32_t) (i * 40 % 256);
  size_t row_n = (w + 1) / 2;
  std::vector<uint8_t> row(row_n);
  p.rows.push_back(0);
  for (int y = 0; y < h; y++) {
    for (size_t k = 0; k < row_n; k++) {
      auto idx = [&](int x) { return (uint8_t) (abs(x - y) < 3 ? 15 : y * 15 / h); };
      row[k] = (uint8_t) (idx((int) k * 2) << 4 | idx((int) k * 2 + 1));
    }
    size_t at = p.data.size();
    p.data.resize(at + row_n + row_n / 128 + 1);
    p.data.resize(at + plink_pack_row(row.data(), (unsigned) row_n, p.data.data() + at));
    p.rows.push_back((uint16_t) p.data.size());
  }
  return p;
}

// An animation as the ESP makes it: its frames, the records of them and the one back to the first
struct Anim {
  int w, h;
  uint8_t transp;
  uint32_t pal[16];
  std::vector<std::vector<uint8_t>> frames;
  std::vector<uint8_t> delays;  // each frame's, then the one back to the first
  std::vector<uint8_t> data;

  Anim(int w_, int h_, uint8_t transp_, int n, int seed) : w(w_), h(h_), transp(transp_) {
    for (int i = 0; i < 16; i++)
      pal[i] = (uint32_t) (seed * 50 + i * 13) % 256 << 16 | (uint32_t) (i * 16) << 8 | (uint32_t) (255 - i * 9);
    for (int f = 0; f < n; f++) {  // a square going round over a background, or nothing where it's transparent
      std::vector<uint8_t> px((size_t) w * h, transp < 16 ? transp : (uint8_t) (seed % 15));
      int sx = (f * 5 + seed) % (w - 6), sy = (f * 3) % (h - 6);
      for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++)
          px[(size_t) (sy + y) * w + sx + x] = (uint8_t) ((f + x + seed) % 15);
      std::vector<uint8_t> packed((size_t) w / 2 * h);
      for (size_t i = 0; i < packed.size(); i++)
        packed[i] = (uint8_t) (px[2 * i] << 4 | px[2 * i + 1]);
      frames.push_back(packed);
      delays.push_back((uint8_t) (4 + 2 * ((f * 7 + seed) % 5)));  // even, so 200 % speed is whole hundredths
    }
    delays.push_back(delays[0]);
    for (int f = 0; f <= n; f++) {
      const std::vector<uint8_t> *prev = f ? &frames[f - 1] : nullptr;
      const std::vector<uint8_t> &cur = frames[f % n];
      std::vector<uint8_t> r(ANIM_RECORD_HEAD + PDEFLATE_BOUND(cur.size()));
      r.resize(plink_anim_record(prev ? prev->data() : nullptr, cur.data(), w, h, delays[f], r.data()));
      CHECK(!r.empty());
      data.insert(data.end(), r.begin(), r.end());
    }
  }

  Scene::Slot slot(int scale, int rate) const {
    Scene::Slot s;
    s.data = data.data();
    s.len = (uint32_t) data.size();
    s.pal = pal;
    s.w = w;
    s.h = h;
    s.scale = scale;
    s.transp = transp;
    s.rate = rate;
    return s;
  }
};

// ms a record shows for, as the panel plays it
static uint32_t period(uint8_t delay, int rate) {
  return std::max<uint32_t>(1, std::max<uint32_t>(delay * 10u, ANIM_FRAME_MIN_MS) * 100 / rate);
}

// The frame a copy shows t ms into the GIF. A round is the first frame for the record back to it, then the others.
// A copy of its own starts start frames on.
static int frame_at(const Anim &a, int rate, uint32_t t, int start) {
  int n = (int) a.frames.size();
  if (n < 2 || !rate)
    return 0;
  auto shows = [&](int f) { return period(a.delays[f ? f : n], rate); };
  uint32_t round = 0;
  for (int f = 0; f < n; f++)
    round += shows(f);
  int f = start % n;
  for (t %= round; t >= shows(f); f = (f + 1) % n)
    t -= shows(f);
  return f;
}

// The screen t ms into the GIF, made from the picture and the animations' frames
static std::vector<uint8_t> screen(const Scene &s, const std::vector<const Anim *> &anims, uint32_t t) {
  std::vector<uint8_t> out((size_t) W * H, Scene::BLACK);
  if (const Pic *p = s.pic) {
    std::vector<uint8_t> row((p->w + 1) / 2);
    for (int y = 0; y < p->h; y++) {
      unpack_row(p->data.data() + p->rows[y], row.data(), row.size());
      for (int x = 0; x < p->w; x++)
        out[(size_t) (y + H / 2 - p->h / 2) * W + x + W / 2 - p->w / 2] = x & 1 ? row[x / 2] & 15 : row[x / 2] >> 4;
    }
  }
  for (const auto &c : s.copies) {
    const Anim &a = *anims[c.slot];
    const Scene::Slot &sl = s.slots[c.slot];
    const auto &f = a.frames[frame_at(a, sl.rate, t, c.own ? c.start : 0)];
    for (int y = 0; y < a.h * sl.scale; y++)
      for (int x = 0; x < a.w * sl.scale; x++) {
        int X = c.x + x, Y = c.y + y, fx = x / sl.scale, fy = y / sl.scale;
        if (X >= W || Y >= H)
          continue;
        if (c.flip_x)
          fx = a.w - 1 - fx;
        if (c.flip_y)
          fy = a.h - 1 - fy;
        uint8_t b = f[(size_t) fy * (a.w / 2) + fx / 2], i = fx & 1 ? b & 15 : b >> 4;
        if (i != a.transp)
          out[(size_t) Y * W + X] = (uint8_t) (16 + 16 * c.slot + i);
      }
  }
  return out;
}

// ---- a GIF decoder, for the frames only

struct Decoded {
  bool ok = false;
  std::vector<std::vector<uint8_t>> screens;  // after each frame
  std::vector<uint32_t> at;                   // ms each frame shows at, then the end
};

static Decoded decode(const uint8_t *g, size_t n) {
  Decoded d;
  size_t i = 0;
  auto u16 = [&](size_t k) { return (unsigned) (g[k] | g[k + 1] << 8); };
  if (n < 13 || memcmp(g, "GIF89a", 6) || u16(6) != W || u16(8) != H || g[10] != 0xE6)
    return d;
  i = 13 + 3 * Gif::COLOURS;
  std::vector<uint8_t> canvas((size_t) W * H, 0);
  uint32_t t = 0, delay = 0;
  while (i < n) {
    uint8_t b = g[i++];
    if (b == 0x3B) {
      d.at.push_back(t);
      d.ok = i == n;
      return d;
    }
    if (b == 0x21) {  // an extension, the delay from a graphic control
      uint8_t label = g[i++];
      if (label == 0xF9)
        delay = u16(i + 2) * 10;
      while (g[i])
        i += g[i] + 1;
      i++;
      (void) label;
      continue;
    }
    if (b != 0x2C)
      return d;
    unsigned fx = u16(i), fy = u16(i + 2), fw = u16(i + 4), fh = u16(i + 6);
    i += 9;
    unsigned min = g[i++];
    std::vector<uint8_t> data;
    while (g[i]) {
      data.insert(data.end(), g + i + 1, g + i + 1 + g[i]);
      i += g[i] + 1;
    }
    i++;
    // LZW
    const unsigned clear = 1u << min, stop = clear + 1;
    std::vector<uint16_t> prefix(4096);
    std::vector<uint8_t> suffix(4096), first(4096), out;
    for (unsigned k = 0; k < clear; k++)
      suffix[k] = first[k] = (uint8_t) k;
    unsigned size = min + 1, next = stop + 1, bitpos = 0;
    int prev = -1;
    auto string = [&](unsigned c, std::vector<uint8_t> &o) {
      size_t at = o.size();
      for (; c >= clear; c = prefix[c])
        o.push_back(suffix[c]);
      o.push_back((uint8_t) c);
      std::reverse(o.begin() + (long) at, o.end());
    };
    for (;;) {
      if (bitpos + size > data.size() * 8)
        return d;
      unsigned c = 0;
      for (unsigned k = 0; k < size; k++, bitpos++)
        c |= (unsigned) (data[bitpos / 8] >> bitpos % 8 & 1) << k;
      if (c == clear) {
        size = min + 1;
        next = stop + 1;
        prev = -1;
        continue;
      }
      if (c == stop)
        break;
      if (prev < 0) {
        if (c >= clear)
          return d;
        out.push_back((uint8_t) c);
        prev = (int) c;
        continue;
      }
      if (c > next)
        return d;
      uint8_t f = c < next ? first[c] : first[prev];
      if (next < 4096) {
        prefix[next] = (uint16_t) prev;
        suffix[next] = f;
        first[next] = first[prev];
        next++;
      }
      string(c, out);
      if (next == 1u << size && size < 12)
        size++;
      prev = (int) c;
    }
    if (out.size() != (size_t) fw * fh || fx + fw > (unsigned) W || fy + fh > (unsigned) H)
      return d;
    for (unsigned y = 0; y < fh; y++)
      memcpy(&canvas[(size_t) (fy + y) * W + fx], &out[(size_t) y * fw], fw);
    d.screens.push_back(canvas);
    d.at.push_back(t);
    t += delay;
  }
  return d;
}

static std::vector<uint8_t> make(Scene &s, size_t cap) {
  std::vector<uint8_t> out(cap);
  s.start(out);
  while (s.step()) {
  }
  uint32_t n = s.finish();
  out.resize(n <= cap ? n : 0);
  return out;
}

// Every frame shows what the screen has at its time, and right before the next one unless a change came sooner than
// MIN_CS after it and waited for that
static int check_frames(const Scene &s, const std::vector<const Anim *> &anims, const std::vector<uint8_t> &g) {
  Decoded d = decode(g.data(), g.size());
  CHECK(d.ok);
  for (size_t f = 0; f < d.screens.size(); f++) {
    CHECK(d.screens[f] == screen(s, anims, d.at[f]));
    CHECK(d.at[f + 1] - d.at[f] >= Scene::MIN_CS * 10 || d.at[f + 1] == d.at.back());
    // A change waited for the next frame, but no longer than MIN_CS, or one too close to the end isn't shown
    if (d.screens[f] != screen(s, anims, d.at[f + 1] - 1))
      CHECK(d.at[f + 1] - d.at[f] == Scene::MIN_CS * 10 || d.at.back() - d.at[f + 1] + 1 < Scene::MIN_CS * 10);
  }
  return (int) d.screens.size();
}

int main(int argc, char **argv) {
  round_trip();

  Pic pic = picture(300, 170);
  Anim a(24, 20, 15, 7, 1), b(16, 12, ANIM_OPAQUE, 5, 2);

  // a twice in step, once flipped, partly off the screen. b at 2x and twice the speed, one copy with its own frame.
  Scene s;
  s.pic = &pic;
  s.slots[0] = a.slot(1, 100);
  s.slots[2] = b.slot(2, 200);
  s.copies = {
      {0, 40, 30}, {2, 100, 100}, {0, 50, 40, true, true}, {2, 300, 225, false, false, true, 3}, {0, 310, 10, true}};
  std::vector<const Anim *> anims = {&a, nullptr, &b};
  std::vector<uint8_t> g = make(s, 256 * 1024);
  CHECK(!g.empty() && s.looped);
  int frames = check_frames(s, anims, g);
  printf("300x170 with 5 copies of 2 animations: %d frames, %u ms, %u bytes\n", frames, (unsigned) s.end_ms,
         (unsigned) g.size());
  uint32_t ra = 0, rb = 0;  // the GIF is as long as both their rounds together
  for (size_t f = 1; f <= a.frames.size(); f++)
    ra += period(a.delays[f], 100);
  for (size_t f = 1; f <= b.frames.size(); f++)
    rb += period(b.delays[f], 200);
  CHECK(s.end_ms == std::lcm(ra, rb));
  CHECK(decode(g.data(), g.size()).at.back() == s.end_ms);
  if (argc > 1) {
    FILE *f = fopen(argv[1], "wb");
    fwrite(g.data(), 1, g.size(), f);
    fclose(f);
  }

  // Too small for all of it: it ends after a whole frame, and still loops
  std::vector<uint8_t> part = make(s, g.size() - 1000);
  CHECK(!part.empty() && part.size() <= g.size() - 1000);
  CHECK(check_frames(s, anims, part) < frames);
  // Not even the first frame
  CHECK(make(s, 2000).empty());

  // Rounds that only meet after MAX_MS: cut there
  Anim d(16, 16, 15, 4, 5);
  Scene m;
  m.slots[0] = a.slot(1, 100);
  m.slots[1] = d.slot(1, 37);
  m.copies = {{0, 0, 0}, {1, 200, 100}};
  g = make(m, 64 * 1024);
  CHECK(!m.looped && m.end_ms == Scene::MAX_MS);
  Decoded dm = decode(g.data(), g.size());  // 37 % isn't whole hundredths, so only how long it goes
  CHECK(dm.ok && dm.screens.size() > 1 && dm.at.back() == Scene::MAX_MS);

  // Rounds whose lcm is past 32 bits: still cut at MAX_MS, not wrapped round to something short
  Anim e(16, 16, 15, 20, 7);
  Scene o;
  o.slots[0] = a.slot(1, 2);
  o.slots[1] = e.slot(1, 1);
  o.copies = {{0, 0, 0}, {1, 100, 100}};
  g = make(o, 64 * 1024);
  CHECK(!o.looped && o.end_ms == Scene::MAX_MS);

  // Ten times as fast: changes 4 ms apart go in frames MIN_CS apart, which browsers play as such
  Scene q;
  q.slots[0] = a.slot(1, 1000);
  q.copies = {{0, 0, 0}};
  g = make(q, 64 * 1024);
  Decoded dq = decode(g.data(), g.size());
  CHECK(dq.ok);
  for (size_t f = 0; f + 1 < dq.at.size(); f++)
    CHECK(dq.at[f + 1] - dq.at[f] >= Scene::MIN_CS * 10);

  // Paused, and no picture: one still frame on black
  Scene p;
  p.slots[0] = a.slot(1, 0);
  p.copies = {{0, 10, 10}};
  g = make(p, 64 * 1024);
  CHECK(check_frames(p, {&a}, g) == 1);
  CHECK(p.end_ms == 0);

  printf(fails ? "%d failed\n" : "ok\n", fails);
  return fails != 0;
}
