// Text and marks over a panel picture, drawn on the ESP in the panel's own Noto Sans (marks_font.h). A Layer lists
// them like a drawing. The text, dots and marks go to the panel sharp, as a still overlay with colours of its own:
// each ink a few opacities, so the edges stay smooth. The lines, glows, boxes, scrims and the soft shadow under it
// all go into the picture, where the JPEG keeps them smooth. Both are drawn BAND rows at a time as picture.h and
// overlay.h ask for them, so neither is held whole. on() hooks a picture up, set() gives it a new layer.
#pragma once
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../panel/overlay.h"
#include "../panel/picture.h"
#include "marks_font.h"

namespace flexit::panel::marks {

inline constexpr int W = PIC_MAX_W, H = PIC_MAX_H;
inline constexpr int BAND = 8;  // rows drawn at a time, a JPEG's MCU row
inline constexpr int PAD = 2;   // rows above and below a band the shadow reaches it from
inline constexpr uint32_t WHITE = 0xFFFFFF, INK = 0x08090E, CYAN = 0x62E2FF, WARM = 0xFFEED4;

struct Box {
  float x0, y0, x1, y1;
};

struct Op {
  enum Kind : uint8_t { TEXT, DOT, RING, LINE, POLY, RECT, SCRIM, GLOW_DOT, GLOW_LINE };
  Kind kind;
  bool soft;  // drawn into the picture, else the overlay
  uint32_t rgb;
  float alpha;
  Box box;                 // what it covers, for the bands it's drawn in
  float r = 0, w = 0;      // a radius (DOT, RING, GLOW_DOT, RECT's corners, SCRIM's size), a width
  std::vector<float> pts;  // LINE, GLOW_LINE, POLY: x, y pairs. POLY is convex.
  std::string text;        // TEXT in UTF-8, or SCRIM's edge: "t", "b", "l" or "r"
  FaceId face = SMALL;
  float track = 0;  // TEXT: px between letters
};

// The next code point of UTF-8 s at i
inline uint32_t next_code(const std::string &s, size_t &i) {
  uint8_t c = (uint8_t) s[i++];
  int more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t cp = more ? c & (0x3F >> more) : c;
  while (more-- > 0 && i < s.size())
    cp = cp << 6 | ((uint8_t) s[i++] & 0x3F);
  return cp;
}

inline const Glyph *glyph_of(const Face &f, uint32_t cp) {
  const uint16_t *end = f.codes + f.count, *c = std::lower_bound(f.codes, end, cp);
  if (c != end && *c == cp)
    return f.glyphs + (c - f.codes);
  return cp == '?' ? nullptr : glyph_of(f, '?');
}

// s's width in face f
inline float width(const std::string &s, FaceId f, float track = 0) {
  float w = 0;
  int n = 0;
  for (size_t i = 0; i < s.size(); n++)
    if (const Glyph *g = glyph_of(FACES[f], next_code(s, i)))
      w += g->advance / 16.0f;
  return w + track * std::max(n - 1, 0);
}

// s in capitals, æøå too
inline std::string upper(const std::string &s) {
  std::string u;
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = (unsigned char) s[i];
    if (c == 0xC3 && i + 1 < s.size() && (s[i + 1] == '\xA6' || s[i + 1] == '\xB8' || s[i + 1] == '\xA5')) {
      u += (char) c;
      u += (char) (s[++i] - 0x20);  // æ ø å to Æ Ø Å
    } else {
      u += (char) toupper(c);
    }
  }
  return u;
}

// Text and marks as the gallery's ui.py draws them: y is a baseline, sizes and widths in px
struct Layer {
  std::vector<Op> ops;
  std::vector<Box> boxes;  // taken, so label() puts nothing over them
  float shadow = 0.6f;     // the soft shadow under the sharp ops and the lines

