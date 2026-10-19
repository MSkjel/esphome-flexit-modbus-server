// Message keys between the CI600 and the ESP. A message is a 16-bit key and either a number (4 bytes, low first)
// or bytes, see plink_kind(). Unknown keys are ignored. Panel settings and unit values are in plink_values.h.
#pragma once

// ---- The link itself
// version << 16 | HELLO_ASK. Each end sends one with HELLO_ASK when it starts and the other answers with one
// without. On any HELLO we send our full state. Everything else is ignored until we get a HELLO with our version.
#define K_HELLO 0x001
#define HELLO_ASK 1
// ESP -> panel: local time, seconds since 2000-01-01
#define K_TIME 0x003
// ESP -> panel: run action A_*. Panel -> ESP: the user ran it
#define K_ACTION 0x004
// ESP -> panel: show home view VIEW_*
#define K_SHOW_VIEW 0x005
// ESP -> panel: a notification with icon, level (NOTE_*) and text
#define K_NOTIFY 0x006
// + equipment id (app/equipment.h): 0/1
#define K_FEATURE 0x030

#define PLINK_TEXT_MAX 64  // max bytes of UTF-8 in a text

// ---- ESP items: anything the ESP adds to the panel, numbered as it likes. Each is on the smart home or comfort
// page. Any item can be a dashboard tile, and sensors can keep a 24-hour trend.
// DEF: type, flags, icon, decimals, step (2), id (2), min (4), max (4), then four levels (2 each) for
// ITEMF_LEVELS. Bad below the first, good from the second to the third, bad above the fourth, fair in between.
// id is a 14-bit hash of the item's name, never 0. Dashboard tiles and trends remember the item by it.
#define K_ITEM_DEF 0x400    // + item: DEF bytes
#define K_ITEM_VALUE 0x440  // + item: the value. Goes both ways, the panel sends it when the user changes it
#define K_ITEM_LABEL 0x480  // + item: text in the panel's LANGUAGE
#define K_ITEM_UNIT 0x4C0   // + item: text
#define K_ITEM_ON 0x540     // + item: a toggle's word for on in the panel's LANGUAGE, empty for On
#define K_ITEM_OFF 0x580    // + item: and for off
// Panel -> ESP: n, asking for item n's help text, shown with ?. ESP -> panel: n, then the text, at most
// PLINK_HELP_MAX bytes.
#define K_ITEM_HELP 0x500
#define N_ITEMS 24
#define ITEM_DEF_LEN 24         // DEF length in bytes
#define ITEM_UNKNOWN INT32_MIN  // VALUE when there's none, like from a sensor that's unavailable
#define PLINK_HELP_MAX 78       // with the item byte within PBULK_MSG_MAX, so it goes in an ordinary window
enum { ITEM_NONE, ITEM_SENSOR, ITEM_TOGGLE, ITEM_NUMBER, ITEM_BUTTON };  // DEF type
// DEF flags
#define ITEMF_VIEW 1     // on its page's view. Smart home shows a tile, comfort a gauge (sensor) or chip (toggle)
#define ITEMF_MENU 2     // a row in its page's menu
#define ITEMF_RO 4       // shown but can't be changed on the panel, like a toggle for a binary sensor
#define ITEMF_COMFORT 8  // on the comfort page, else smart home
#define ITEMF_TREND 16   // panel keeps 24 hours of it for the sensors view, only the first N_TRENDS items
#define ITEMF_LEVELS 32  // coloured by its levels

enum { NOTE_INFO, NOTE_WARNING, NOTE_ALARM };

// ---- Pictures (the screensaver background), in one of two formats:
//   PIC_PALETTE  16 colours, each row PackBits-compressed on its own (plink_pack_row) so changed rows can be sent
//                alone with PATCH. For drawings like the weather picture.
//   PIC_JPEG     baseline JPEG from pjpeg.h. Y at 2x1, a restart marker after every MCU row, width a multiple of
//                16, height of 8. For photos, always sent whole.
// A transfer is BEGIN or PATCH, then DATA up to len bytes, then END. The panel answers with PIC_ACKs and centers
// pictures smaller than the screen.
// BEGIN, a whole picture: id, format, w (2), h (2), len (4), then for PIC_PALETTE the palette (16 x RGB)
enum { PIC_PALETTE, PIC_JPEG };
#define K_PIC_BEGIN 0x330
// PATCH, rows y..y+count-1 of the current 16-colour picture: id, y (2), count (2), len (4), then a new palette or
// none. With no rows only the palette changes.
#define K_PIC_PATCH 0x331
#define K_PIC_DATA 0x332   // compressed bytes, a multiple of 4 for pbulk. Bytes past len are ignored
#define K_PIC_END 0x333    // plink_crc32 of the compressed bytes
#define K_PIC_ACK 0x334    // panel -> ESP: status (PIC_*) << 24 | id << 16 | bytes received
#define K_PIC_CLEAR 0x335  // forget the picture

#define PIC_POOL 14336  // panel memory for a picture, plus room behind it for a patch while it comes in

// ANIM_BEGIN, an animation drawn over the screensaver picture: id, w (2, even), h (2), scale (1 or 2), x (2),
// y (2) for where it goes on screen, transparent colour index (ANIM_OPAQUE = none), len (4), palette (16 x RGB).
// Then DATA up to len bytes and END, as for a picture. The data is one plink_anim_record per frame. The first is
// the whole first frame, the last one takes the last frame back to the first.
#define K_ANIM_BEGIN 0x336
#define K_ANIM_CLEAR 0x337  // forget the animation
#define ANIM_OPAQUE 16
#define ANIM_POOL 6144  // panel memory for an animation, one inflated frame of w x h / 2 bytes plus the records

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
