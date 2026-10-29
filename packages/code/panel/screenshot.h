// What the panel shows, for the camera in packages/panel/screen_camera.yaml. The panel sends it as 64x8 tiles, each a
// JPEG restart interval, and after the first time only the tiles that changed. We keep them while someone looks and
// put them together behind headers of our own, see K_SHOT in plink_keys.h.
#pragma once
#include <cstring>
#include <functional>
#include <memory>
#include <vector>
#include "panel_link.h"
#include "pjpeg.h"
#include "plink.h"
#include "util.h"

namespace flexit::panel::shot {

inline constexpr uint32_t GAP_MS = 1000;        // between the starts of shots while someone keeps looking
inline constexpr uint32_t PARTIAL_MS = 4000;    // a request waits this long for the shot, then gets what we have
inline constexpr uint32_t PANEL_MS = 120000;    // a shot not done by then isn't coming, the panel lost the ask
inline constexpr uint32_t KEEP_MS = 60000;      // the tiles are let go this long after the last look
inline constexpr size_t TILES_RAM = 24 * 1024;  // about what the tiles take
inline constexpr int TILE_BYTES = 1024;         // more than a tile can be, the panel keeps it in 400

// A part of the JPEG, as flexit_panel_camera takes them. A tile that changes is a new one, so an image being sent
// keeps the old.
using Part = std::shared_ptr<const std::vector<uint8_t>>;

struct State {
  std::function<bool()> wanted;  // someone waits for an image
  std::function<void(std::vector<Part> &&)> publish;
  int quality = 0;                                     // 0 until attach()
  std::vector<Part> tiles;                             // SHOT_TILES while someone looks, none for a black one
  std::vector<Part> marks;                             // the headers, a black tile, RST0..RST7 and EOI
  std::vector<uint8_t> in;                             // the tile coming in, which goes in its place once it's whole
  uint8_t head[SHOT_TILE_HEAD];                        // its head
  int head_n = 0, in_left = 0;                         // head bytes so far, then the bytes it still needs
  bool lost = false;                                   // a head that made no sense, nothing more until SHOT_DONE
  bool asked = false, all = true;                      // a SHOT is out, the next asks for every tile
  bool waited = false;                                 // wanted() at the last loop
  bool warned = false;                                 // that the panel doesn't answer
  uint32_t asked_ms = 0, looked_ms = 0, since_ms = 0;  // when the SHOT went, someone last looked, the wait began
};
inline State S;

inline uint32_t black(void *, int, int) { return 0; }

inline Part part(const uint8_t *b, size_t n) { return std::make_shared<std::vector<uint8_t>>(b, b + n); }

// The tiles as a JPEG, a black one for each we don't have. Only the parts are new, the bytes are shared.
inline std::vector<Part> jpeg() {
  enum { HEAD, NONE, RST, EOI = RST + 8 };
  if (S.marks.empty()) {
    uint8_t head[1024], none[128];
    S.marks.push_back(part(head, pjpeg_head(head, sizeof head, PIC_MAX_W, PIC_MAX_H, S.quality, SHOT_TILE_W / 16)));
    S.marks.push_back(part(none, pjpeg_tile(none, sizeof none, S.quality, 0, black, nullptr, 0, 0, SHOT_TILE_W / 16)));
    for (int i = 0; i < 8; i++) {
      uint8_t rst[2] = {0xFF, (uint8_t) (0xD0 + i)};
      S.marks.push_back(part(rst, 2));
    }
    static const uint8_t eoi[2] = {0xFF, 0xD9};
    S.marks.push_back(part(eoi, 2));
  }
  std::vector<Part> j;
  j.reserve(2 * SHOT_TILES + 1);
  j.push_back(S.marks[HEAD]);
  for (int t = 0; t < SHOT_TILES; t++) {
    if (t)
      j.push_back(S.marks[RST + (t - 1) % 8]);  // RST0..RST7 in turn between them
    j.push_back(S.tiles[t] ? S.tiles[t] : S.marks[NONE]);
  }
  j.push_back(S.marks[EOI]);
  return j;
}

// The camera gets what we have. A stream that still wants more waits from now.
inline void give() {
  if (!has_ram(4096, 4096))  // what jpeg() takes, and the API's reader
    return;
  S.publish(jpeg());
  S.since_ms = millis();
}

// The panel's PIC_DATA, straight from the link: the tiles one after the other, each behind its head
inline void data(const uint8_t *b, int n) {
  if (!S.asked || S.tiles.empty())
    return;
  for (int i = 0; i < n && !S.lost;) {
    if (S.head_n < SHOT_TILE_HEAD) {
      S.head[S.head_n++] = b[i++];
      if (S.head_n < SHOT_TILE_HEAD)
        continue;
      S.in_left = plink_get16(S.head + SHOT_TILE_LEN);
      if (!S.in_left) {  // the zeros at the end
        S.head_n = 0;
      } else if (S.head[SHOT_TILE_NUM] >= SHOT_TILES || S.in_left > TILE_BYTES) {
        S.lost = true;  // the panel thinks we have the tiles from here on
        S.all = true;
      } else {
        S.in.reserve(S.in_left);
      }
      continue;
    }
    int k = std::min(S.in_left, n - i);
    S.in.insert(S.in.end(), b + i, b + i + k);
    S.in_left -= k;
    i += k;
    if (!S.in_left) {  // whole, in its place
      S.tiles[S.head[SHOT_TILE_NUM]] = std::make_shared<std::vector<uint8_t>>(std::move(S.in));
      S.in = {};
      S.head_n = 0;
    }
  }
}

// Ready for the next shot's tiles. A tile cut short would have its rest taken for a head.
inline void data_end() {
  if (S.head_n == SHOT_TILE_HEAD && S.in_left)
    S.all = true;
  S.in.clear();
  S.head_n = 0;
  S.lost = false;
}

// K_SHOT_DONE, with the number of tiles the panel sent, only logged
inline void done([[maybe_unused]] uint32_t n) {
  if (!S.asked)
    return;
  data_end();
  S.asked = false;
  S.warned = false;
  ESP_LOGV("panel", "Screenshot: %u tiles in %u ms", (unsigned) n, (unsigned) (millis() - S.asked_ms));
  if (S.wanted())
    give();
}

// The panel or we started, so it doesn't know what we have. Unsent asks are gone.
inline void restarted() {
  S.asked = false;
  S.all = true;
  data_end();
}

// The camera, a flexit_panel_camera, and the JPEG quality for its images
template<typename Cam> void attach(Cam &&camera, int quality) {
  auto *cam = ptr_of(camera);
  S.wanted = [cam] { return cam->wanted(); };
  S.publish = [cam](std::vector<Part> &&j) { cam->publish(std::move(j)); };
  S.quality = std::clamp(quality, 1, 100);
  link::got_shot = data;
}

// From panel::loop(). A shot while someone wants an image, the tiles kept until a minute after.
inline void loop() {
  if (!S.quality)
    return;
  uint32_t now = millis();
  bool want = S.wanted();
  if (want && !S.waited)
    S.since_ms = now;
  S.waited = want;
  if (want)
    S.looked_ms = now;
  if (want && !S.tiles.empty() && now - S.since_ms > PARTIAL_MS)
    give();  // the first shot takes a while
  if (S.asked) {
    if (now - S.asked_ms > PANEL_MS) {
      if (!S.warned) {
        ESP_LOGW("panel", "The panel didn't finish a screenshot, asking again");
      }
      S.warned = true;
      restarted();
    }
    return;
  }
  if (!want) {
    if (!S.tiles.empty() && now - S.looked_ms > KEEP_MS) {
      drop(S.tiles);
      drop(S.marks);
      drop(S.in);
      S.all = true;
    }
    return;
  }
  // Not while a picture or an animation goes, the panel's tiles would take their room
  if (!link::connected || link::waiting() || now - S.asked_ms < GAP_MS)
    return;
  if (!has_ram(S.tiles.empty() ? TILES_RAM : 8192, 4096))  // the tiles, or those that change and the images
    return;
  if (S.tiles.empty()) {
    S.tiles.resize(SHOT_TILES);
    S.all = true;
  }
  if (link::send(K_SHOT, (uint32_t) S.quality | (S.all ? SHOT_ALL : 0))) {
    S.asked = true;
    S.all = false;
    S.asked_ms = now;
  }
}

}  // namespace flexit::panel::shot