  Op &add(Op::Kind k, bool soft, uint32_t rgb, float alpha, Box b) {
    Op o;
    o.kind = k;
    o.soft = soft;
    o.rgb = rgb;
    o.alpha = alpha;
    o.box = b;
    ops.push_back(std::move(o));
    return ops.back();
  }
  // anchor: 'l', 'm' or 'r'. The width it took.
  float text(float x, float y, const std::string &s, FaceId f, uint32_t rgb = WHITE, float alpha = 1, char anchor = 'l',
             float track = 0) {
    float w = width(s, f, track);
    x -= anchor == 'r' ? w : anchor == 'm' ? w / 2 : 0;
    const Face &fc = FACES[f];
    Op &o = add(Op::TEXT, false, rgb, alpha, {x - 1, y - fc.ascent - 1, x + w + 1, y + fc.height - fc.ascent + 1});
    o.text = s;
    o.face = f;
    o.track = track;
    return w;
  }
  // A small label in capitals
  float caps(float x, float y, const std::string &s, float alpha = 0.62f, uint32_t rgb = WHITE, char anchor = 'l') {
    return text(x, y, upper(s), CAPS, rgb, alpha, anchor, 0.78f);
  }
  void line(std::vector<float> pts, float w, uint32_t rgb, float alpha = 1, float glow = 0) {
    Box b{1e9f, 1e9f, -1e9f, -1e9f};
    for (size_t i = 0; i + 1 < pts.size(); i += 2)
      b = {std::min(b.x0, pts[i]), std::min(b.y0, pts[i + 1]), std::max(b.x1, pts[i]), std::max(b.y1, pts[i + 1])};
    if (glow) {
      float m = w * 2 + 3;
      Op &g = add(Op::GLOW_LINE, true, rgb, glow, {b.x0 - m, b.y0 - m, b.x1 + m, b.y1 + m});
      g.w = w * 4;
      g.pts = pts;
    }
    float m = w / 2 + 1;
    Op &o = add(Op::LINE, true, rgb, alpha, {b.x0 - m, b.y0 - m, b.x1 + m, b.y1 + m});
    o.w = w;
    o.pts = std::move(pts);
  }
  void dot(float x, float y, float r, uint32_t rgb, float alpha = 1, float glow = 0) {
    if (glow) {
      float R = r * 3.2f;
      add(Op::GLOW_DOT, true, rgb, glow, {x - R - 3, y - R - 3, x + R + 3, y + R + 3}).r = R;
      add(Op::DOT, false, rgb, alpha, {x - r - 1, y - r - 1, x + r + 1, y + r + 1}).r = r;
    } else {
      add(Op::DOT, false, rgb, alpha, {x - r - 1, y - r - 1, x + r + 1, y + r + 1}).r = r;
    }
  }
  void ring(float x, float y, float r, uint32_t rgb, float w = 1, float alpha = 1) {
    float m = r + w / 2 + 1;
    Op &o = add(Op::RING, true, rgb, alpha, {x - m, y - m, x + m, y + m});
    o.r = r;
    o.w = w;
  }
  void poly(std::vector<float> pts, uint32_t rgb, float alpha = 1) {
    Box b{1e9f, 1e9f, -1e9f, -1e9f};
    for (size_t i = 0; i + 1 < pts.size(); i += 2)
      b = {std::min(b.x0, pts[i]), std::min(b.y0, pts[i + 1]), std::max(b.x1, pts[i]), std::max(b.y1, pts[i + 1])};
    add(Op::POLY, false, rgb, alpha, {b.x0 - 1, b.y0 - 1, b.x1 + 1, b.y1 + 1}).pts = std::move(pts);
  }
  // A box with round corners, into the picture
  void rect(Box b, float radius, uint32_t rgb, float alpha = 1) {
    add(Op::RECT, true, rgb, alpha, {b.x0 - 1, b.y0 - 1, b.x1 + 1, b.y1 + 1}).r = radius;
  }
  // Darkens smoothly toward one edge, so text over a picture reads
  void scrim(char edge, float size, float strength = 0.7f) {
    Op &o = add(Op::SCRIM, true, 0x06070B, strength, {0, 0, (float) W, (float) H});
    o.r = size;
    o.text = std::string(1, edge);
  }
  // A rounded tag whose left middle is at x, y
  Box pill(float x, float y, const std::string &s, uint32_t fill = CYAN, uint32_t ink = INK) {
    float w = width(upper(s), CAPS, 0.78f);
    Box b{x, y - 5.5f, x + w + 9, y + 5.5f};
    rect(b, 5.5f, fill);
    caps(x + 4.5f, y + 2.3f, s, 1.0f, ink);
    boxes.push_back(b);
    return b;
  }
  bool free(const Box &b) const {
    if (b.x0 < 2 || b.x1 >= W - 2 || b.y0 < 1 || b.y1 >= H - 1)
      return false;
    for (const Box &o : boxes)
      if (b.x0 < o.x1 && o.x0 < b.x1 && b.y0 < o.y1 && o.y0 < b.y1)
        return false;
    return true;
  }
  // s beside the point x, y where nothing else is, on side ('l', 'r', 'u', 'd') if it can. Without need it gives up
  // quietly. extra is a second, fainter part after s.
  bool label(float x, float y, const std::string &s, FaceId f = LABEL, uint32_t rgb = WHITE, float alpha = 0.92f,
             char side = 0, bool need = false, const std::string &extra = "") {
    const float size = FACES[f].size;
    float w = width(s, f) + (extra.empty() ? 0 : width(" " + extra, SMALL)), h = size * 1.05f;
    struct Spot {
      float dx, dy;
    } spots[] = {{5, -h / 2}, {-5 - w, -h / 2}, {4, -h - 3},      {-4 - w, -h - 3},
                 {4, 3},      {-4 - w, 3},      {-w / 2, -h - 5}, {-w / 2, 5}};
    auto wanted = [&](const Spot &o) {
      return side == 'l' ? o.dx < -w / 2 : side == 'r' ? o.dx > 0 : side == 'u' ? o.dy < -h / 2 : o.dy > -h / 2;
    };
    if (side)
      std::stable_sort(std::begin(spots), std::end(spots),
                       [&](const Spot &a, const Spot &b) { return wanted(a) && !wanted(b); });
    Box b{};
    bool found = false;
    for (const Spot &o : spots) {
      b = {x + o.dx, y + o.dy, x + o.dx + w, y + o.dy + h};
      if ((found = free(b)))
        break;
    }
    if (!found && !need)
      return false;
    boxes.push_back(b);
    float bx = text(b.x0, b.y0 + size * 0.86f, s, f, rgb, alpha);
    if (!extra.empty())
      text(b.x0 + bx + size * 0.3f, b.y0 + size * 0.86f, extra, SMALL, rgb, alpha * 0.55f);
    return true;
  }
  // A figure, its unit in a lighter weight, and a small label under it. y is the figure's baseline.
  float stat(float x, float y, const std::string &value, const std::string &unit, const std::string &caption,
             FaceId f = BIG, FaceId unit_face = SMALL, uint32_t rgb = WHITE, char anchor = 'l') {
    float gap = FACES[f].size * 0.14f;
    float w = width(value, f) + (unit.empty() ? 0 : gap + width(unit, unit_face));
    float x0 = anchor == 'r' ? x - w : x;
    float vw = text(x0, y, value, f, rgb);
    if (!unit.empty())
      text(x0 + vw + gap, y, unit, unit_face, rgb, 0.75f);
    caps(anchor == 'r' ? x : x0, y + 10, caption, 0.62f, WHITE, anchor == 'r' ? 'r' : 'l');
    return w;
  }
};

