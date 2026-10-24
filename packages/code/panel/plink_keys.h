// Message keys between the CI600 and the ESP. A message is a 16-bit key and either a number or bytes, see
// plink_kind(). A number goes low byte first without its high zero bytes, so 0 takes none. Unknown keys are
// ignored. The keys, each range with room to grow:
//
//   0x001..0x00F  the link
//   0x010..0x03F  panel settings the ESP sees, plink_values.h
//   0x040..0x07F  the unit's values, plink_values.h
//   0x080..0x09F  equipment, K_FEATURE + app/equipment.h's id
//   0x0A0..0x0FF  panel settings only the panel has, app/values.h. Their keys only name them in its flash
//   0x100..0x11F  the screensaver picture
//   0x120..0x13F  screensaver animations
//   0x200..0x3FF  ESP items, 0x40 keys for each kind
#pragma once

// ---- The link
// version << 16 | HELLO_ASK. Each end sends one with HELLO_ASK when it starts and the other answers with one
// without. On any HELLO we send our full state. Everything else is ignored until we get a HELLO with our version,
// so a HELLO goes ahead of whatever hasn't gone out yet.
#define K_HELLO 0x001
#define HELLO_ASK 1
#define K_TIME 0x002       // ESP -> panel: local time, seconds since 2000-01-01
#define K_ACTION 0x003     // ESP -> panel: run action A_*. Panel -> ESP: the user ran it
#define K_SHOW_VIEW 0x004  // ESP -> panel: show home view VIEW_*
#define K_NOTIFY 0x005     // ESP -> panel: a notification with icon, level (NOTE_*) and text
enum { NOTE_INFO, NOTE_WARNING, NOTE_ALARM };
#define PLINK_TEXT_MAX 64  // max bytes of UTF-8 in a text

#define K_FEATURE 0x080  // + equipment id (app/equipment.h): 0/1

// ---- The screensaver picture, in one of two formats:
//   PIC_PALETTE  16 colours, each row PackBits-compressed on its own (plink_pack_row) so changed rows can be sent
//                alone with PATCH. For drawings like the weather picture. DATA carries them as pdeflate.h's pzip
//                pieces, deflated against what the panel has.
//   PIC_JPEG     baseline JPEG from pjpeg.h. Y at 2x1, a restart marker after every MCU row, width a multiple of
//                16, height of 8. For photos, always sent whole.
// A transfer is BEGIN or PATCH, then DATA up to len bytes, then END. The panel answers with PIC_ACKs and centers
// pictures smaller than the screen.
// BEGIN, a whole picture: id, format, w (2), h (2), len (4), then for PIC_PALETTE the palette (16 x RGB)
enum { PIC_PALETTE, PIC_JPEG };
#define K_PIC_BEGIN 0x100
// PATCH, rows y..y+count-1 of the current 16-colour picture: id, y (2), count (2), len (4), then a new palette or
// none. With no rows only the palette changes.
#define K_PIC_PATCH 0x101
#define K_PIC_DATA 0x102   // the picture's bytes, a multiple of 4 for pbulk. Bytes past the end are ignored
#define K_PIC_END 0x103    // plink_crc32 of the len bytes
#define K_PIC_ACK 0x104    // panel -> ESP: status (PIC_*) << 24 | id << 16 | bytes received
#define K_PIC_CLEAR 0x105  // forget the picture

#define PIC_POOL 14336  // panel memory for a picture, plus room behind it for a patch while it comes in

// PIC_ACK status
enum {
  PIC_PROGRESS,    // another 1 KiB received
  PIC_DONE,        // complete and CRC checked, now shown
  PIC_BAD_CRC,     // send it again
  PIC_TOO_BIG,     // doesn't fit PIC_POOL. For an animation it's ANIM_POOL, try it at half size
  PIC_NO_ROOM,     // patch doesn't fit behind the picture, send smaller pieces or the whole picture
  PIC_NO_PICTURE,  // patch but no 16-colour picture to patch, the panel restarted. Send the whole picture
  PIC_BAD_SIZE,    // width, height or rows out of range
  PIC_BAD,         // header of the wrong length, data that doesn't decode to the rows, or a JPEG the panel can't draw
};

