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
//   0x120..0x13F  screensaver overlays
//   0x140..0x14F  screenshots
//   0x150..0x15F  the screensaver's lines
//   0x160..0x16F  debugging
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
// BEGIN, PATCH and OVERLAY_BEGIN start with the transfer's id, which its PIC_ACKs carry
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
// PART, a part of the picture in front of the places farther than it, like a flagpole the boats go behind: part
// (0..PIC_PARTS - 1), x0, y0, x1, y1 (2 each, signed, x1 and y1 just past it) on the screen like a place's x and y,
// colours (2, a bit for each of the picture's that make the part, PIC_ALL_COLOURS for all it covers, the only way
// for a JPEG), then depth, see K_OVERLAY_DEPTH. An empty one drops the part, BEGIN and CLEAR all of them.
#define K_PIC_PART 0x106
enum {
  PIC_PART_X0 = 1,
  PIC_PART_Y0 = 3,
  PIC_PART_X1 = 5,
  PIC_PART_Y1 = 7,
  PIC_PART_COLOURS = 9,
  PIC_PART_DEPTH = 11,
  PIC_PART_LEN
};
#define PIC_PARTS 10
#define PIC_ALL_COLOURS 0xFFFF

#define PIC_POOL 14336  // panel memory for a picture, plus room behind it for a patch while it comes in
#define PIC_MAX_W 320   // the screen, the biggest picture or overlay
#define PIC_MAX_H 240

// PIC_ACK status
enum {
  PIC_PROGRESS,    // another 1 KiB received
  PIC_DONE,        // complete and CRC checked, now shown
  PIC_BAD_CRC,     // send it again
  PIC_TOO_BIG,     // doesn't fit PIC_POOL. For an overlay it's OVERLAY_POOL, try it at half size
  PIC_NO_ROOM,     // patch doesn't fit behind the picture, send smaller pieces or the whole picture
  PIC_NO_PICTURE,  // patch but no 16-colour picture to patch, the panel restarted. Send the whole picture
  PIC_BAD_SIZE,    // width, height or rows out of range
  PIC_BAD,         // header of the wrong length, data that doesn't decode to the rows, or a JPEG the panel can't draw
};