// ---------------------------------------------------------------- drawing a band

inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
inline float smooth(float v) {
  v = clamp01(v);
  return v * v * (3 - 2 * v);
}

// Distance from p to the segment a-b
inline float to_segment(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
  float t = l > 0 ? clamp01(((px - ax) * dx + (py - ay) * dy) / l) : 0;
  float ex = px - ax - t * dx, ey = py - ay - t * dy;
  return sqrtf(ex * ex + ey * ey);
}

// A band's coverage of an op, 0..255 in rows y0.. of cov, W a row. Parts of one op take the most of them, so lines
// don't double up where they meet. The rows and columns it touched, to clear.
struct Span {
  int x0 = W, x1 = 0, y0 = INT_MAX, y1 = INT_MIN;
  void add(int x, int y) {
    x0 = std::min(x0, x);
    x1 = std::max(x1, x + 1);
    y0 = std::min(y0, y);
    y1 = std::max(y1, y + 1);
  }
  bool empty() const { return x0 >= x1; }
};

inline void put(uint8_t *cov, int y0, int x, int y, float c, Span &sp) {
  if (c <= 0)
    return;
  uint8_t v = (uint8_t) std::min(255.0f, c * 255 + 0.5f);
  uint8_t &p = cov[(y - y0) * W + x];
  if (v > p)
    p = v;
  sp.add(x, y);
}

// Pixels of b in rows y0..y1 of the screen, as x0..x1 and ya..yb
inline bool clip(const Box &b, int y0, int y1, int &x0, int &x1, int &ya, int &yb) {
  x0 = std::max(0, (int) floorf(b.x0));
  x1 = std::min(W, (int) ceilf(b.x1) + 1);
  ya = std::max(y0, (int) floorf(b.y0));
  yb = std::min(y1, (int) ceilf(b.y1) + 1);
  return x0 < x1 && ya < yb;
}

