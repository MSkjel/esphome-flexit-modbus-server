// A picture's marks (marks.h): the soft ones drawn into it, the sharp ones shown over it as a still overlay in front
// of the animations. on() hooks a picture up, set() gives it a new layer.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include "marks.h"
#include "overlay.h"
#include "picture.h"

namespace flexit::panel::marks {

struct Marks {
  std::string name;   // the picture's, and its overlay's with "_marks"
  std::string label;  // its overlay's, for Panel Animation Status
  Layer layer;
  bool into = false;                       // the layer draws into the picture
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
  if (!m.into)
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

// Picture name's marks are drawn into it and shown over it, from set() on, label in Panel Animation Status. After
// picture::add().
inline void on(const std::string &name, const std::string &label = "") {
  auto it = picture::sources.find(name);
  if (it == picture::sources.end())
    return;
  Marks &m = all[name];
  m.name = name;
  m.label = label.empty() ? name + " text" : label;
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

// Picture name's marks are now l, unless they're the same. What it draws into the picture comes with the picture
// made again, the overlay with it. Without that the overlay changes at once.
inline void set(const std::string &name, Layer l) {
  auto it = all.find(name);
  if (it == all.end())
    return;
  Marks &m = it->second;
  drop(l.boxes);  // they were for laying it out
  if (l == m.layer)
    return;
  std::string k = overlay_of(name);
  if (overlay::find(k))
    overlay::clear(k);
  bool was_into = m.into;
  m.layer = std::move(l);
  m.into = into_picture(m.layer);
  m.soft.reset();
  m.sharp.reset();
  // the overlay covers what the sharp ops do
  Box b{(float) W, (float) H, 0, 0};
  for (const Op &o : m.layer.ops)
    if (!o.soft)
      b = {std::min(b.x0, o.box.x0), std::min(b.y0, o.box.y0), std::max(b.x1, o.box.x1), std::max(b.y1, o.box.y1)};
  m.ow = 0;
  if (b.x0 < b.x1) {
    m.ox = std::max(0, (int) floorf(b.x0));
    m.oy = std::max(0, (int) floorf(b.y0));
    m.ow = std::min(W - m.ox, (int) ceilf(b.x1) - m.ox + 1);
    m.oh = std::min(H - m.oy, (int) ceilf(b.y1) - m.oy + 1);
    overlay::Source s{m.ow, m.oh, 1, 500, m.ox, m.oy, 1, [&m](int, int x, int y) { return sharp_pixel(m, x, y); }};
    s.colours = colours_of(m.layer);
    s.fade_ms = 300;
    s.label = m.label;
    overlay::add(k, std::move(s));
  }
  if (was_into || m.into)
    picture::changed(name);
  else if (m.on_panel && m.ow)
    overlay::show(k);
}

}  // namespace flexit::panel::marks