// ---- Screensaver animations
// BEGIN, an animation: id, slot (0..ANIM_SLOTS - 1), w (2, even), h (2), scale (1 or 2), transparent colour index
// (ANIM_OPAQUE = none), len (4), palette (16 x RGB). Then DATA up to len bytes and END, as for a picture. The data is
// one plink_anim_record per frame. The first is the whole first frame, the last one takes the last frame back to the
// first. It replaces the slot's animation and its places. The panel keeps every slot's frame and records in
// ANIM_POOL, and answers PIC_TOO_BIG when they don't fit next to the others.
#define K_ANIM_BEGIN 0x120
#define K_ANIM_CLEAR 0x121  // a slot, ANIM_PLACE_BIT | a place, or ANIM_ALL, | ms << 8 to fade out over ms first
// PLACE, where an animation shows: place (0..ANIM_PLACES - 1), slot, x (2), y (2), own, start, fade (2). Replaces
// that place. Every place of a slot shows its frames in step, unless own = 1 gives it a frame of its own (w x h / 2
// bytes more of ANIM_POOL) that starts start frames in. It fades in over fade ms, upright, full size and on the
// whole screen. Ignored without the animation or the room.
#define K_ANIM_PLACE 0x122
// MOVE, moves a place: place, mode, then
//   ANIM_GLIDE: x, y (2 each, signed, the top left to glide to) and ms (2) to get there, 0 = jump
//   ANIM_DRIFT: vx, vy (2 each, signed) in pixels per second
//   ANIM_WANDER: legs of speed min..max px/s (2 + 2) toward heading +- spread degrees (2 + 2, signed, clockwise from
//     right) for leg min..max ms (2 + 2, 0 0 = one leg), rests of min..max ms (2 + 2, 0 0 = none) between. A bounce
//     turns the heading round.
// A drift or wander keeps to the place's AREA. ANIM_STOP alone leaves it where it is. Ignored without the place.
#define K_ANIM_MOVE 0x123
enum { ANIM_STOP, ANIM_GLIDE, ANIM_DRIFT, ANIM_WANDER };
// TURN, turns and flips a place round its middle: place, mode, flip (ANIM_FLIP_*), then
//   ANIM_GLIDE: angle (2, signed, degrees clockwise) to turn to the short way round and ms (2), 0 = at once
//   ANIM_DRIFT: degrees per second (2, signed) to spin
//   ANIM_FACE: toward where it goes: ANIM_FACE_* flags, the way the frames face (2, degrees), tilt (2, the most
//     it turns, 0..180) and rate (2, degrees per second, 0 = at once)
// ANIM_STOP stays at the angle it has.
#define K_ANIM_TURN 0x124
#define ANIM_FACE 3         // TURN's own mode
#define ANIM_FLIP_X 1       // mirrored left to right, before it turns
#define ANIM_FLIP_Y 2       // upside down
#define ANIM_FACE_MIRROR 1  // flips left to right to face the other way
#define ANIM_FACE_LEVEL 2   // turns back upright when it stops
// AREA, where a drift or wander keeps: place, x0, y0, x1, y1 (2 each, signed, x1 and y1 just past it), then
// ANIM_EDGE_* for x and for y. The turned and sized box keeps to it.
#define K_ANIM_AREA 0x125
enum {
  ANIM_EDGE_SCREEN,  // round the whole screen, what's over one edge shows at the other
  ANIM_EDGE_CROSS,   // out across an edge of the area, back in at the other one
  ANIM_EDGE_BOUNCE,  // back off the edges
};
#define K_ANIM_SIZE 0x126  // place, percent (2, 10..400) and ms (2) to get there, round its middle
#define K_ANIM_FADE 0x127  // place, opacity (0..255) and ms (2) to get there
// ROUNDS: place, rounds (2), fade (2), tag. Plays to the end of rounds rounds, then fades out over fade ms and goes,
// answering GONE. A frame of its own holds the last one. 0 plays on.
#define K_ANIM_ROUNDS 0x128
#define K_ANIM_GONE 0x129  // panel -> ESP: place | tag << 8 of one that went after its rounds
#define ANIM_OPAQUE 16
#define ANIM_SLOTS 4
#define ANIM_PLACES 8
#define ANIM_PLACE_BIT 0x80
#define ANIM_ALL 0xFF
#define ANIM_POOL 6144  // panel memory for the animations, each one inflated frame of w x h / 2 bytes plus records