// ---- Screensaver overlays, drawn over the picture: 16 colours, each with its own opacity, animated or still
// BEGIN: id, slot (0..OVERLAY_SLOTS - 1), w (2), h (2), scale (1 or 2), OVERLAY_ANIMATED or OVERLAY_STILL, size (4),
// then at OVERLAY_BEGIN_HEAD the palette: 16 x RGB, then their opacities from 0, clear, to 15, two a byte, the first
// colour's in the high nibble. Then at OVERLAY_BEGIN_GLOWS a bit for each colour that glows (2), which a place's
// light and tint leave as they are, and at OVERLAY_BEGIN_SHADE the RGB its light goes toward below 100. A frame's rows
// are (w + 1) / 2 bytes. Then DATA up to size bytes and END, as for a picture. It replaces the slot's overlay and its
// places. The panel keeps every slot's records, and an animated one's frame, in OVERLAY_POOL, and answers
// PIC_TOO_BIG when they don't fit next to the others.
#define K_OVERLAY_BEGIN 0x120
enum {
  OVERLAY_BEGIN_SLOT = 1,
  OVERLAY_BEGIN_W,
  OVERLAY_BEGIN_H = 4,
  OVERLAY_BEGIN_SCALE = 6,
  OVERLAY_BEGIN_KIND,
  OVERLAY_BEGIN_SIZE,
  OVERLAY_BEGIN_HEAD = 12,
};
#define OVERLAY_PAL_LEN (PIC_PAL_LEN + 8)  // the palette and its opacities
#define OVERLAY_BEGIN_GLOWS (OVERLAY_BEGIN_HEAD + OVERLAY_PAL_LEN)
#define OVERLAY_BEGIN_SHADE (OVERLAY_BEGIN_GLOWS + 2)
#define OVERLAY_BEGIN_LEN (OVERLAY_BEGIN_SHADE + 3)
// The data is records of OVERLAY_RECORD_HEAD bytes, delay, x / 2, y, w / 2 and h, then that rectangle of the frame's
// bytes:
//   OVERLAY_ANIMATED  a record a frame from plink_overlay_record, the rectangle that changed, deflated. The first is
//                     the whole first frame, the last takes the last frame back to the first. An empty one with x
//                     OVERLAY_RECORD_MORE only makes the frame before it longer, so it isn't a frame of its own. It
//                     can't be the last, and the second goes for the first frame each time round.
//   OVERLAY_STILL     its pieces from plink_overlay_pieces, each at most OVERLAY_PIECE_MAX bytes, its rows packed as
//                     plink_pack_row packs them. Clear where there's none. The panel keeps them packed and unpacks
//                     them as they're drawn. The first one's delay is how long a round is, see OVERLAY_DO_ROUNDS.
// The delay is in OVERLAY_TICK_MS, up to OVERLAY_DELAY_MAX. Whatever its delay, a frame shows OVERLAY_FRAME_MIN_MS at
// least.
enum { OVERLAY_ANIMATED, OVERLAY_STILL };
#define OVERLAY_PIECE_MAX 1280  // 8 rows of the screen
enum {
  OVERLAY_RECORD_DELAY,
  OVERLAY_RECORD_X,
  OVERLAY_RECORD_Y,
  OVERLAY_RECORD_W,
  OVERLAY_RECORD_H,
  OVERLAY_RECORD_HEAD
};
#define OVERLAY_RECORD_MORE 0xFF
#define OVERLAY_TICK_MS 10
#define OVERLAY_DELAY_MAX 255
#define OVERLAY_FRAME_MIN_MS 40
// CLEAR: a slot, OVERLAY_PLACE_BIT | a place, or OVERLAY_ALL, | ms << 8 to fade out over ms first
#define K_OVERLAY_CLEAR 0x121
// The rest up to DEPTH go to overlay_msg(). FRAMES is for a slot, the others for a place, ignored without it.
#define OVERLAY_MSG_PLACE 0
#define K_OVERLAY_FRAMES 0x122  // slot, frame rate (2, percent of the records' times, 0..OVERLAY_RATE_MAX, 0 = paused)
enum { OVERLAY_FRAMES_SLOT, OVERLAY_FRAMES_RATE, OVERLAY_FRAMES_LEN = 3 };
#define OVERLAY_RATE_MAX 1000
// PLACE, where an overlay shows: place (0..OVERLAY_PLACES - 1), slot, x (2), y (2), own, fade (2). Replaces that
// place, x and y signed, so it can start off the screen. Every place of an animated slot shows its frames in step,
// unless own = 1 gives it a frame of its own (as many bytes again of OVERLAY_POOL), from the first frame. A still
// ignores own. It fades in over fade ms, upright, full size, in its light and on the whole screen, showing all of its
// frame. Ignored without the overlay or the room.
#define K_OVERLAY_PLACE 0x123
enum {
  OVERLAY_PLACE_SLOT = 1,
  OVERLAY_PLACE_X,
  OVERLAY_PLACE_Y = 4,
  OVERLAY_PLACE_OWN = 6,
  OVERLAY_PLACE_FADE,
  OVERLAY_PLACE_LEN = 9,
};
// AREA, where a drift or wander keeps and what a MAP goes across: place, x0, y0, x1, y1 (2 each, signed, x1 and y1
// just past it), then OVERLAY_EDGE_* for x and for y. The turned and sized box keeps to it.
#define K_OVERLAY_AREA 0x124
enum {
  OVERLAY_AREA_X0 = 1,
  OVERLAY_AREA_Y0 = 3,
  OVERLAY_AREA_X1 = 5,
  OVERLAY_AREA_Y1 = 7,
  OVERLAY_AREA_EDGES = 9,
  OVERLAY_AREA_LEN = 11,
};
enum {
  OVERLAY_EDGE_SCREEN,  // round the whole screen, what's over one edge shows at the other
  OVERLAY_EDGE_CROSS,   // out across an edge of the area, back in at the other one
  OVERLAY_EDGE_BOUNCE,  // back off the edges
};
// A place's channels: x, y the top left of its frame at full size in px, turn degrees clockwise round its middle,
// size, opacity, speed and light in percent. Speed is how fast its drifts and wander on x and y go. Below 100 its light
// takes its colours toward its slot's shade, above toward white, all the way at 0 and OVERLAY_LIGHT_MAX.
enum {
  OVERLAY_X,
  OVERLAY_Y,
  OVERLAY_TURN,
  OVERLAY_SIZE,
  OVERLAY_OPACITY,
  OVERLAY_SPEED,
  OVERLAY_LIGHT,
  OVERLAY_CHANNELS
};
// GLIDE: place, channels (a bit per OVERLAY_*), ms (2, 0 = at once), OVERLAY_EASE_*, then at OVERLAY_GLIDE_HEAD a value
// (2, signed) for each channel in order: size OVERLAY_SIZE_MIN..OVERLAY_SIZE_MAX, opacity, speed and light from 0 to
// their OVERLAY_*_MAX. A turn goes the short way round. It stops each channel's drift, the wander on x or y, the face
// on turn.
#define K_OVERLAY_GLIDE 0x125
enum { OVERLAY_GLIDE_CHANNELS = 1, OVERLAY_GLIDE_MS, OVERLAY_GLIDE_EASE = 4, OVERLAY_GLIDE_HEAD };
#define OVERLAY_EASE_IN 1   // starts slow
#define OVERLAY_EASE_OUT 2  // ends slow
#define OVERLAY_SIZE_MIN 10
#define OVERLAY_SIZE_MAX 400
#define OVERLAY_OPACITY_MAX 100
#define OVERLAY_SPEED_MAX 1000
#define OVERLAY_LIGHT_MAX 200
// DRIFT: place, channels (bits of x, y and turn), then at OVERLAY_DRIFT_HEAD for each channel in order hundredths of
// a px per second on x and y, tenths of a degree on turn (2, signed), 0 = stays where it is. On x and y it keeps to
// the AREA. It stops each channel's glide, the wander on x or y, the face on turn.
#define K_OVERLAY_DRIFT 0x126
enum { OVERLAY_DRIFT_CHANNELS = 1, OVERLAY_DRIFT_HEAD };
// WANDER: place, legs of speed min..max tenths of a px/s (2 + 2) toward heading +- spread degrees (2 + 2, signed,
// clockwise from right) for leg min..max ms (2 + 2, 0 0 = one leg), rests of min..max ms (2 + 2, 0 0 = none)
// between. It keeps to the AREA, and a bounce turns the heading round. It stops the glides and drifts of x and y.
#define K_OVERLAY_WANDER 0x127
enum {
  OVERLAY_WANDER_SPEED = 1,  // min, then max
  OVERLAY_WANDER_HEADING = 5,
  OVERLAY_WANDER_SPREAD = 7,
  OVERLAY_WANDER_LEG = 9,    // min, then max
  OVERLAY_WANDER_REST = 13,  // min, then max
  OVERLAY_WANDER_LEN = 17,
};
// FACE: place, OVERLAY_FACE_*, the way the frames face (2, degrees), tilt (2, the most it turns, 0..180) and rate (2,
// whole degrees per second, 0 = at once). Turns toward where it goes until a glide or drift of its turn.
#define K_OVERLAY_FACE 0x128
enum {
  OVERLAY_FACE_FLAGS = 1,
  OVERLAY_FACE_FACING,
  OVERLAY_FACE_TILT = 4,
  OVERLAY_FACE_RATE = 6,
  OVERLAY_FACE_LEN = 8
};
#define OVERLAY_FACE_MIRROR 1  // flips left to right to face the other way
#define OVERLAY_FACE_LEVEL 2   // turns back upright when it stops
#define K_OVERLAY_FLIP 0x129   // place, OVERLAY_FLIP_*
#define OVERLAY_FLIP_X 1       // mirrored left to right, before it turns
#define OVERLAY_FLIP_Y 2       // upside down
enum { OVERLAY_FLIP_FLAGS = 1, OVERLAY_FLIP_LEN };
// WAVE: place, channel, low, high (2 each, signed), rise, fall (2 each, ms for the whole way up and down), hold at
// high, hold at low (2 each, ms), jitter (percent the times vary), OVERLAY_WAVE_*. Goes between low and high on its
// own, an offset on x, y and turn, percent of the channel on the others. Low = high goes there and stays, and at no
// change the wave is gone.
#define K_OVERLAY_WAVE 0x12A
enum {
  OVERLAY_WAVE_CHANNEL = 1,
  OVERLAY_WAVE_LO,
  OVERLAY_WAVE_HI = 4,
  OVERLAY_WAVE_MS = 6,     // rise, then fall
  OVERLAY_WAVE_HOLD = 10,  // at high, then at low
  OVERLAY_WAVE_JITTER = 14,
  OVERLAY_WAVE_FLAGS,
  OVERLAY_WAVE_LEN,
};
#define OVERLAY_WAVE_SLOW_LOW 1   // eases in and out at low
#define OVERLAY_WAVE_SLOW_HIGH 2  // and at high
#define OVERLAY_WAVE_RANDOM 4     // each way to anywhere between low and high
// MAP: place, channel (turn, size, opacity or speed), its value at the AREA's left, right, top and bottom (2 each,
// signed). The channel follows where its middle is across the area, an offset on turn and percent on the others,
// the two ways added or multiplied. The places it makes bigger are nearer, and drawn in front of the others at their
// depth.
#define K_OVERLAY_MAP 0x12B
enum { OVERLAY_MAP_CHANNEL = 1, OVERLAY_MAP_VALUES, OVERLAY_MAP_LEN = 10 };  // VALUES: left, right, top, bottom
// SCRIPT: place, at (2), len (2), then the script's bytes from at on, in as many messages as it takes, each going on
// at the byte the one before ended. len is the whole script's, OVERLAY_SCRIPT_MAX at most. It takes OVERLAY_SCRIPT_POOL
// + len bytes of OVERLAY_POOL from its first message until the place goes, and runs once the last is in: at once, then
// on as its waits end. Another one, one of len 0, a PLACE or a CLEAR of the place ends it. It's dropped whole if it
// doesn't fit, has a bad step or plays frames on a place without a frame of its own.
#define K_OVERLAY_SCRIPT 0x12C
enum { OVERLAY_SCRIPT_AT = 1, OVERLAY_SCRIPT_LEN = 3, OVERLAY_SCRIPT_BYTES = 5 };
#define OVERLAY_SCRIPT_MAX 480
#define OVERLAY_SCRIPT_POOL 64   // what a script takes of OVERLAY_POOL besides its bytes
#define OVERLAY_SCRIPT_STEPS 32  // steps it takes in a row, then it goes on at the next motion step
// A script is steps, each an OVERLAY_DO_* and then what it takes, in this order:
//   r   a register: 0..7 the place's own, 8..11 shared by all of them. 32 bits, signed, 0 to start with, the
//       shared ones again after a CLEAR of everything.
//   v   a value: OVERLAY_V16 and 2 bytes or OVERLAY_V32 and 4, signed, low byte first, or OVERLAY_VREG + a register
//   p   a place, or OVERLAY_SELF for its own
//   at  the byte of the script the step to go on with starts at, 2 bytes, low byte first
//   l   a list: a byte n, 1 or more, then n numbers of 2 bytes, signed, low byte first
//   z   a size in px, 2 bytes, low byte first, 0 for all of the frame's width or height
// Maths wraps round at 32 bits, v / 0 and v % 0 are 0. Frames are numbered as the records go, but for those that only
// make one longer, and round its frames: 5 of 4 is 1.
enum {
  OVERLAY_DO_SET,  // r v: r is v
  OVERLAY_DO_ADD,  // r v: r + v, and so on
  OVERLAY_DO_SUB,
  OVERLAY_DO_MUL,
  OVERLAY_DO_DIV,
  OVERLAY_DO_MOD,
  OVERLAY_DO_MIN,
  OVERLAY_DO_MAX,
  OVERLAY_DO_ABS,     // r v: r is v without its sign
  OVERLAY_DO_RAND,    // r v v: r is the first to the second, both in
  OVERLAY_DO_GET,     // r OVERLAY_GET_* p: what p is now, 0 if it has no overlay
  OVERLAY_DO_DIST,    // r p p: px from the first's middle to the second's
  OVERLAY_DO_ANGLE,   // r p p: the way from the first's middle to the second's, degrees, 0 right, 90 down
  OVERLAY_DO_SIN,     // r v v: the second times sin of the first, in degrees
  OVERLAY_DO_COS,     // r v v
  OVERLAY_DO_PICK,    // r v l: r is the list's number v, from 0, its first below that and its last past it
  OVERLAY_DO_GOTO,    // at
  OVERLAY_DO_IF,      // v OVERLAY_IF_* v at: on at at if it holds
  OVERLAY_DO_WAIT,    // v: ms
  OVERLAY_DO_UNTIL,   // v OVERLAY_IF_* v at: on if it holds, else on at at at the next motion step
  OVERLAY_DO_ROUNDS,  // v: until that many more rounds of its clip have ended
  OVERLAY_DO_CLIP,    // v v OVERLAY_CLIP_* z z: its frames from the first to the second round and round, w x h of
                      // each from its top left: its box, and what it's mirrored and turned in
  OVERLAY_DO_FRAME,   // v: to that frame at once, its clip going on from there
  OVERLAY_DO_RATE,    // v: percent its frames go at, 0..OVERLAY_RATE_MAX, 0 holds the frame it's on
  OVERLAY_DO_MOVE,    // v v v ease: a GLIDE of x and y to the first two over the third's ms, ease an OVERLAY_EASE_*
  OVERLAY_DO_SEND,    // key - K_OVERLAY_FRAMES, n, n bytes, k, then k times a byte and v: that message to itself, AREA
                      // to TINT but SCRIPT, with the low 16 bits of each v at its byte
  OVERLAY_DO_MARK,    // tag: tells the ESP, see MARK
  OVERLAY_DO_END,     // v tag: fades out over v ms and goes, answering GONE. A frame of its own stays as it is.
  OVERLAY_DOS
};
#define OVERLAY_V16 0
#define OVERLAY_V32 1
#define OVERLAY_VREG 2
#define OVERLAY_REGS 12
#define OVERLAY_OWN_REGS 8
#define OVERLAY_SELF 0xFF
#define OVERLAY_SEND_MAX 24  // bytes in a SEND's message
enum { OVERLAY_IF_LT, OVERLAY_IF_LE, OVERLAY_IF_EQ, OVERLAY_IF_NE, OVERLAY_IF_GE, OVERLAY_IF_GT };
// x, y its top left, w, h what its clip shows of its frame at full size, px. turn in degrees 0..359, size, opacity and
// light percent as it's shown, the frame it shows, rounds of its clip so far and ms since its script started, but for
// those 0 without a script, and here 1. A place shown in step with its slot counts the slot's rounds, one that
// doesn't play frames a round each time its first record's delay goes by.
enum {
  OVERLAY_GET_X,
  OVERLAY_GET_Y,
  OVERLAY_GET_W,
  OVERLAY_GET_H,
  OVERLAY_GET_TURN,
  OVERLAY_GET_SIZE,
  OVERLAY_GET_OPACITY,
  OVERLAY_GET_LIGHT,
  OVERLAY_GET_FRAME,
  OVERLAY_GET_ROUNDS,
  OVERLAY_GET_HERE,
  OVERLAY_GET_MS,
  OVERLAY_GETS
};
#define OVERLAY_CLIP_NOW 0    // from the first at once
#define OVERLAY_CLIP_NEXT 1   // once a round of the clip it plays ends, the script waiting. At once if one just did.
#define K_OVERLAY_GONE 0x12D  // panel -> ESP: place | tag << 8 of one that went after its script's END
// DEPTH: place, depth, 0 the farthest. PLACE starts it at OVERLAY_NEAREST. A place is drawn in front of those at less
// depth, and behind each part of the picture at more, see K_PIC_PART.
#define K_OVERLAY_DEPTH 0x12E
enum { OVERLAY_DEPTH_DEPTH = 1, OVERLAY_DEPTH_LEN };
#define K_OVERLAY_MARK 0x12F  // panel -> ESP: place | tag << 8 of a script at its MARK
// TINT: place, 0 for its own colours or 1 and an RGB: its colours as shades of that one, from black through it to
// white as they're dark or light, but for those that glow. PLACE starts it with its own.
#define K_OVERLAY_TINT 0x130
enum { OVERLAY_TINT_ON = 1, OVERLAY_TINT_RGB, OVERLAY_TINT_LEN = 5 };
#define OVERLAY_NEAREST 255
#define OVERLAY_SLOTS 6
#define OVERLAY_PLACES 8
#define OVERLAY_WAVES 12  // waves the panel runs at a time, of all places together
#define OVERLAY_PLACE_BIT 0x80
#define OVERLAY_ALL 0xFF
#define OVERLAY_POOL 8192  // panel memory for the overlays: records, an animated one's frame, scripts

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

// ---- Debugging
#define K_LOG 0x160  // panel -> ESP: a line of text for the ESP's log, from a debug build like make -C hw PROF=1

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