inline void cover_text(const Op &o, int y0, int y1, uint8_t *cov, Span &sp) {
  const Face &f = FACES[o.face];
  float pen = o.box.x0 + 1;
  int base = (int) lroundf(o.box.y0 + 1 + f.ascent);
  for (size_t i = 0; i < o.text.size();) {
    const Glyph *g = glyph_of(f, next_code(o.text, i));
    if (!g)
      continue;
    int gx = (int) lroundf(pen) + g->x, gy = base + g->y;
    for (int y = std::max(gy, y0); y < std::min(gy + g->h, y1); y++)
      for (int x = std::max(gx, 0); x < std::min(gx + g->w, W); x++) {
        uint32_t k = (uint32_t) ((y - gy) * g->w + (x - gx));
        uint8_t b = f.data[g->at + k / 2];
        put(cov, y0, x, y, (k & 1 ? b & 15 : b >> 4) / 15.0f, sp);
      }
    pen += g->advance / 16.0f + o.track;
  }
}

// The op's coverage in rows y0..y1 into cov, by its own rule
inline void cover(const Op &o, int y0, int y1, uint8_t *cov, Span &sp) {
  int x0, x1, ya, yb;
  if (!clip(o.box, y0, y1, x0, x1, ya, yb))
    return;
  if (o.kind == Op::TEXT) {
    cover_text(o, y0, y1, cov, sp);
    return;
  }
  if (o.kind == Op::LINE || o.kind == Op::GLOW_LINE) {
    float m = o.kind == Op::LINE ? o.w / 2 + 1 : o.w / 2 + 3;
    for (size_t i = 0; i + 3 < o.pts.size(); i += 2) {
      float ax = o.pts[i], ay = o.pts[i + 1], bx = o.pts[i + 2], by = o.pts[i + 3];
      Box sb{std::min(ax, bx) - m, std::min(ay, by) - m, std::max(ax, bx) + m, std::max(ay, by) + m};
      int sx0, sx1, sya, syb;
      if (!clip(sb, y0, y1, sx0, sx1, sya, syb))
        continue;
      for (int y = sya; y < syb; y++)
        for (int x = sx0; x < sx1; x++) {
          float d = to_segment(x + 0.5f, y + 0.5f, ax, ay, bx, by);
          put(cov, y0, x, y, o.kind == Op::LINE ? clamp01(o.w / 2 + 0.5f - d) : 1 - smooth((d - o.w / 2 + 2.5f) / 5),
              sp);
        }
    }
    return;
  }
  // a convex polygon's edges, signed by its winding
  float wind = 0;
  if (o.kind == Op::POLY)
    for (size_t i = 0; i + 1 < o.pts.size(); i += 2) {
      size_t j = (i + 2) % o.pts.size();
      wind += o.pts[i] * o.pts[j + 1] - o.pts[j] * o.pts[i + 1];
    }
  float cx = (o.box.x0 + o.box.x1) / 2, cy = (o.box.y0 + o.box.y1) / 2;
  for (int y = ya; y < yb; y++)
    for (int x = x0; x < x1; x++) {
      float px = x + 0.5f, py = y + 0.5f, c = 0;
      switch (o.kind) {
        case Op::DOT:
          c = clamp01(o.r + 0.5f - hypotf(px - cx, py - cy));
          break;
        case Op::RING:
          c = clamp01(o.w / 2 + 0.5f - fabsf(hypotf(px - cx, py - cy) - o.r));
          break;
        case Op::GLOW_DOT:
          c = 1 - smooth((hypotf(px - cx, py - cy) - o.r + 2.5f) / 5);
          break;
        case Op::RECT: {
          float hx = (o.box.x1 - o.box.x0) / 2 - 1, hy = (o.box.y1 - o.box.y0) / 2 - 1;
          float qx = fabsf(px - cx) - (hx - o.r), qy = fabsf(py - cy) - (hy - o.r);
          float d = hypotf(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - o.r;
          c = clamp01(0.5f - d);
          break;
        }
        case Op::POLY: {
          float in = 1e9f;
          for (size_t i = 0; i + 1 < o.pts.size(); i += 2) {
            size_t j = (i + 2) % o.pts.size();
            float ex = o.pts[j] - o.pts[i], ey = o.pts[j + 1] - o.pts[i + 1], l = hypotf(ex, ey);
            if (l > 0)
              in = std::min(in, (wind > 0 ? 1 : -1) * (ex * (py - o.pts[i + 1]) - ey * (px - o.pts[i])) / l);
          }
          c = clamp01(in + 0.5f);
          break;
        }
        default:
          break;
      }
      put(cov, y0, x, y, c, sp);
    }
}

inline uint32_t mix(uint32_t a, uint32_t b, uint32_t k) {  // a toward b by k / 255
  uint32_t out = 0;
  for (int s = 0; s < 24; s += 8) {
    int ca = (int) (a >> s & 255), cb = (int) (b >> s & 255);
    out |= (uint32_t) (ca + ((cb - ca) * (int) k + 127) / 255) << s;
  }
  return out;
}

// The picture's band: its pixels with the soft ops drawn in
struct Soft {
  int y0 = INT_MIN;
  uint32_t rgb[W * BAND];
  uint8_t cov[W * (BAND + 2 * PAD)];
  uint8_t cast[W * (BAND + 2 * PAD)];  // what casts the shadow, then the shadow
};

// The overlay's band, premultiplied 0xAARRGGBB
struct Sharp {
  int y0 = INT_MIN;
  uint32_t px[W * BAND];
  uint8_t cov[W * BAND];
};

inline bool casts(const Op &o) { return o.kind != Op::SCRIM && o.kind != Op::GLOW_DOT && o.kind != Op::GLOW_LINE; }

// Rows y0.. of the picture: base, the scrims, the glows, the shadow, then the lines and boxes
inline void draw_soft(const Layer &l, const std::function<uint32_t(int, int)> &base, Soft &b, int y0) {
  b.y0 = y0;
  int y1 = std::min(y0 + BAND, H);
  for (int y = y0; y < y1; y++)
    for (int x = 0; x < W; x++)
      b.rgb[(y - y0) * W + x] = base(x, y);
  auto paint = [&](const Op &o) {
    Span sp;
    cover(o, y0, y1, b.cov, sp);
    if (sp.empty())
      return;
    for (int y = sp.y0; y < sp.y1; y++)
      for (int x = sp.x0; x < sp.x1; x++) {
        uint8_t &c = b.cov[(y - y0) * W + x];
        uint32_t &p = b.rgb[(y - y0) * W + x];
        p = mix(p, o.rgb, (uint32_t) (c * o.alpha + 0.5f));
        c = 0;
      }
  };
  for (const Op &o : l.ops)
    if (o.kind == Op::SCRIM)
      for (int y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) {
          char e = o.text[0];
          float d = e == 't' ? y : e == 'b' ? H - 1 - y : e == 'l' ? x : W - 1 - x;
          float t = powf(clamp01(1 - d / o.r), 1.6f) * o.alpha;
          uint32_t &p = b.rgb[(y - y0) * W + x];
          p = mix(p, o.rgb, (uint32_t) (t * 255 + 0.5f));
        }
  for (const Op &o : l.ops)
    if (o.kind == Op::GLOW_DOT || o.kind == Op::GLOW_LINE)
      paint(o);
  if (l.shadow > 0) {
    // what casts it in a band PAD rows bigger, blurred twice with 1 2 1 each way
    int c0 = y0 - PAD, c1 = std::min(y1 + PAD, H), rows = BAND + 2 * PAD;
    memset(b.cast, 0, sizeof b.cast);
    bool any = false;
    for (const Op &o : l.ops)
      if (casts(o)) {
        Span sp;
        cover(o, std::max(c0, 0), c1, b.cov, sp);
        if (sp.empty())
          continue;
        any = true;
        int shift = (std::max(c0, 0) - c0) * W;
        for (int y = sp.y0; y < sp.y1; y++)
          for (int x = sp.x0; x < sp.x1; x++) {
            uint8_t &c = b.cov[(y - std::max(c0, 0)) * W + x];
            uint8_t &s = b.cast[shift + (y - std::max(c0, 0)) * W + x];
            s = std::max<uint8_t>(s, (uint8_t) (c * o.alpha + 0.5f));
            c = 0;
          }
      }
    if (any) {
      for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < rows; y++) {
          uint8_t *r = b.cast + y * W, prev = 0;
          for (int x = 0; x < W; x++) {
            uint8_t here = r[x], next = x + 1 < W ? r[x + 1] : 0;
            r[x] = (uint8_t) ((prev + 2 * here + next + 2) / 4);
            prev = here;
          }
        }
        for (int x = 0; x < W; x++) {
          uint8_t prev = 0;
          for (int y = 0; y < rows; y++) {
            uint8_t here = b.cast[y * W + x], next = y + 1 < rows ? b.cast[(y + 1) * W + x] : 0;
            b.cast[y * W + x] = (uint8_t) ((prev + 2 * here + next + 2) / 4);
            prev = here;
          }
        }
      }
      for (int y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) {
          uint8_t s = b.cast[(y - c0) * W + x];
          if (s) {
            uint32_t &p = b.rgb[(y - y0) * W + x];
            p = mix(p, INK, (uint32_t) (s * l.shadow + 0.5f));
          }
        }
    }
  }
  for (const Op &o : l.ops)
    if (o.soft && o.kind != Op::SCRIM && o.kind != Op::GLOW_DOT && o.kind != Op::GLOW_LINE)
      paint(o);
}

