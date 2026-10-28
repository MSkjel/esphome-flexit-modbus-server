// The screensaver for packages/panel/camera.yaml's camera, made from what the panel shows when Home Assistant asks
// for it, as a GIF of the 16-colour picture with the animations playing over it where they were put, a few hundred
// pixels a step, see picture_gif.h. A JPEG picture, a photo, is black in it, as the ESP doesn't decode JPEGs. Only
// while no picture or animation is being made or sent, as that takes the RAM. Without camera.yaml there's nothing here.
#pragma once
#include <algorithm>
#include "animation.h"
#include "picture.h"
#include "picture_gif.h"

#ifdef USE_FLEXIT_PANEL_CAMERA
namespace flexit::panel::snapshot {

using picture::S;

inline constexpr uint32_t CAPS[] = {48 * 1024, 24 * 1024};  // the GIF's room, the smaller one if the RAM is short
// What making it takes besides: the LZW's table, the animations' frames and a row of the picture
inline constexpr uint32_t WORK = picture::Gif::TABLE * 4 + ANIM_POOL + PIC_MAX_W;

inline esphome::flexit_panel_camera::PanelCamera *cam = nullptr;

struct Making {
  bool on = false;
  uint32_t failed = 0;  // the room the first frame didn't fit, until what's shown changes. Only more is tried.
  picture::Scene scene;
  const uint8_t *pic = nullptr;  // the picture's data it's made from, which mustn't go from under it
  std::vector<uint8_t> out;
};
inline Making M;

inline void stop() {
  if (M.on)
    M.scene.finish();
  M.on = false;
  drop(M.out);
}

// What the panel shows changed: the camera's GIF is old, and one being made is dropped
inline void changed() {
  cam->invalidate();
  stop();
  M.failed = 0;
}

inline void attach(esphome::flexit_panel_camera::PanelCamera *c) {
  cam = c;
  picture::on_shown = changed;
  anim::on_changed = changed;
}

// The scene the panel shows: its picture, and the copies of the animations it has, in the order of their places
inline void set_scene() {
  picture::Scene &s = M.scene;
  s = picture::Scene();
  s.pic = S.shown.data.empty() || S.shown.format != PIC_PALETTE ? nullptr : &S.shown;  // a JPEG isn't decoded here
  M.pic = S.shown.data.data();
  std::vector<std::pair<uint8_t, picture::Scene::Copy>> copies;
  for (int i = 0; i < ANIM_SLOTS; i++) {
    const anim::Anim &a = anim::anims[i];
    if (a.name.empty() || a.state != anim::SENT)
      continue;
    s.slots[i] = {a.data.data(), (uint32_t) a.data.size(), a.pal, a.w, a.h, a.scale, a.transp, a.frame_rate};
    for (auto &c : a.copies) {
      anim::Look l = anim::look(a, c);
      if (c.used && !l.hidden)
        copies.push_back({c.place, {i, l.x, l.y, l.flip_x, l.flip_y, c.own, c.start}});
    }
  }
  std::sort(copies.begin(), copies.end(), [](const auto &p, const auto &q) { return p.first < q.first; });
  for (auto &c : copies)
    s.copies.push_back(c.second);
}

inline bool begin() {
  for (uint32_t cap : CAPS) {
    if (cap <= M.failed || !has_ram(cap + WORK, cap))
      continue;
    set_scene();
    M.out.resize(cap);
    M.scene.start(M.out);
    M.on = true;
    return true;
  }
  return false;
}

// The GIF's done, n bytes, cut short after a whole frame if it was full
inline void done(uint32_t n, bool cut) {
  M.on = false;
  int frames = M.scene.gif.frames;
  if (n > M.out.size()) {
    ESP_LOGW("panel", "The camera's picture doesn't fit %u bytes", (unsigned) M.out.size());
    M.failed = std::max<uint32_t>(M.failed, (uint32_t) M.out.size());
    drop(M.out);
    return;
  }
  M.out.resize(n);
  if (has_ram(n, n))
    M.out.shrink_to_fit();
  ESP_LOGD("panel", "Picture for the camera: GIF, %d frames, %u bytes%s", frames, (unsigned) n,
           cut ? ", cut short where it was full" : "");
  cam->publish(std::make_shared<const std::vector<uint8_t>>(std::move(M.out)));
  drop(M.out);
}

// Every loop after the pictures and before the animations, so an animation about to be made finds the RAM free,
// until deadline_us
inline void loop(uint32_t deadline_us) {
  if (!cam)
    return;
  if (anim::busy()) {
    stop();  // and the picture kept for the next requests goes too, the connections sending it keep theirs
    cam->invalidate();
    return;
  }
  if (!cam->wanted() || (S.phase != picture::IDLE && S.phase != picture::START)) {
    stop();  // nobody's asking now, or a picture is being made
    return;
  }
  if (M.on && S.shown.data.data() != M.pic)
    changed();  // the panel lost the picture, so it's another one
  if (!M.on && !begin())
    return;
  bool more = true;
  while (more && before(deadline_us))
    more = M.scene.step();
  if (!more) {
    bool cut = M.scene.gif.full();
    done(M.scene.finish(), cut);
  }
}

}  // namespace flexit::panel::snapshot
#else
namespace flexit::panel::snapshot {
inline void loop(uint32_t) {}
}  // namespace flexit::panel::snapshot
#endif
