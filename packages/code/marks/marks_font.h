// The glyphs marks.h draws text with, from scripts/marks_font.py. Plain C++ without ESPHome.
#pragma once
#include <cstdint>

namespace flexit::panel::marks {

struct Glyph {
  uint16_t at;       // in its face's data: w * h coverages of 0..15, two to a byte, row by row
  uint16_t advance;  // to the next glyph's pen, in 1/16 px
  int8_t x, y;       // its top left from the pen on the baseline
  uint8_t w, h;      // 0 for a space
};

struct Face {
  const uint16_t *codes;  // Unicode, in order, shared by the faces with the same characters
  const Glyph *glyphs;    // the codes' glyphs
  uint16_t count;
  const uint8_t *data;
  uint8_t ascent, height;  // px
  float size;              // px, as the font was asked for
};

// scripts/marks_font.py's FACES, in its order
enum FaceId { CAPS, SMALL, LABEL, TEXT, TITLE, BIG, HUGE };
extern const Face FACES[];

}  // namespace flexit::panel::marks