// Rows y0.. of the overlay, premultiplied
inline void draw_sharp(const Layer &l, Sharp &b, int y0) {
  b.y0 = y0;
  int y1 = std::min(y0 + BAND, H);
  std::fill(std::begin(b.px), std::end(b.px), 0);
  for (const Op &o : l.ops) {
    if (o.soft)
      continue;
    Span sp;
    cover(o, y0, y1, b.cov, sp);
    for (int y = sp.y0; y < sp.y1; y++)
      for (int x = sp.x0; x < sp.x1; x++) {
        uint8_t &c = b.cov[(y - y0) * W + x];
        uint32_t a = (uint32_t) (c * o.alpha + 0.5f), &p = b.px[(y - y0) * W + x], out = 0;
        for (int s = 0; s < 32; s += 8) {
          uint32_t src = s == 24 ? 255 : o.rgb >> s & 255;
          out |= (src * a / 255 + (p >> s & 255) * (255 - a) / 255) << s;
        }
        p = out;
        c = 0;
      }
  }
}

// ---------------------------------------------------------------- the overlay's colours

// Each ink a few opacities, in the panel's 15ths: the ones it's drawn at, and between them and 0 so its edges have
// steps to land on. 15 of them shared out between the inks, and one clear.
inline std::vector<uint32_t> colours_of(const Layer &l) {
  std::map<uint32_t, std::vector<int>> inks;  // rgb, the opacities it's drawn at
  for (const Op &o : l.ops)
    if (!o.soft) {
      auto &v = inks[o.rgb & 0xFFFFFF];
      int a = std::clamp((int) lroundf(o.alpha * 15), 1, 15);
      if (std::find(v.begin(), v.end(), a) == v.end())
        v.push_back(a);
    }
  std::vector<uint32_t> out;
  if (inks.empty())
    return out;
  int each = std::max(1, 15 / (int) inks.size());
  for (auto &it : inks) {
    std::vector<int> &v = it.second;
    std::sort(v.rbegin(), v.rend());
    if ((int) v.size() > each)
      v.resize(each);
    while ((int) v.size() < each) {  // halves the widest gap, down to 0
      std::sort(v.begin(), v.end());
      int best = -1, gap = 1;
      for (size_t i = 0; i < v.size(); i++) {
        int g = v[i] - (i ? v[i - 1] : 0);
        if (g > gap) {
          gap = g;
          best = (int) i;
        }
      }
      if (best < 0)
        break;
      v.push_back(v[best] - gap / 2);
    }
    for (int a : v)
      if (out.size() < 15)
        out.push_back((uint32_t) (a * 17) << 24 | it.first);
  }
  out.push_back(0);
  return out;
}

