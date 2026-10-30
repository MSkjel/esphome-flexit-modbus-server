// Message keys between the CI600 and the ESP. A message is a 16-bit key and either a number or bytes, see
// plink_kind(). A number goes low byte first without its high zero bytes, so 0 takes none. Unknown keys are
// ignored. The keys, each range with room to grow:
//
//   0x001..0x00F  the link
//   0x010..0x03F  panel settings the ESP sees, plink_values.h
//   0x040..0x07F  the unit's settings only the lowest address may write, plink_values.h
//   0x080..0x09F  equipment, K_FEATURE + app/equipment.h's id
//   0x0A0..0x0FF  panel settings only the panel has, app/values.h. Their keys only name them in its flash
//   0x100..0x11F  the screensaver picture
//   0x120..0x13F  screensaver animations
//   0x140..0x14F  screenshots
//   0x150..0x15F  the screensaver's lines
//   0x200..0x3FF  ESP items, 0x40 keys for each kind
//
// Copied into esphome-flexit-modbus-server by make -C sim esp_sync, edit it here.
#pragma once

// ---- The link
// version << 16 | HELLO_ASK. Each end sends one with HELLO_ASK when it starts and the other answers with one
// without. On any HELLO we send our full state. Everything else is ignored until we get a HELLO with our version,
// so a HELLO goes ahead of whatever hasn't gone out yet.
#define K_HELLO 0x001
#define HELLO_ASK 1
#define HELLO_FRESH 2      // panel -> ESP: it started without saved settings, like after a flash
#define K_TIME 0x002       // ESP -> panel: local time, seconds since 2000-01-01
#define K_ACTION 0x003     // ESP -> panel: run action A_*. Panel -> ESP: the user ran it
#define K_SHOW_VIEW 0x004  // ESP -> panel: show home view VIEW_*
#define K_NOTIFY 0x005     // ESP -> panel: a notification with icon, level (NOTE_*) and text
// K_TIME's 2000-01-01 in Unix seconds. The panel's clock counts as not set before it.
#define PLINK_EPOCH 946684800u
// ESP -> panel: reg << 16 | value, a CX60 register the ESP has seen since it started. It sends them all after the
// panel's HELLO_ASK, and the panel takes them as if the CX60 sent them, so it has what changed while it was away.
// Not the status broadcast's (pbus.h), which come every cycle, nor the clock, which K_TIME covers.
#define K_CX60_REG 0x006
#define R_CLOCK_HI 0x094  // the CX60's clock, local time in Unix seconds
#define R_CLOCK_LO 0x095
// The panel's own settings around a flash, which erases them. The ESP sends 0 to ask, and the panel answers with
// SETTINGS_FORMAT, then each saved value as key << 16 | value, then SETTINGS_END << 16 | how many. The ESP sends
// them back the same way when the panel says hello with HELLO_FRESH after it flashed it, and the panel restarts
// with them. Unknown keys are left out, and a panel with another format takes none.
#define K_SETTINGS 0x007
#define SETTINGS_END 0xFFFF
enum { NOTE_INFO, NOTE_WARNING, NOTE_ALARM };
#define PLINK_TEXT_MAX 64  // max bytes of UTF-8 in a text

#define K_FEATURE 0x080  // + equipment id (app/equipment.h): 0/1

// ---- The screensaver picture, in one of two formats:
//   PIC_PALETTE  16 colours, each row PackBits-compressed on its own (plink_pack_row) so changed rows can be sent
//                alone with PATCH. For drawings like the weather picture. DATA carries them as pdeflate.h's pzip
//                pieces, deflated against what the panel has.
//   PIC_JPEG     baseline JPEG from pjpeg.h. Y at 2x1, a restart marker after every MCU row, width a multiple of
//                16, height of 8. For photos, always sent whole.
// A transfer is BEGIN or PATCH, then DATA up to size bytes, then END. The panel answers with PIC_ACKs and centers
// pictures smaller than the screen. Each message's field offsets follow it.
// BEGIN, PATCH and ANIM_BEGIN start with the transfer's id, which its PIC_ACKs carry
#define PIC_MSG_ID 0
// BEGIN, a whole picture: id, format, w (2), h (2), size (4), then at PIC_BEGIN_HEAD for PIC_PALETTE the palette
// (16 x RGB, plink_put_palette)
enum { PIC_PALETTE, PIC_JPEG };
#define K_PIC_BEGIN 0x100
enum { PIC_BEGIN_FORMAT = 1, PIC_BEGIN_W, PIC_BEGIN_H = 4, PIC_BEGIN_SIZE = 6, PIC_BEGIN_HEAD = 10 };
#define PIC_PAL_LEN 48  // a palette
// PATCH, rows y..y+count-1 of the current 16-colour picture: id, y (2), count (2), size (4), then a new palette or
// none. With no rows only the palette changes.
#define K_PIC_PATCH 0x101
enum { PIC_PATCH_Y = 1, PIC_PATCH_COUNT = 3, PIC_PATCH_SIZE = 5, PIC_PATCH_HEAD = 9 };
// The picture's bytes, a multiple of 4 (PBULK_DATA_KEY). Bytes past the end are ignored. From the panel: a
// screenshot's tiles, see K_SHOT
#define K_PIC_DATA 0x102
#define K_PIC_END 0x103    // plink_crc32 of the size bytes of DATA
#define K_PIC_ACK 0x104    // panel -> ESP: status (PIC_*) << 24 | id << 16 | bytes received
#define K_PIC_CLEAR 0x105  // forget the picture

