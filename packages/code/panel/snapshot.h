// The screensaver picture for packages/panel/camera.yaml's camera, made from what the panel shows when Home Assistant
// asks for it. A JPEG goes as it is, 16 colours are encoded a few MCUs per loop, nothing shown is a black screen.
// Only while no picture or animation is being made or sent, as that takes the RAM. Without camera.yaml there's
// nothing here.
#pragma once
#include "animation.h"
#include "picture.h"
#include "picture_snap.h"

#ifdef USE_FLEXIT_PANEL_CAMERA
namespace flexit::panel::snapshot {

using picture::S;

inline constexpr uint32_t CAP = 24 * 1024;   // a 16-colour picture's JPEG, tried at lower qualities until it fits
inline constexpr uint32_t BLACK_CAP = 4096;  // the black screen's
inline constexpr int QUALITIES[] = {85, 70, 50};

inline esphome::flexit_panel_camera::PanelCamera *cam = nullptr;

struct Making {
  bool on = false, failed = false;  // failed: it didn't fit at any quality, until the picture changes
  size_t tries = 0;                 // into QUALITIES
  picture::Snap snap;
  std::vector<uint8_t> out;
};
inline Making M;

inline void stop() {
  if (M.on)
    M.snap.finish();
  M.on = false;
  drop(M.out);
}

// What the panel shows changed: the camera's JPEG is old, and one being made is dropped
inline void changed() {
  cam->invalidate();
  stop();
  M.tries = 0;
  M.failed = false;
}

inline void attach(esphome::flexit_panel_camera::PanelCamera *c) {
  cam = c;
  picture::on_shown = changed;
}

inline bool begin() {
  const picture::Pic *p = S.shown.data.empty() ? nullptr : &S.shown;
  uint32_t cap = p ? CAP : BLACK_CAP;
  if (!has_ram(cap, cap))
    return false;
  M.out.resize(cap);
  M.snap.start(p, p ? QUALITIES[M.tries] : 50, M.out);
  M.on = true;
  return true;
}

// The JPEG's done, n bytes. Again at a lower quality if it didn't fit.
inline void done(uint32_t n) {
  M.on = false;
  if (n > M.out.size()) {
    drop(M.out);
    if (++M.tries < std::size(QUALITIES))
      return;
    ESP_LOGW("panel", "Picture %s: too big for the camera even at quality %d", S.shown.name.c_str(),
             QUALITIES[M.tries - 1]);
    M.failed = true;
    return;
  }
  M.out.resize(n);
  if (has_ram(n, n))
    M.out.shrink_to_fit();
  ESP_LOGD("panel", "Picture for the camera: %u bytes", (unsigned) n);
  cam->publish(std::make_shared<const std::vector<uint8_t>>(std::move(M.out)));
  drop(M.out);
  M.tries = 0;
}

// Every loop after the pictures and before the animations, so an animation about to be made finds the RAM free,
// until deadline_us
inline void loop(uint32_t deadline_us) {
  if (!cam)
    return;
  if (anim::busy()) {
    stop();  // and the JPEG kept for the next requests goes too, the connections sending it keep theirs
    cam->invalidate();
    return;
  }
  if (!cam->wanted() || (S.phase != picture::IDLE && S.phase != picture::START)) {
    stop();  // nobody's asking now, or a picture is being made
    return;
  }
  const picture::Pic &p = S.shown;
  if (!M.on && p.format == PIC_JPEG && !p.data.empty()) {
    if (has_ram(p.data.size(), p.data.size()))
      cam->publish(std::make_shared<const std::vector<uint8_t>>(p.data));
    return;
  }
  if (M.failed || (!M.on && !begin()))
    return;
  bool more = true;
  while (more && before(deadline_us))
    more = M.snap.step();
  if (!more)
    done(M.snap.finish());
}

}  // namespace flexit::panel::snapshot
#else
namespace flexit::panel::snapshot {
inline void loop(uint32_t) {}
}  // namespace flexit::panel::snapshot
#endif