// ---------------------------------------------------------------- a picture with its marks

struct Marks {
  std::string name;  // the picture's, and its overlay's with "_marks"
  Layer layer;
  std::function<uint32_t(int, int)> base;  // the picture without them, at the screen's size
  std::unique_ptr<Soft> soft;              // while the picture is made
  std::unique_ptr<Sharp> sharp;            // while the overlay is made
  int ox = 0, oy = 0, ow = 0, oh = 0;      // the overlay's place and size, 0 wide for none
  bool on_panel = false;                   // the picture is
};
inline std::map<std::string, Marks> all;  // by picture

inline std::string overlay_of(const std::string &name) { return name + "_marks"; }

inline uint32_t soft_pixel(Marks &m, int x, int y) {
  if (x < 0 || y < 0 || x >= W || y >= H)
    return 0;
  if (m.layer.ops.empty())
    return m.base(x, y);
  if (!m.soft)
    m.soft.reset(new (std::nothrow) Soft());
  if (!m.soft)
    return m.base(x, y);
  int y0 = y / BAND * BAND;
  if (m.soft->y0 != y0)
    draw_soft(m.layer, m.base, *m.soft, y0);
  return m.soft->rgb[(y - y0) * W + x];
}

inline uint32_t sharp_pixel(Marks &m, int x, int y) {
  int sx = x + m.ox, sy = y + m.oy;
  if (!m.sharp)
    m.sharp.reset(new (std::nothrow) Sharp());
  if (!m.sharp || sx >= W || sy >= H)
    return 0;
  int y0 = sy / BAND * BAND;
  if (m.sharp->y0 != y0)
    draw_sharp(m.layer, *m.sharp, y0);
  uint32_t p = m.sharp->px[(sy - y0) * W + sx], a = p >> 24;
  if (x == m.ow - 1 && y == m.oh - 1)
    m.sharp.reset();  // the last pixel of a pass, the next starts over
  if (!a)
    return 0;
  uint32_t out = a << 24;
  for (int s = 0; s < 24; s += 8)
    out |= std::min<uint32_t>(255, ((p >> s & 255) * 255 + a / 2) / a) << s;
  return out;
}