#define PIC_POOL 14336  // panel memory for a picture, plus room behind it for a patch while it comes in
#define PIC_MAX_W 320   // the screen, the biggest picture or animation
#define PIC_MAX_H 240

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
// (ANIM_OPAQUE = none), size (4), then at ANIM_BEGIN_HEAD the palette (16 x RGB). Then DATA up to size bytes and END,
// as for a picture. The data is one plink_anim_record per frame. The first is the whole first frame, the last one
// takes the last frame back to the first. It replaces the slot's animation and its places. The panel keeps every
// slot's frame and records in ANIM_POOL, and answers PIC_TOO_BIG when they don't fit next to the others.
#define K_ANIM_BEGIN 0x120
enum {
  ANIM_BEGIN_SLOT = 1,
  ANIM_BEGIN_W,
  ANIM_BEGIN_H = 4,
  ANIM_BEGIN_SCALE = 6,
  ANIM_BEGIN_TRANSP,
  ANIM_BEGIN_SIZE,
  ANIM_BEGIN_HEAD = 12,
};
// A record is ANIM_RECORD_HEAD bytes, delay, x / 2, y, w / 2 and h, then its deflated rectangle. The delay is in
// ANIM_TICK_MS, up to ANIM_DELAY_MAX. Whatever its delay, a frame shows ANIM_FRAME_MIN_MS at least.
enum { ANIM_RECORD_DELAY, ANIM_RECORD_X, ANIM_RECORD_Y, ANIM_RECORD_W, ANIM_RECORD_H, ANIM_RECORD_HEAD };
#define ANIM_TICK_MS 10
#define ANIM_DELAY_MAX 255
#define ANIM_FRAME_MIN_MS 40
#define K_ANIM_CLEAR 0x121  // a slot, ANIM_PLACE_BIT | a place, or ANIM_ALL, | ms << 8 to fade out over ms first
// The rest up to ROUNDS go to anim_msg(). FRAMES is for a slot, the others for a place, ignored without it.
#define ANIM_MSG_PLACE 0
#define K_ANIM_FRAMES 0x122  // slot, frame rate (2, percent of the records' times, 0..ANIM_RATE_MAX, 0 = paused)
enum { ANIM_FRAMES_SLOT, ANIM_FRAMES_RATE, ANIM_FRAMES_LEN = 3 };
#define ANIM_RATE_MAX 1000
// PLACE, where an animation shows: place (0..ANIM_PLACES - 1), slot, x (2), y (2), own, start, fade (2). Replaces
// that place, x and y signed, so it can start off the screen. Every place of a slot shows its frames in step,
// unless own = 1 gives it a frame of its own (w x h / 2 bytes more of ANIM_POOL) that starts start frames in. It
// fades in over fade ms, upright, full size and on the whole screen. Ignored without the animation or the room.
#define K_ANIM_PLACE 0x123
enum {
  ANIM_PLACE_SLOT = 1,
  ANIM_PLACE_X,
  ANIM_PLACE_Y = 4,
  ANIM_PLACE_OWN = 6,
  ANIM_PLACE_START,
  ANIM_PLACE_FADE,
  ANIM_PLACE_LEN = 10,
};
// AREA, where a drift or wander keeps and what a MAP goes across: place, x0, y0, x1, y1 (2 each, signed, x1 and y1
// just past it), then ANIM_EDGE_* for x and for y. The turned and sized box keeps to it.
#define K_ANIM_AREA 0x124
enum {
  ANIM_AREA_X0 = 1,
  ANIM_AREA_Y0 = 3,
  ANIM_AREA_X1 = 5,
  ANIM_AREA_Y1 = 7,
  ANIM_AREA_EDGES = 9,
  ANIM_AREA_LEN = 11,
};
enum {
  ANIM_EDGE_SCREEN,  // round the whole screen, what's over one edge shows at the other
  ANIM_EDGE_CROSS,   // out across an edge of the area, back in at the other one
  ANIM_EDGE_BOUNCE,  // back off the edges
};
// A place's channels: x, y the top left of its frame at full size in px, turn degrees clockwise round its middle,
// size, opacity and speed in percent. Speed is how fast its drifts and wander on x and y go.
enum { ANIM_X, ANIM_Y, ANIM_TURN, ANIM_SIZE, ANIM_OPACITY, ANIM_SPEED, ANIM_CHANNELS };
// GLIDE: place, channels (a bit per ANIM_*), ms (2, 0 = at once), ANIM_EASE_*, then at ANIM_GLIDE_HEAD a value (2,
// signed) for each channel in order: size ANIM_SIZE_MIN..ANIM_SIZE_MAX, opacity and speed from 0 to their
// ANIM_*_MAX. A turn goes the short way round. It stops each channel's drift, the wander on x or y, the face on turn.
#define K_ANIM_GLIDE 0x125
enum { ANIM_GLIDE_CHANNELS = 1, ANIM_GLIDE_MS, ANIM_GLIDE_EASE = 4, ANIM_GLIDE_HEAD };
#define ANIM_EASE_IN 1   // starts slow
#define ANIM_EASE_OUT 2  // ends slow
#define ANIM_SIZE_MIN 10
#define ANIM_SIZE_MAX 400
#define ANIM_OPACITY_MAX 100
#define ANIM_SPEED_MAX 1000
// DRIFT: place, channels (bits of x, y and turn), then at ANIM_DRIFT_HEAD tenths of a px or degree per second (2,
// signed) for each channel in order, 0 = stays where it is. On x and y it keeps to the AREA. It stops each channel's
// glide, the wander on x or y, the face on turn.
#define K_ANIM_DRIFT 0x126
enum { ANIM_DRIFT_CHANNELS = 1, ANIM_DRIFT_HEAD };
// WANDER: place, legs of speed min..max tenths of a px/s (2 + 2) toward heading +- spread degrees (2 + 2, signed,
// clockwise from right) for leg min..max ms (2 + 2, 0 0 = one leg), rests of min..max ms (2 + 2, 0 0 = none)
// between. It keeps to the AREA, and a bounce turns the heading round. It stops the glides and drifts of x and y.
#define K_ANIM_WANDER 0x127
enum {
  ANIM_WANDER_SPEED = 1,  // min, then max
  ANIM_WANDER_HEADING = 5,
  ANIM_WANDER_SPREAD = 7,
  ANIM_WANDER_LEG = 9,    // min, then max
  ANIM_WANDER_REST = 13,  // min, then max
  ANIM_WANDER_LEN = 17,
};
// FACE: place, ANIM_FACE_*, the way the frames face (2, degrees), tilt (2, the most it turns, 0..180) and rate (2,
// whole degrees per second, 0 = at once). Turns toward where it goes until a glide or drift of its turn.
#define K_ANIM_FACE 0x128
enum { ANIM_FACE_FLAGS = 1, ANIM_FACE_FACING, ANIM_FACE_TILT = 4, ANIM_FACE_RATE = 6, ANIM_FACE_LEN = 8 };
#define ANIM_FACE_MIRROR 1  // flips left to right to face the other way
#define ANIM_FACE_LEVEL 2   // turns back upright when it stops
#define K_ANIM_FLIP 0x129   // place, ANIM_FLIP_*
#define ANIM_FLIP_X 1       // mirrored left to right, before it turns
#define ANIM_FLIP_Y 2       // upside down
enum { ANIM_FLIP_FLAGS = 1, ANIM_FLIP_LEN };
// WAVE: place, channel, low, high (2 each, signed), rise, fall (2 each, ms for the whole way up and down), hold at
// high, hold at low (2 each, ms), jitter (percent the times vary), ANIM_WAVE_*. Goes between low and high on its
// own, an offset on x, y and turn, percent of the channel on the others. Low = high goes there and stays, and at no
// change the wave is gone.
#define K_ANIM_WAVE 0x12A
enum {
  ANIM_WAVE_CHANNEL = 1,
  ANIM_WAVE_LO,
  ANIM_WAVE_HI = 4,
  ANIM_WAVE_MS = 6,     // rise, then fall
  ANIM_WAVE_HOLD = 10,  // at high, then at low
  ANIM_WAVE_JITTER = 14,
  ANIM_WAVE_FLAGS,
  ANIM_WAVE_LEN,
};
#define ANIM_WAVE_SLOW_LOW 1   // eases in and out at low
#define ANIM_WAVE_SLOW_HIGH 2  // and at high
#define ANIM_WAVE_RANDOM 4     // each way to anywhere between low and high
// MAP: place, channel (turn, size, opacity or speed), its value at the AREA's left, right, top and bottom (2 each,
// signed). The channel follows where its middle is across the area, an offset on turn and percent on the others,
// the two ways added or multiplied. The places it makes bigger are nearer, and drawn in front.
#define K_ANIM_MAP 0x12B
enum { ANIM_MAP_CHANNEL = 1, ANIM_MAP_VALUES, ANIM_MAP_LEN = 10 };  // VALUES: left, right, top, bottom
// ROUNDS: place, rounds (2), fade (2), tag. Plays to the end of rounds rounds, then fades out over fade ms and goes,
// answering GONE. A frame of its own holds the last one. 0 plays on.
#define K_ANIM_ROUNDS 0x12C
enum { ANIM_ROUNDS_COUNT = 1, ANIM_ROUNDS_FADE = 3, ANIM_ROUNDS_TAG = 5, ANIM_ROUNDS_LEN };
#define K_ANIM_GONE 0x12D  // panel -> ESP: place | tag << 8 of one that went after its rounds
#define ANIM_OPAQUE 16
#define ANIM_SLOTS 4
#define ANIM_PLACES 8
#define ANIM_WAVES 12  // waves the panel runs at a time, of all places together
#define ANIM_PLACE_BIT 0x80
#define ANIM_ALL 0xFF
#define ANIM_POOL 6144  // panel memory for the animations, each one inflated frame of w x h / 2 bytes plus records