// ---- ESP items: anything the ESP adds to the panel, numbered as it likes. Each is on the smart home or comfort
// page. Any item can be a dashboard tile, and sensors can keep a 24-hour trend.
// DEF: type, flags, icon, decimals, step (2), id (2), min (4), max (4), then four levels (2 each) for
// ITEMF_LEVELS. Bad below the first, good from the second to the third, bad above the fourth, fair in between.
// id is a 14-bit hash of the item's name, never 0. Dashboard tiles and trends remember the item by it.
#define K_ITEM_DEF 0x200    // + item: DEF bytes
#define K_ITEM_VALUE 0x240  // + item: the value. Goes both ways, the panel sends it when the user changes it
#define K_ITEM_LABEL 0x280  // + item: text in the panel's LANGUAGE
#define K_ITEM_UNIT 0x2C0   // + item: text
#define K_ITEM_ON 0x300     // + item: a toggle's word for on in the panel's LANGUAGE, empty for On
#define K_ITEM_OFF 0x340    // + item: and for off
#define K_ITEM_TEXT 0x380   // + item: an ITEM_TEXT's value, text in the panel's LANGUAGE
// Panel -> ESP: n, asking for item n's help text, shown with ?. ESP -> panel: n, then the text, at most
// PLINK_HELP_MAX bytes.
#define K_ITEM_HELP 0x3C0
#define N_ITEMS 24              // up to 0x40, a kind's keys
#define ITEM_DEF_LEN 24         // DEF length in bytes
#define ITEM_TEXT_LEN 16        // the panel's room for an ITEM_TEXT value, with the 0
#define ITEM_LABEL 20           // and for a label
#define ITEM_UNIT 8             // a unit
#define ITEM_STATE 12           // a toggle's word for on or off
#define ITEM_UNKNOWN INT32_MIN  // VALUE when there's none, like from a sensor that's unavailable
#define PLINK_HELP_MAX 78       // with the item byte within PBULK_MSG_MAX, so it goes in an ordinary window
// DEF type. ITEM_TEXT is read-only and shows its TEXT, the DEF's value fields are unused
enum { ITEM_NONE, ITEM_SENSOR, ITEM_TOGGLE, ITEM_NUMBER, ITEM_BUTTON, ITEM_TEXT };
// DEF flags
#define ITEMF_VIEW 1     // on its page's view. Smart home shows a tile, comfort a gauge (sensor) or chip (toggle)
#define ITEMF_MENU 2     // a row in its page's menu
#define ITEMF_RO 4       // shown but can't be changed on the panel, like a toggle for a binary sensor
#define ITEMF_COMFORT 8  // on the comfort page, else smart home
#define ITEMF_TREND 16   // panel keeps 24 hours of it for the sensors view, only the first N_TRENDS items
#define ITEMF_LEVELS 32  // coloured by its levels

// ---- Value meanings
// Actions for K_ACTION. The ESP's name for each is its id in lower case. The last two are panel-only.
#define PLINK_ACTIONS(X) \
  X(NONE) \
  X(RESET_FILTER) \
  X(RESET_ALARMS) \
  X(CLEAR_HISTORY) \
  X(RESTART) \
  X(FACTORY_RESET) \
  X(REMOVE_PERIOD) \
  X(RESET_PANEL)
#define A_ENUM(id) A_##id,
typedef enum { PLINK_ACTIONS(A_ENUM) A_COUNT } action_t;
#undef A_ENUM
#define A_REMOTE_LAST A_FACTORY_RESET  // the last action the ESP may run

// Home views for K_SHOW_VIEW and V_HOME_VIEW: X(id, the ESP's name for it)
#define PLINK_VIEWS(X) \
  X(DASH, "dashboard") \
  X(FLOW, "airflow") \
  X(TREND, "trends") \
  X(SENSORS, "sensors") \
  X(COMFORT, "comfort") \
  X(SMART, "smart_home")
#define VIEW_ENUM(id, name) VIEW_##id,
enum { PLINK_VIEWS(VIEW_ENUM) VIEW_COUNT };
#undef VIEW_ENUM