// Picture name's marks are drawn into it and shown over it, from set() on. After picture::add().
inline void on(const std::string &name) {
  auto it = picture::sources.find(name);
  if (it == picture::sources.end())
    return;
  Marks &m = all[name];
  m.name = name;
  picture::Source &s = it->second;
  m.base = s.pixel;
  s.pixel = [&m](int x, int y) { return soft_pixel(m, x, y); };
  auto done = s.done;
  s.done = [&m, done]() {
    m.soft.reset();
    if (done)
      done();
  };
  auto panel = s.on_panel;
  s.on_panel = [&m, panel](bool on) {
    m.on_panel = on;
    std::string k = overlay_of(m.name);
    if (on && m.ow)
      overlay::show(k);
    else if (!on && overlay::find(k)) {
      overlay::clear(k);
      m.sharp.reset();
    }
    if (panel)
      panel(on);
  };
}

// Picture name's marks are now l: drawn into it as it's made again, and their overlay made again over it
inline void set(const std::string &name, Layer l) {
  auto it = all.find(name);
  if (it == all.end())
    return;
  Marks &m = it->second;
  std::string k = overlay_of(name);
  if (overlay::find(k))
    overlay::clear(k);
  m.layer = std::move(l);
  drop(m.layer.boxes);  // they were for laying it out
  m.soft.reset();
  m.sharp.reset();
  // the overlay covers what the sharp ops do, an even size so its rows are whole bytes
  Box b{(float) W, (float) H, 0, 0};
  for (const Op &o : m.layer.ops)
    if (!o.soft)
      b = {std::min(b.x0, o.box.x0), std::min(b.y0, o.box.y0), std::max(b.x1, o.box.x1), std::max(b.y1, o.box.y1)};
  m.ow = 0;
  if (b.x0 < b.x1) {
    m.ox = std::max(0, (int) floorf(b.x0)) & ~1;
    m.oy = std::max(0, (int) floorf(b.y0)) & ~1;
    m.ow = std::min(W - m.ox, ((int) ceilf(b.x1) - m.ox + 2) & ~1);
    m.oh = std::min(H - m.oy, ((int) ceilf(b.y1) - m.oy + 2) & ~1);
    overlay::Source s{m.ow, m.oh, 1, 500, m.ox, m.oy, 1, [&m](int, int x, int y) { return sharp_pixel(m, x, y); }};
    s.colours = colours_of(m.layer);
    s.fade_ms = 300;
    overlay::add(k, std::move(s));
  }
  picture::changed(name);  // the overlay comes once the picture is on the panel again
}

// ---------------------------------------------------------------- times and dates, as the panel writes them

// "16:04"
inline std::string clock(int h, int m) {
  char b[8];
  snprintf(b, sizeof b, "%02d:%02d", h, m);
  return b;
}
// "1 Nov"
inline std::string day(int d, int month) { return std::to_string(d) + " " + MONTHS[(month + 11) % 12]; }

}  // namespace flexit::panel::marks