// ---- Screenshots: what the panel shows as a JPEG, which the ESP puts together from 64x8 tiles, numbered across and
// then down. Each tile is one restart interval from pjpeg_tile() at the quality SHOT asked for. The ESP writes the
// headers with pjpeg_head(), then the tiles with RST0..RST7 in turn between them, then EOI.
// SHOT: quality 1..100, | SHOT_ALL for every tile, else only those that changed since they were last sent. 0 stops.
// The panel sends the tiles as PIC_DATA, one after the other, each as its number, its length (2) and its bytes. Then
// SHOT_DONE, after zeros that make the DATA a multiple of 4. Tiles are drawn again one at a time to be sent.
#define K_SHOT 0x140
#define SHOT_ALL 0x100
enum { SHOT_TILE_NUM, SHOT_TILE_LEN, SHOT_TILE_HEAD = 3 };
#define K_SHOT_DONE 0x141  // panel -> ESP: how many tiles it sent since SHOT
#define SHOT_TILE_W 64     // 4 MCUs
#define SHOT_TILE_H 8
#define SHOT_COLS (PIC_MAX_W / SHOT_TILE_W)
#define SHOT_ROWS (PIC_MAX_H / SHOT_TILE_H)
#define SHOT_TILES (SHOT_COLS * SHOT_ROWS)

