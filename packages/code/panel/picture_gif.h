// What the panel shows as an animated GIF for Home Assistant's camera, see snapshot.h: the 16-colour picture centred
// on the screen and the animations playing over it where they were put, in the panel's own colours and times. Each
// animation's records are played into frames of the ESP's own as the panel plays them. The GIF loops where all of
// them do, or after MAX_MS. Plain C++ without ESPHome.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "gif.h"
#include "picture_plan.h"

namespace flexit::panel::picture {

// A row packed by plink_pack_row back into its n bytes
void unpack_row(const uint8_t *in, uint8_t *out, size_t n);

struct Scene {
  static constexpr uint32_t MAX_MS = 30000;  // the longest the GIF goes before it loops
  static constexpr int STEP_PX = 256;
  // Hundredths a frame shows at least, as browsers show 0 and 1 as 10. A change sooner than that waits for the
  // next frame.
  static constexpr uint32_t MIN_CS = 2;                   // pixels a step() takes
  static constexpr uint8_t BLACK = 16 + 16 * ANIM_SLOTS;  // round the picture, after the animations' 16 each

  // One of the panel's animation slots, as anim::Anim has it. data = nullptr for none.
  struct Slot {
    const uint8_t *data = nullptr;
    uint32_t len = 0;
    const uint32_t *pal = nullptr;  // 16 colours
    int w = 0, h = 0, scale = 1;
    uint8_t transp = ANIM_OPAQUE;  // the transparent colour, ANIM_OPAQUE = none
    int rate = 100;                // percent of the records' speed, 0 = paused
  };
  // A copy on the screen, the last on top. own: a frame of its own, start records in.
  struct Copy {
    int slot = 0, x = 0, y = 0;
    bool flip_x = false, flip_y = false, own = false;
    int start = 0;
  };

  const Pic *pic = nullptr;  // 16 colours, nullptr = black. Has to stay as it is until it's done, like the slots.
  Slot slots[ANIM_SLOTS];
  std::vector<Copy> copies;

  // Filled in by start()
  struct Player {  // a frame being played: a slot's, or a copy's own
    int slot;
    std::vector<uint8_t> frame;  // w x h, 2 pixels a byte
    size_t next;                 // the record applied next, into at
    uint32_t due;                // ms it's applied at
  };
  std::vector<uint32_t> at[ANIM_SLOTS];  // where each slot's records start, then its end. Empty: it isn't drawn.
  std::vector<Player> players;
  std::vector<int> player_of;  // each copy's
  struct Box {
    int x0, y0, x1, y1;
  };
  std::vector<Box> boxes;  // each copy's on the screen, so pixel() passes the others by quickly
  uint32_t now = 0, end_ms = 0;
  int ox = 0, oy = 0;  // where the picture is
  int row_y = -1;      // the picture's row in row
  std::vector<uint8_t> row;
  bool looped = false;  // it ends where all its animations start over, not cut short
  Gif gif;

  // Into out, as much as fits. The first frame is the whole screen, step() writes it and the ones after.
  void start(std::vector<uint8_t> &out);
  bool step();  // false when it's done
  // The GIF's length, over out's size if not even the first frame fit. Frees what it worked in.
  uint32_t finish();

  // ms record i of slot s shows for
  uint32_t period(int s, size_t i) const;
  bool advance();
  static uint8_t pixel(void *ctx, int x, int y);
};

}  // namespace flexit::panel::picture