// ---- The screensaver's lines, which it shows in place of the temperatures. LINE + i, i < SAVER_LINES: the line's
// size in px, 16, 20 or 28, then its text, empty for a gap. None show after the last with text, nor any that don't
// fit under those before them. The panel forgets them at a HELLO and shows the temperatures while the ESP is away.
#define K_SAVER_LINE 0x150
#define SAVER_LINES 4

// ---- ESP items: anything the ESP adds to the panel, numbered as it likes. Each is on the smart home or comfort
// page. Any item can be a dashboard tile, and sensors can keep a 24-hour trend.
// DEF: type, flags, icon, decimals, step (2), id (2), min (4), max (4), then four levels (2 each) for
// ITEMF_LEVELS. Bad below the first, good from the second to the third, bad above the fourth, fair in between.
// id is a 14-bit hash of the item's name, never 0. Dashboard tiles and trends remember the item by it.
enum {
  ITEM_DEF_TYPE,
  ITEM_DEF_FLAGS,
  ITEM_DEF_ICON,
  ITEM_DEF_DEC,
  ITEM_DEF_STEP,
  ITEM_DEF_ID = 6,
  ITEM_DEF_MIN = 8,
  ITEM_DEF_MAX = 12,
  ITEM_DEF_LEVELS = 16,
  ITEM_DEF_LEN = 24,
};
#define ITEM_ID_MASK 0x3FFF
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
// An item's key is its kind, K_ITEM_DEF .. K_ITEM_HELP, plus the item
#define K_ITEM_KIND(key) ((key) & ~0x3F)
#define K_ITEM_INDEX(key) ((key) & 0x3F)
#define N_ITEMS 24              // up to 0x40, a kind's keys
#define ITEM_TEXT_LEN 16        // the panel's room for an ITEM_TEXT value, with the 0
#define ITEM_LABEL_LEN 20       // and for a label
#define ITEM_UNIT_LEN 8         // a unit
#define ITEM_STATE_LEN 12       // a toggle's word for on or off
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
