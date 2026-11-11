// The messages that move a copy on the panel, and the scripts copies run there, see K_OVERLAY_SCRIPT. A script is
// text like this, made into the panel's steps by compile():
//   clip sail                    # the frames of a clip of the animation's, or clip 4 to 8, or clip 3
//   y += sin(ms / 20, 3)         # bound: worked out by the panel each step from now on
//   left:
//     drift -0.9, 0              # px a second
//     wait until x <= 40
//     move 40, y                 # at once, or over ms and eased
//     clip tack at end           # when a round of the clip it plays ends
//     wait rounds 1
//     flip x
//     goto left
// docs/panel.md has all of it.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "pbulk.h"
#include "plink.h"

namespace flexit::panel::overlay {

inline int16_t clamp16(int v) { return (int16_t) std::clamp(v, -32768, 32767); }
inline uint16_t clamp_u16(int v) { return (uint16_t) std::clamp(v, 0, 65535); }
// v to a hundredth, or to places, as a script takes it
inline std::string decimal(float v, int places = 2) {
  char b[24];
  snprintf(b, sizeof b, "%.*f", places, v);
  return b;
}

inline constexpr const char *CHANNELS[] = {"x",     "y",     "pan_x", "pan_y",   "w",       "h",
                                           "turn",  "phase", "size",  "size_x",  "size_y",  "opacity",
                                           "speed", "light", "level", "pivot_x", "pivot_y", "depth"};
inline constexpr const char *EASES[] = {"none", "in", "out", "both"};          // OVERLAY_EASE_* bits
inline constexpr const char *SLOWS[] = {"none", "low", "high", "both"};        // OVERLAY_WAVE_SLOW_* bits
inline constexpr const char *EDGES[] = {"screen", "cross", "bounce"};          // OVERLAY_EDGE_*
inline constexpr const char *BLENDS[] = {"mix", "add", "screen", "multiply"};  // OVERLAY_BLEND_*, 32 apart
static_assert(sizeof CHANNELS / sizeof *CHANNELS == OVERLAY_CHANNELS, "a name for each channel");

inline bool share(int ch) { return ch >= OVERLAY_SIZE && ch < OVERLAY_PIVOT_X; }
inline int no_change(int ch) { return share(ch) ? 100 : 0; }
// What a bind's value is in to a GLIDE's one: thousandths of a px, degree or colour, tenths of a percent
inline int fine_of(int ch) { return ch < OVERLAY_SIZE ? 1000 : share(ch) ? 10 : 1; }

// ---- the messages, laid out as in plink_keys.h, the place left for whoever sends them

namespace msg {

inline void put16(std::vector<uint8_t> &m, int at, int v) { plink_put16(&m[at], (uint16_t) v); }

// What a GLIDE takes for each channel
struct Range {
  int lo, hi;
};
inline constexpr Range ANY = {-32768, 32767};
inline constexpr Range RANGES[] = {ANY,
                                   ANY,
                                   ANY,
                                   ANY,
                                   {0, OVERLAY_BOX_MAX},
                                   {0, OVERLAY_BOX_MAX},
                                   ANY,
                                   ANY,
                                   {OVERLAY_SIZE_MIN, OVERLAY_SIZE_MAX},
                                   {-OVERLAY_SIZE_MAX, OVERLAY_SIZE_MAX},
                                   {-OVERLAY_SIZE_MAX, OVERLAY_SIZE_MAX},
                                   {0, OVERLAY_OPACITY_MAX},
                                   {0, OVERLAY_SPEED_MAX},
                                   {0, OVERLAY_LIGHT_MAX},
                                   {0, OVERLAY_LEVEL_MAX},
                                   {0, OVERLAY_PIVOT_MAX},
                                   {0, OVERLAY_PIVOT_MAX},
                                   {0, OVERLAY_NEAREST}};
static_assert(sizeof RANGES / sizeof *RANGES == OVERLAY_CHANNELS, "a range for each channel");

// A channel and what it goes to
using Each = std::pair<int, int>;

// From at, each channel and its value
inline void put_each(std::vector<uint8_t> &m, int at, const std::vector<Each> &to) {
  for (auto &[ch, v] : to) {
    m[at] = (uint8_t) ch;
    put16(m, at + 1, clamp16(v));
    at += OVERLAY_GLIDE_EACH;
  }
}

// Each channel to its value over ms, in the channels' units
inline std::vector<uint8_t> glide(const std::vector<Each> &to, int ms, int ease) {
  std::vector<uint8_t> m(OVERLAY_GLIDE_HEAD + OVERLAY_GLIDE_EACH * to.size());
  put16(m, OVERLAY_GLIDE_MS, clamp_u16(ms));
  m[OVERLAY_GLIDE_EASE] = (uint8_t) ease;
  put_each(m, OVERLAY_GLIDE_HEAD, to);
  return m;
}

// Each channel drifting at its value a second, in hundredths of a px or a colour, tenths of a degree on turn
inline std::vector<uint8_t> drift(const std::vector<Each> &at) {
  std::vector<uint8_t> m(OVERLAY_DRIFT_HEAD + OVERLAY_GLIDE_EACH * at.size());
  put_each(m, OVERLAY_DRIFT_HEAD, at);
  return m;
}

// The OVERLAY_VIEW_* in mask as flags has them
inline std::vector<uint8_t> view(unsigned mask, unsigned flags) {
  std::vector<uint8_t> m(OVERLAY_VIEW_LEN);
  m[OVERLAY_VIEW_MASK] = (uint8_t) mask;
  m[OVERLAY_VIEW_FLAGS] = (uint8_t) (flags & mask);
  return m;
}
inline std::vector<uint8_t> flip(bool x, bool y) {
  return view(OVERLAY_VIEW_FLIP_X | OVERLAY_VIEW_FLIP_Y, (x ? OVERLAY_VIEW_FLIP_X : 0) | (y ? OVERLAY_VIEW_FLIP_Y : 0));
}

inline std::vector<uint8_t> area(int left, int top, int right, int bottom, int edge_x, int edge_y) {
  std::vector<uint8_t> m(OVERLAY_AREA_LEN);
  put16(m, OVERLAY_AREA_X0, clamp16(left));
  put16(m, OVERLAY_AREA_Y0, clamp16(top));
  put16(m, OVERLAY_AREA_X1, clamp16(right));
  put16(m, OVERLAY_AREA_Y1, clamp16(bottom));
  m[OVERLAY_AREA_EDGES] = (uint8_t) edge_x;
  m[OVERLAY_AREA_EDGES + 1] = (uint8_t) edge_y;
  return m;
}

// Speeds in tenths of a px a second
inline std::vector<uint8_t> wander(int speed_min, int speed_max, int heading, int spread, int leg_min, int leg_max,
                                   int rest_min, int rest_max) {
  std::vector<uint8_t> m(OVERLAY_WANDER_LEN);
  put16(m, OVERLAY_WANDER_SPEED, std::max(0, speed_min));
  put16(m, OVERLAY_WANDER_SPEED + 2, std::max(0, speed_max));
  put16(m, OVERLAY_WANDER_HEADING, clamp16(heading));
  put16(m, OVERLAY_WANDER_SPREAD, clamp16(spread));
  put16(m, OVERLAY_WANDER_LEG, clamp_u16(leg_min));
  put16(m, OVERLAY_WANDER_LEG + 2, clamp_u16(leg_max));
  put16(m, OVERLAY_WANDER_REST, clamp_u16(rest_min));
  put16(m, OVERLAY_WANDER_REST + 2, clamp_u16(rest_max));
  return m;
}

inline std::vector<uint8_t> face(int facing, bool mirror, int tilt, int rate, bool level) {
  std::vector<uint8_t> m(OVERLAY_FACE_LEN);
  m[OVERLAY_FACE_FLAGS] = (uint8_t) ((mirror ? OVERLAY_FACE_MIRROR : 0) | (level ? OVERLAY_FACE_LEVEL : 0));
  put16(m, OVERLAY_FACE_FACING, (facing % 360 + 360) % 360);
  put16(m, OVERLAY_FACE_TILT, std::clamp(tilt, 0, 180));
  put16(m, OVERLAY_FACE_RATE, std::clamp(rate, 0, 32767));
  return m;
}

// Its colours as shades of rgb, 0xRRGGBB, or its own with none
inline std::vector<uint8_t> tint(std::optional<uint32_t> rgb) {
  std::vector<uint8_t> m(OVERLAY_TINT_LEN);
  m[OVERLAY_TINT_ON] = rgb.has_value();
  for (int k = 0; k < 3 && rgb; k++)
    m[OVERLAY_TINT_RGB + k] = (uint8_t) (*rgb >> (16 - 8 * k));
  return m;
}

// what, an OVERLAY_BIND_* op | what it binds, to the value in its bytes, none for a FREE
inline std::vector<uint8_t> bind(unsigned what, const std::vector<uint8_t> &value) {
  std::vector<uint8_t> m(OVERLAY_BIND_VALUE);
  m[OVERLAY_BIND_WHAT] = (uint8_t) what;
  m.insert(m.end(), value.begin(), value.end());
  return m;
}
inline bool frees(const std::vector<uint8_t> &bind) {
  return (bind[OVERLAY_BIND_WHAT] & OVERLAY_BIND_OP) == OVERLAY_BIND_FREE;
}

// What message m of key sets, a bit each: a GLIDE's and DRIFT's channels, a VIEW's mask, all of it for the rest
inline uint32_t sets(uint16_t key, const std::vector<uint8_t> &m) {
  if (key == K_OVERLAY_VIEW)
    return m[OVERLAY_VIEW_MASK];
  if (key != K_OVERLAY_GLIDE && key != K_OVERLAY_DRIFT)
    return 0xFFFFFFFF;
  uint32_t bits = 0;
  for (size_t at = key == K_OVERLAY_GLIDE ? (size_t) OVERLAY_GLIDE_HEAD : (size_t) OVERLAY_DRIFT_HEAD; at < m.size();
       at += OVERLAY_GLIDE_EACH)
    bits |= 1u << m[at];
  return bits;
}
static_assert(OVERLAY_CHANNELS <= 32, "a bit a channel");

// Whether m takes the place of was, an earlier one of key to the same copy: it sets all that one did, a BIND what
// that one bound with its op, or all three with a FREE
inline bool replaces(uint16_t key, const std::vector<uint8_t> &m, const std::vector<uint8_t> &was) {
  if (key != K_OVERLAY_BIND)
    return !(sets(key, was) & ~sets(key, m));
  uint8_t a = m[OVERLAY_BIND_WHAT], b = was[OVERLAY_BIND_WHAT];
  return !((a ^ b) & ~OVERLAY_BIND_OP) && (frees(m) || a == b);
}

}  // namespace msg

// ---- scripts

// A script made into the panel's steps, with what's filled in as it's sent
struct Program {
  enum { PLACE, TAG, FADE };  // the place of another copy, its END's tag, a fade as long as its own (4 bytes)
  struct Patch {
    uint16_t at;
    uint8_t kind;
    std::string name;  // PLACE's copy
    int copy;
  };
  // Its first steps while they only send numbers: the messages they are, which the panel keeps without a script
  struct Told {
    uint16_t key;
    std::vector<uint8_t> m;
  };
  std::vector<Told> told;
  std::vector<uint8_t> bytes;  // the rest, for the panel to run
  std::vector<Patch> patches;
  int regs = 0;         // the registers of its own it takes
  int binds = 0;        // what it binds at most at a time
  bool frames = false;  // it plays frames, so it needs a frame of its own
  bool roams = false;   // it has its copy wander or face where it goes, OVERLAY_ROAM of the panel's pool
  // What its tells give other copies at the most: bytes of the pool, for a wander or face and for what they're
  // bound to, and binds
  size_t gives = 0;
  int gives_binds = 0;
  bool ends() const {
    for (auto &p : patches)
      if (p.kind == TAG)
        return true;
    return false;
  }
};

// An animation's clips by name: first and last frame, and the w x h they show from its frames' top left, 0 for all
struct Clip {
  int first, last, w = 0, h = 0;
};
using Clips = std::map<std::string, Clip>;
// Numbers by name for a script, as if written in it
using Consts = std::map<std::string, double>;

namespace text {

struct Tok {
  char kind;  // 'n' a number, 'w' a word, 'o' an operator, ';' a statement's end, 0 the end
  std::string s;
  double v;
  int line;
};

template<size_t N> int index(const char *const (&names)[N], const std::string &s) {
  for (size_t i = 0; i < N; i++)
    if (s == names[i])
      return (int) i;
  return -1;
}

// What a GET reads after the channels, as the script calls it, OVERLAY_GET_*
inline constexpr const char *READS[] = {"frame", "frames", "rounds", "ms",   "frame_w", "frame_h",
                                        "here",  "at_x",   "at_y",   "seed", "row"};
static_assert(sizeof READS / sizeof *READS == OVERLAY_GETS - OVERLAY_GET_FRAME, "a name for each");
// What can be bound besides the channels and the registers, OVERLAY_BIND_*
inline constexpr const char *BOUND[] = {"frame", "row.shift", "row.size", "row.opacity", "row.light", "row.phase"};
static_assert(sizeof BOUND / sizeof *BOUND == OVERLAY_BIND_TARGETS - OVERLAY_BIND_FRAME, "a name for each");
inline constexpr int BOUND_FINE[] = {1, 1000, 10, 10, 10, 1000};

// What a script can work out with: its name, the OVERLAY_OP_* it is, 0 for those made of others, and the fewest and
// most it takes, -1 for any number
struct Func {
  const char *name;
  uint8_t op;
  int least, most;
};
inline constexpr Func FUNCS[] = {{"rand", OVERLAY_OP_RAND, 2, 2},
                                 {"sin", OVERLAY_OP_SIN, 2, 2},
                                 {"cos", OVERLAY_OP_COS, 2, 2},
                                 {"abs", OVERLAY_OP_ABS, 1, 1},
                                 {"min", OVERLAY_OP_MIN, 2, 2},
                                 {"max", OVERLAY_OP_MAX, 2, 2},
                                 {"wrap", OVERLAY_OP_WRAP, 2, 2},
                                 {"atan", OVERLAY_OP_ATAN, 2, 2},
                                 {"hypot", OVERLAY_OP_HYPOT, 2, 2},
                                 {"hash", OVERLAY_OP_HASH, 2, 2},
                                 {"noise", OVERLAY_OP_NOISE, 3, 3},
                                 {"ease", OVERLAY_OP_EASE, 3, 3},
                                 {"scale", OVERLAY_OP_SCALE, 3, 3},
                                 {"if", OVERLAY_OP_IF, 3, 3},
                                 {"wave", OVERLAY_OP_WAVE, 5, 9},
                                 {"pick", OVERLAY_OP_PICK, 2, -1},
                                 {"curve", OVERLAY_OP_CURVE, 3, -1},
                                 {"dist", OVERLAY_OP_DIST, 2, 2},
                                 {"angle", OVERLAY_OP_ANGLE, 2, 2},
                                 {"clamp", 0, 3, 3},
                                 {"lerp", 0, 4, 4}};
inline const Func *func(const std::string &name) {
  for (const Func &f : FUNCS)
    if (name == f.name)
      return &f;
  return nullptr;
}
// Numbers a script has by name: what ease() and wave() take
inline const Consts NAMED = {{"ease_in", OVERLAY_EASE_IN},
                             {"ease_out", OVERLAY_EASE_OUT},
                             {"ease_both", OVERLAY_EASE_IN | OVERLAY_EASE_OUT},
                             {"slow_low", OVERLAY_WAVE_SLOW_LOW},
                             {"slow_high", OVERLAY_WAVE_SLOW_HIGH},
                             {"slow_both", OVERLAY_WAVE_SLOW_LOW | OVERLAY_WAVE_SLOW_HIGH},
                             {"random", OVERLAY_WAVE_RANDOM}};

inline constexpr const char *WORDS[] = {
    "goto",  "if",    "wait",   "until", "to",     "clip",   "at",     "end",    "rate", "move",
    "over",  "ease",  "glide",  "drift", "spin",   "scroll", "grow",   "flip",   "self", "area",
    "mark",  "fade",  "stop",   "depth", "shared", "wander", "face",   "wave",   "map",  "hold",
    "slow",  "tilt",  "mirror", "level", "jitter", "tint",   "repeat", "shape",  "tell", "none",
    "pivot", "blend", "soft",   "and",   "or",     "not",    "free",   "smooth", "cycle"};

// The OVERLAY_GET_* of a read, -1 for none
inline int read_of(const std::string &s) {
  int c = index(CHANNELS, s), r = index(READS, s);
  return c >= 0 ? c : r >= 0 ? OVERLAY_GET_FRAME + r : -1;
}

// The drifts by name, of the channels from first on, n of them, in their messages' units
struct Drift {
  const char *word;
  int first, n, scale;
};
inline constexpr Drift DRIFTS[] = {{"drift", OVERLAY_X, 2, 100},
                                   {"scroll", OVERLAY_PAN_X, 2, 100},
                                   {"grow", OVERLAY_W, 2, 100},
                                   {"spin", OVERLAY_TURN, 1, 10},
                                   {"cycle", OVERLAY_PHASE, 1, 100}};

// A part of what a script works out
// A value is worked through from its top down, a call of fold(), make_fine() or emit() in another for each operator
// or function on top of another, and read with a call of expr() and atom() for each bracket in another. An ESP32's
// loop has 8 KB of stack for it all, and running out of it writes over the heap. So a value is no higher than
// HIGHEST and its brackets no deeper than NESTED, and none of it starts with less of the stack left than it may
// take: STACK for a script, where a statement takes 0.7 KB, a wave 1 KB more and what they call another, then DEEPER
// at each bracket, 208 bytes, and each step up a value, 112.
inline constexpr int HIGHEST = 12;
inline constexpr int NESTED = 6;
inline constexpr size_t STACK = 4608, DEEPER = 2048;
inline bool stack_has(size_t bytes) {
  return (uintptr_t) __builtin_frame_address(0) - (uintptr_t) pxTaskGetStackStart(nullptr) >= bytes;
}

struct Node {
  enum Kind { NUM, REG, GET, CALL, OP } kind = NUM;
  double v = 0;              // NUM's
  int reg = 0;               // REG's
  int get = 0;               // GET's OVERLAY_GET_*
  uint8_t op = 0;            // OP's OVERLAY_OP_*, which takes its args
  uint8_t high = 1;          // how many of it are on top of each other, itself one
  const Func *fn = nullptr;  // CALL's
  std::vector<Node> args;
  std::vector<std::string> places;  // GET's, dist's and angle's copies, "" for its own
  // high from its args. A clamp counts as 3 and a lerp as 4: the 2 and 3 they're made of on top of each other, and
  // the making, see Compiler::made().
  void rise() {
    uint8_t under = 0;
    for (const Node &a : args)
      under = std::max(under, a.high);
    high = (uint8_t) (under + (kind == CALL && !fn->op ? fn->least : 1));
  }
  static Node num(double v) {
    Node n;
    n.v = v;
    return n;
  }
  static Node of(uint8_t op, std::vector<Node> args) {
    Node n;
    n.kind = OP;
    n.op = op;
    n.args = std::move(args);
    n.rise();
    return n;
  }
  static Node read(int get) {
    Node n;
    n.kind = GET;
    n.get = get;
    n.places.emplace_back();
    return n;
  }
  static Node call(const char *name, std::vector<Node> args) {
    Node n;
    n.kind = CALL;
    n.fn = func(name);
    n.args = std::move(args);
    n.rise();
    return n;
  }
};

// The number a node is if it needs no working out on the panel
inline std::optional<double> fold(const Node &n) {
  if (n.kind == Node::NUM)
    return n.v;
  if (n.kind != Node::OP && !(n.kind == Node::CALL && (n.fn->op == OVERLAY_OP_MIN || n.fn->op == OVERLAY_OP_MAX ||
                                                       n.fn->op == OVERLAY_OP_ABS || n.fn->op == OVERLAY_OP_IF)))
    return {};
  double a[3] = {};
  if (n.args.size() > 3)
    return {};
  for (size_t i = 0; i < n.args.size(); i++) {
    auto v = fold(n.args[i]);
    if (!v)
      return {};
    a[i] = *v;
  }
  switch (n.kind == Node::OP ? n.op : n.fn->op) {
    case OVERLAY_OP_ADD:
      return a[0] + a[1];
    case OVERLAY_OP_SUB:
      return a[0] - a[1];
    case OVERLAY_OP_MUL:
      return a[0] * a[1];
    case OVERLAY_OP_DIV:
      return a[1] ? std::trunc(a[0] / a[1]) : 0;
    case OVERLAY_OP_MOD:
      return a[1] ? std::fmod(std::trunc(a[0]), std::trunc(a[1])) : 0;
    case OVERLAY_OP_SCALE:
      return a[2] ? std::trunc(a[0] * a[1] / a[2]) : 0;
    case OVERLAY_OP_NEG:
      return -a[0];
    case OVERLAY_OP_NOT:
      return !a[0];
    case OVERLAY_OP_LT:
      return a[0] < a[1];
    case OVERLAY_OP_LE:
      return a[0] <= a[1];
    case OVERLAY_OP_EQ:
      return a[0] == a[1];
    case OVERLAY_OP_NE:
      return a[0] != a[1];
    case OVERLAY_OP_GE:
      return a[0] >= a[1];
    case OVERLAY_OP_GT:
      return a[0] > a[1];
    case OVERLAY_OP_AND:
      return a[0] && a[1];
    case OVERLAY_OP_OR:
      return a[0] || a[1];
    case OVERLAY_OP_MIN:
      return std::min(a[0], a[1]);
    case OVERLAY_OP_MAX:
      return std::max(a[0], a[1]);
    case OVERLAY_OP_ABS:
      return std::fabs(a[0]);
    case OVERLAY_OP_IF:
      return a[0] ? a[1] : a[2];
  }
  return {};
}

// What making n k times finer scales: a bit for each of its args, none if it's n as it is. What's a share of one of
// its parts takes the k there, so the sine of a wave 3 px high isn't whole px but thousandths.
__attribute__((noinline)) inline unsigned fine_args(const Node &n) {
  static constexpr struct {
    const char *name;
    uint8_t args;
  } CALLS[] = {{"sin", 2},  {"cos", 2},  {"min", 3},   {"max", 3}, {"abs", 3},  {"rand", 3},
               {"lerp", 3}, {"ease", 3}, {"noise", 4}, {"if", 6},  {"wave", 6}, {"clamp", 7}};
  if (n.kind == Node::OP)
    switch (n.op) {
      case OVERLAY_OP_ADD:
      case OVERLAY_OP_SUB:
      case OVERLAY_OP_MOD:
        return 3;
      case OVERLAY_OP_NEG:
      case OVERLAY_OP_DIV:
        return 1;
      case OVERLAY_OP_MUL:
      case OVERLAY_OP_SCALE:  // the part that isn't a number, which stays as it is
        return fold(n.args[0]) ? 2 : 1;
    }
  if (n.kind != Node::CALL)
    return 0;
  if (n.fn->op == OVERLAY_OP_SCALE)
    return fold(n.args[0]) ? 2 : 1;
  for (const auto &c : CALLS)
    if (!strcmp(n.fn->name, c.name))
      return c.args;
  return 0;
}
// n k times finer where that's all of it: a number, or a pick's or a curve's list if it fits the panel's 16 bits
__attribute__((noinline)) inline bool fine_whole(Node &n, int k) {
  if (auto c = fold(n)) {
    n = Node::num(std::round(*c * k));
    return true;
  }
  if (n.kind != Node::CALL || (n.fn->op != OVERLAY_OP_PICK && n.fn->op != OVERLAY_OP_CURVE))
    return false;
  size_t first = n.fn->op == OVERLAY_OP_PICK ? 1 : 2;
  for (size_t i = first; i < n.args.size(); i++)
    if (std::fabs(n.args[i].v * k) > 32767)
      return false;
  for (size_t i = first; i < n.args.size(); i++)
    n.args[i].v = std::round(n.args[i].v * k);
  return true;
}
__attribute__((noinline)) inline void times(Node &n, int k) {
  std::vector<Node> both;
  both.push_back(std::move(n));
  both.push_back(Node::num(k));
  n = Node::of(OVERLAY_OP_MUL, std::move(both));
}
// n times k in place, worked out as finely as that: a bind's value from a script's px, degrees and percent
inline void make_fine(Node &n, int k) {
  if (fine_whole(n, k))
    return;
  unsigned args = fine_args(n);
  if (!args) {
    times(n, k);
    return;
  }
  for (size_t i = 0; i < n.args.size(); i++)
    if (args >> i & 1)
      make_fine(n.args[i], k);
  n.rise();
}
inline Node fine(const Node &n, int k) {
  Node out = n;
  if (k != 1)
    make_fine(out, k);
  return out;
}

// A value as a step takes it: what it's worked out from, and the number it is if it needs no working out
struct Val {
  Node n;
  std::optional<int32_t> num;
};

struct Compiler {
  std::vector<Tok> t;
  size_t i = 0;
  std::string err;
  const Clips &clips;
  const Consts &consts;
  std::map<std::string, int> vars;  // registers by name, 0..7 its own, from 0 up, and the shared ones
  int own = 0, shared = OVERLAY_OWN_REGS;
  Program p;
  std::vector<uint8_t> &b = p.bytes;
  std::map<std::string, int> labels;
  std::optional<std::string> target;  // the copy a tell sends to, "" for its own
  std::vector<uint8_t> bound;         // what it has bound so far, an OVERLAY_BIND_* each, for how many at a time
  int since = -1;                     // the register a wave counts its time from, while it's the time of b's end
  size_t since_at = 0;
  struct Jump {
    size_t at;
    std::string label;
    int line;
  };
  std::vector<Jump> jumps;

  Compiler(const Clips &c, const Consts &k) : clips(c), consts(k) {}

  bool fail(const std::string &why) {
    if (err.empty())
      err = "line " + std::to_string(t[std::min(i, t.size() - 1)].line) + ": " + why;
    return false;
  }

  // ---- reading

  bool lex(const std::string &s) {
    int line = 1;
    for (size_t k = 0; k < s.size();) {
      char c = s[k];
      if (c == '#') {
        while (k < s.size() && s[k] != '\n')
          k++;
      } else if (c == '\n' || c == ';') {
        t.push_back({';', "", 0, line});
        line += c == '\n';
        k++;
      } else if (isspace((unsigned char) c)) {
        k++;
      } else if (isdigit((unsigned char) c) || (c == '.' && k + 1 < s.size() && isdigit((unsigned char) s[k + 1]))) {
        char *e;
        double v = strtod(s.c_str() + k, &e);
        t.push_back({'n', "", v, line});
        k = (size_t) (e - s.c_str());
      } else if (isalpha((unsigned char) c) || c == '_') {
        size_t e = k;
        while (e < s.size() && (isalnum((unsigned char) s[e]) || s[e] == '_' || s[e] == '.'))
          e++;
        t.push_back({'w', s.substr(k, e - k), 0, line});
        k = e;
      } else {
        static const char *const PAIRS[] = {"<=", ">=", "==", "!=", ":=", "+=", "-=", "*="};
        std::string two = s.substr(k, 2);
        bool pair = index(PAIRS, two) >= 0;
        if (!pair && !strchr("+-*/%(),<>=:", c)) {
          t.push_back({0, "", 0, line});
          i = t.size() - 1;
          return fail(std::string("what's '") + c + "'?");
        }
        t.push_back({'o', pair ? two : std::string(1, c), 0, line});
        k += pair ? 2 : 1;
      }
    }
    t.push_back({';', "", 0, line});
    t.push_back({0, "", 0, line});
    return true;
  }

  const Tok &peek(size_t k = 0) const { return t[std::min(i + k, t.size() - 1)]; }
  bool is(const char *s, size_t k = 0) const { return peek(k).kind != 'n' && peek(k).s == s; }
  bool take(const char *s) {
    if (!is(s))
      return false;
    i++;
    return true;
  }
  bool need(const char *s) { return take(s) || fail(std::string("'") + s + "' expected"); }
  bool word(std::string &w) {
    if (peek().kind != 'w')
      return fail("a name expected");
    w = t[i++].s;
    return true;
  }
  bool reserved(const std::string &w) const {
    return read_of(w) >= 0 || func(w) || index(WORDS, w) >= 0 || index(BOUND, w) >= 0 || NAMED.count(w);
  }

  // A copy: its own, an animation's copy 0, or name.copy
  bool place(std::string &out) {
    out.clear();
    if (is(")") || is(","))
      return true;
    if (!word(out))
      return false;
    if (out == "self")
      out.clear();
    size_t dot = out.rfind('.');
    if (dot != std::string::npos && isdigit((unsigned char) out[dot + 1]) && atoi(&out[dot + 1]) >= OVERLAY_PLACES)
      return fail("no copy " + out.substr(dot + 1) + ", they're 0 to " + std::to_string(OVERLAY_PLACES - 1));
    return true;
  }

  // What it works out, the ones that bind the least first: or, and, not, the six that compare, + and -, then * / %.
  // A not takes all that compares after it.
  struct Op {
    const char *text;
    uint8_t op, level;
  };
  static constexpr Op OPS[] = {{"not", OVERLAY_OP_NOT, 2}, {"or", OVERLAY_OP_OR, 0}, {"and", OVERLAY_OP_AND, 1},
                               {"<=", OVERLAY_OP_LE, 3},   {">=", OVERLAY_OP_GE, 3}, {"==", OVERLAY_OP_EQ, 3},
                               {"!=", OVERLAY_OP_NE, 3},   {"<", OVERLAY_OP_LT, 3},  {">", OVERLAY_OP_GT, 3},
                               {"+", OVERLAY_OP_ADD, 4},   {"-", OVERLAY_OP_SUB, 4}, {"*", OVERLAY_OP_MUL, 5},
                               {"/", OVERLAY_OP_DIV, 5},   {"%", OVERLAY_OP_MOD, 5}};
  static constexpr const Op *OP_NOT = &OPS[0];
  int nested = 0;  // brackets and calls it's inside

  // The same for a text that's there as it is, so the caller's stack doesn't hold a string for it
  __attribute__((noinline)) bool fail(const char *why) { return fail(std::string(why)); }
  bool low(const Node &n) { return n.high <= HIGHEST || fail("too much to work out at once, in parts it goes"); }
  bool deeper() { return stack_has(DEEPER) || fail("no room on the stack to work it out"); }

  // The operator between two values that comes next, nullptr if none does
  const Op *between() const {
    for (const Op *o = OP_NOT + 1; o < std::end(OPS); o++)
      if (is(o->text))
        return o;
    return nullptr;
  }

  // The last of ops takes its values off vals
  __attribute__((noinline)) bool apply(std::vector<Node> &vals, std::vector<const Op *> &ops) {
    uint8_t op = ops.back()->op;
    ops.pop_back();
    std::vector<Node> args;
    if (op != OVERLAY_OP_NOT) {
      Node rhs = std::move(vals.back());
      vals.pop_back();
      Node &n = vals.back();
      // a * b / c in one go, so a * b can be more than a register holds
      if (op == OVERLAY_OP_DIV && n.kind == Node::OP && n.op == OVERLAY_OP_MUL) {
        op = OVERLAY_OP_SCALE;
        args = std::move(n.args);
      } else {
        args.push_back(std::move(n));
      }
      args.push_back(std::move(rhs));
    } else {
      args.push_back(std::move(vals.back()));
    }
    vals.back() = Node::of(op, std::move(args));
    return low(vals.back());
  }

  // A value: what atom() reads with operators between, each applied once the one after it binds no tighter. They
  // wait in lists of their own, so only brackets and calls go deeper in the processor's stack.
  bool expr(Node &n) {
    static_assert(NESTED == 6, "as the text says");
    if (nested > NESTED)
      return fail("more than 6 brackets inside each other");
    if (!deeper())
      return false;
    nested++;
    std::vector<Node> vals;
    std::vector<const Op *> ops;
    for (;;) {
      while ((ops.empty() || ops.back()->level <= OP_NOT->level) && take("not"))
        ops.push_back(OP_NOT);
      vals.emplace_back();
      if (!atom(vals.back()))
        return false;
      const Op *next = between();
      while (!ops.empty() && (!next || ops.back()->level >= next->level))
        if (!apply(vals, ops))
          return false;
      if (!next)
        break;
      ops.push_back(next);
      i++;
    }
    n = std::move(vals[0]);
    nested--;
    return true;
  }

  // A number, a name, a call or a value in brackets, with any minus signs before it
  bool atom(Node &n) {
    int minus = 0;
    while (take("-"))
      minus++;
    const Func *f = nullptr;
    if (peek().kind == 'n') {
      n = Node::num(peek().v);
      i++;
    } else if (take("(")) {
      if (!expr(n) || !need(")"))
        return false;
    } else if (peek().kind != 'w') {
      return fail("a number or a name expected");
    } else if (!named(n, f) || (f && !call(n, *f))) {
      return false;
    }
    return !minus || negate(n, minus);
  }

  __attribute__((noinline)) bool negate(Node &n, int times) {
    for (; times; times--) {
      std::vector<Node> of;
      of.push_back(std::move(n));
      n = Node::of(OVERLAY_OP_NEG, std::move(of));
      if (!low(n))
        return false;
    }
    return true;
  }

  // A name as a value: a number by name, a register or what the panel reads. A function's is f, with its ( read.
  __attribute__((noinline)) bool named(Node &n, const Func *&f) {
    std::string w;
    word(w);
    for (const Consts *named : {&consts, &NAMED})
      if (auto c = named->find(w); c != named->end()) {
        n = Node::num(c->second);
        return true;
      }
    if (auto v = vars.find(w); v != vars.end()) {
      n = Node();
      n.kind = Node::REG;
      n.reg = v->second;
      return true;
    }
    if (int g = read_of(w); g >= 0) {
      n = Node::read(g);
      return !take("(") || (place(n.places[0]) && need(")"));
    }
    f = func(w);
    if (!f)
      return fail("what's '" + w + "'?");
    n = Node::call(f->name, {});
    return need("(");
  }

  // A function's values or copies, up to its )
  bool call(Node &n, const Func &f) {
    bool places = f.op == OVERLAY_OP_DIST || f.op == OVERLAY_OP_ANGLE;
    for (int k = 0; !k || take(","); k++) {
      if (places) {
        n.places.emplace_back();
        if (!place(n.places.back()))
          return false;
      } else {
        n.args.emplace_back();
        if (!expr(n.args.back()))
          return false;
      }
    }
    return need(")") && takes(n, f, places);
  }

  // Whether f takes what n got
  __attribute__((noinline)) bool takes(Node &n, const Func &f, bool places) {
    int count = (int) (places ? n.places.size() : n.args.size());
    if (count < f.least || (f.most >= 0 && count > f.most))
      return fail(std::string(f.name) + " takes " + std::to_string(f.least) +
                  (f.most < 0         ? " or more"
                   : f.most > f.least ? " to " + std::to_string(f.most)
                                      : ""));
    if (f.op == OVERLAY_OP_PICK || f.op == OVERLAY_OP_CURVE) {  // its list is numbers the panel has 16 bits for
      size_t first = f.op == OVERLAY_OP_PICK ? 1 : 2;
      if (n.args.size() - first > 255)
        return fail(std::string(f.name) + " takes 255 numbers at most");
      for (size_t a = first; a < n.args.size(); a++) {
        auto c = fold(n.args[a]);
        if (!c || *c != std::round(*c) || std::fabs(*c) > 32767)
          return fail(std::string(f.name) + "'s list is whole numbers, -32767 to 32767");
        n.args[a] = Node::num(*c);
      }
    }
    n.rise();
    return low(n);
  }

  // ---- working it out

  // A value's bytes as they're made: where its places go, how many numbers its OPs have left and the most at a time
  struct Out {
    std::vector<uint8_t> v;
    std::vector<Program::Patch> places;
    int left = 0, most = 0;
    bool regs = false;  // it takes registers of its own
    void op(uint8_t code, int takes) {
      v.push_back(code);
      left += 1 - takes;
      most = std::max(most, left);
    }
    void number(int32_t n) {
      if (n >= -128 && n <= 127) {
        op(OVERLAY_V8, 0);
        v.push_back((uint8_t) n);
      } else if (n >= -32768 && n <= 32767) {
        op(OVERLAY_V16, 0);
        v.push_back((uint8_t) n);
        v.push_back((uint8_t) ((uint16_t) n >> 8));
      } else {
        op(OVERLAY_V32, 0);
        for (int k = 0; k < 4; k++)
          v.push_back((uint8_t) ((uint32_t) n >> 8 * k));
      }
    }
    void place(const std::string &name) {
      if (name.empty()) {
        v.push_back(OVERLAY_SELF);
        return;
      }
      size_t dot = name.rfind('.');
      int copy = 0;
      std::string anim = name;
      if (dot != std::string::npos && dot + 1 < name.size() && isdigit((unsigned char) name[dot + 1])) {
        copy = atoi(name.c_str() + dot + 1);
        anim = name.substr(0, dot);
      }
      places.push_back({(uint16_t) v.size(), Program::PLACE, anim, copy});
      v.push_back(0);
    }
  };

  bool whole(double v, int32_t &out) {
    out = 0;  // a value on every way out: a compiler can't tell that fail() is always false
    if (std::fabs(v) > 2147483647.0)
      return fail("a number too big for the panel");
    out = (int32_t) std::lround(v);
    return std::fabs(v - out) < 1e-9 || fail("only whole numbers there, the panel has no fractions");
  }

  // clamp and lerp as the others they're made of: no less than its low and no more than its high, and from a to b
  // as v goes from 0 to n
  __attribute__((noinline)) static Node made(const Node &n) {
    if (!strcmp(n.fn->name, "clamp"))
      return Node::call("min", {Node::call("max", {n.args[0], n.args[1]}), n.args[2]});
    return Node::of(OVERLAY_OP_ADD,
                    {n.args[0], Node::of(OVERLAY_OP_SCALE,
                                         {Node::of(OVERLAY_OP_SUB, {n.args[1], n.args[0]}), n.args[2], n.args[3]})});
  }
  __attribute__((noinline)) bool emit_made(const Node &n, Out &o) {
    Node as = made(n);
    return emit(as, o);
  }

  // n's OPs into o, what it's made of first
  bool emit(const Node &n, Out &o) {
    if (!deeper())
      return false;
    if (auto c = fold(n)) {
      int32_t v;
      if (!whole(*c, v))
        return false;
      o.number(v);
      return true;
    }
    if (n.kind == Node::REG) {
      o.op((uint8_t) (OVERLAY_VREG + n.reg), 0);
      o.regs |= n.reg < OVERLAY_OWN_REGS;
      return true;
    }
    if (n.kind == Node::GET) {
      o.op(n.places[0].empty() ? OVERLAY_OP_OWN : OVERLAY_OP_GET, 0);
      o.v.push_back((uint8_t) n.get);
      if (!n.places[0].empty())
        o.place(n.places[0]);
      return true;
    }
    uint8_t op = n.kind == Node::OP ? n.op : n.fn->op;
    bool list = n.kind == Node::CALL && (op == OVERLAY_OP_PICK || op == OVERLAY_OP_CURVE);
    if (n.kind == Node::CALL && !op)
      return emit_made(n, o);
    if (n.kind == Node::CALL && (op == OVERLAY_OP_DIST || op == OVERLAY_OP_ANGLE)) {
      o.op(op, 0);
      o.place(n.places[0]);
      o.place(n.places[1]);
      return true;
    }
    // a pick's and a curve's list goes as it is, after what picks from it
    size_t worked = list ? std::min<size_t>(op == OVERLAY_OP_PICK ? 1 : 2, n.args.size()) : n.args.size();
    for (size_t k = 0; k < worked; k++)
      if (!emit(n.args[k], o))
        return false;
    if (list) {
      o.op(op, (int) worked);
      o.v.push_back((uint8_t) (n.args.size() - worked));
      for (size_t k = worked; k < n.args.size(); k++) {
        o.v.push_back((uint8_t) (int) n.args[k].v);
        o.v.push_back((uint8_t) ((uint16_t) (int) n.args[k].v >> 8));
      }
      return true;
    }
    int most = n.kind == Node::CALL ? n.fn->most : 0;
    for (int k = (int) n.args.size(); k < most; k++)  // a wave's holds, jitter and how
      o.number(0);
    o.op(op, std::max(most, (int) n.args.size()));
    return true;
  }

  // n as a value's bytes: a number or a register on its own, or the OPs that work it out
  bool value(const Node &n, Out &o) {
    Out in;
    if (!emit(n, in))
      return false;
    if (in.most > OVERLAY_EXPR_STACK)
      return fail("too much to work out at once, in parts it goes");
    bool one = n.kind == Node::REG || fold(n) || (n.kind == Node::GET) ||
               (n.kind == Node::CALL && (n.fn->op == OVERLAY_OP_DIST || n.fn->op == OVERLAY_OP_ANGLE));
    if (!one)
      o.v.push_back(OVERLAY_VEXPR);
    for (Program::Patch k : in.places) {
      k.at = (uint16_t) (k.at + o.v.size());
      o.places.push_back(k);
    }
    o.v.insert(o.v.end(), in.v.begin(), in.v.end());
    if (!one)
      o.v.push_back(OVERLAY_OP_END);
    o.regs |= in.regs;
    return true;
  }

  // o's bytes onto the script, its places where they are there
  void append(const Out &o) {
    for (Program::Patch k : o.places) {
      k.at = (uint16_t) (k.at + b.size());
      p.patches.push_back(k);
    }
    b.insert(b.end(), o.v.begin(), o.v.end());
  }

  // n as a value onto the script
  bool put(const Node &n) {
    Out o;
    if (!value(n, o))
      return false;
    append(o);
    return true;
  }
  bool put(const Val &v) { return put(v.n); }
  void number(int32_t v) {
    Out o;
    o.number(v);
    append(o);
  }

  // n as a step's value, the number it is if it's one
  bool val(const Node &n, Val &v) {
    v.n = n;
    v.num.reset();
    if (auto c = fold(n)) {
      int32_t k = 0;
      if (!whole(*c, k))
        return false;
      v.num = k;
    }
    return true;
  }
  // n times scale: speeds in tenths or hundredths
  bool scaled(const Node &n, int scale, Val &v) {
    if (auto c = fold(n))
      return val(Node::num(std::round(*c * scale)), v);
    return val(Node::of(OVERLAY_OP_MUL, {n, Node::num(scale)}), v);
  }
  static Val just(int32_t v) { return {Node::num(v), v}; }

  // ---- statements

  void jump(const std::string &label) {
    jumps.push_back({b.size(), label, peek().line});
    b.insert(b.end(), 2, 0);
  }
  // An expression that has to be a number
  bool number(int32_t &v, const char *what) {
    Node n;
    if (!expr(n))
      return false;
    auto c = fold(n);
    return (c && whole(*c, v)) || fail(std::string(what) + " takes a number");
  }
  // A choice of names
  template<size_t N> bool choice(const char *const (&names)[N], int &k) {
    std::string w;
    if (!word(w))
      return false;
    k = index(names, w);
    if (k >= 0)
      return true;
    std::string all;
    for (size_t j = 0; j < N; j++)
      all += (j ? ", " : "") + std::string(names[j]);
    return fail("'" + w + "' isn't one of " + all);
  }

  // Whether what's told already of key leaves m to be told too, rather than sent as the script runs: before its
  // first step and label, to its own copy, and nothing told sets some of what m does, so each comes in its turn
  bool tells(uint16_t key, const std::vector<uint8_t> &m) const {
    if (target || !b.empty() || !labels.empty())
      return false;
    for (auto &t : p.told)
      if (t.key == key && key != K_OVERLAY_BIND && (msg::sets(key, t.m) & msg::sets(key, m)))
        return false;
    return true;
  }

  // A SEND of key's message m, the fields at the given bytes from v where they aren't numbers, to its own copy or
  // the one a tell names. The message has the numbers in already.
  struct Field {
    int at;
    Val v;
  };
  bool send(uint16_t key, std::vector<uint8_t> m, const std::vector<Field> &fields,
            const std::vector<Program::Patch> &places = {}) {
    int worked = 0;
    for (auto &f : fields) {
      if (!f.v.num)
        worked++;
      else
        msg::put16(m, f.at, std::clamp<int32_t>(*f.v.num, -32768, 65535));  // as the panel takes one worked out
    }
    if (!worked && places.empty() && tells(key, m)) {
      p.told.push_back({key, std::move(m)});
      return true;
    }
    if (m.size() > OVERLAY_SEND_MAX)
      return fail("too long to send, " + std::to_string(m.size()) + " bytes of " + std::to_string(OVERLAY_SEND_MAX));
    bool roams = key == K_OVERLAY_WANDER || key == K_OVERLAY_FACE, binds = key == K_OVERLAY_BIND && !msg::frees(m);
    p.roams |= roams && !target;
    if (target) {
      p.gives += (roams ? OVERLAY_ROAM : 0) + (binds ? m.size() - OVERLAY_BIND_VALUE + 1 : 0);
      p.gives_binds += binds;
    }
    b.push_back(OVERLAY_DO_SEND);
    Out to;
    to.place(target.value_or(""));
    append(to);
    b.push_back((uint8_t) (key - K_OVERLAY_FRAMES));
    b.push_back((uint8_t) m.size());
    for (Program::Patch k : places) {
      k.at = (uint16_t) (k.at + b.size());
      p.patches.push_back(k);
    }
    b.insert(b.end(), m.begin(), m.end());
    b.push_back((uint8_t) worked);
    for (auto &f : fields)
      if (!f.v.num) {
        b.push_back((uint8_t) f.at);
        if (!put(f.v))
          return false;
      }
    return true;
  }

  // A glide of each channel to its value over ms, eased
  bool glide(const std::vector<std::pair<int, Val>> &to, const Val &ms, int ease) {
    std::vector<Field> f = {{OVERLAY_GLIDE_MS, ms}};
    std::vector<msg::Each> each;
    for (auto &[ch, v] : to) {
      const msg::Range &r = msg::RANGES[ch];
      if (v.num && (*v.num < r.lo || *v.num > r.hi))
        return fail(std::string(CHANNELS[ch]) + " takes " + std::to_string(r.lo) + " to " + std::to_string(r.hi));
      f.push_back({OVERLAY_GLIDE_HEAD + OVERLAY_GLIDE_EACH * (int) each.size() + 1, v});
      each.push_back({ch, 0});
    }
    if (ms.num && (*ms.num < 0 || *ms.num > 65535))
      return fail("over takes 0 to 65535 ms");
    std::vector<uint8_t> m = msg::glide(each, 0, ease);
    bool known = true;
    for (auto &k : f)
      known &= k.v.num.has_value();
    if (target || (known && tells(K_OVERLAY_GLIDE, m)) || to.size() > OVERLAY_GLIDE_STEP)
      return send(K_OVERLAY_GLIDE, std::move(m), f);
    b.push_back(OVERLAY_DO_GLIDE);  // its own step, shorter than the message
    b.push_back((uint8_t) (to.size() | (unsigned) ease << 6));
    if (!put(ms))
      return false;
    for (auto &[ch, v] : to) {
      b.push_back((uint8_t) ch);
      if (!put(v))
        return false;
    }
    return true;
  }

  // What says, an op | what's bound, is n from now on, k times it for the panel, or free. Told like a message when
  // it takes no registers or other copies, else a step, or a SEND for a tell.
  bool bind(unsigned what, const Node *n, int k) {
    Out o;
    if (n && !value(fine(*n, k), o))
      return false;
    std::vector<uint8_t> m = msg::bind(what, o.v);
    for (auto &at : o.places)
      at.at = (uint16_t) (at.at + OVERLAY_BIND_VALUE);
    unsigned to = what & ~OVERLAY_BIND_OP;
    bool reg = to >= OVERLAY_BIND_REG, script = to == OVERLAY_BIND_FRAME || reg;
    if (target) {
      if (o.regs || script)
        return fail("another copy's is bound by what it has itself, or what's shared");
      return send(K_OVERLAY_BIND, std::move(m), {}, o.places);
    }
    if (!o.regs && !script && o.places.empty() && m.size() <= PBULK_MSG_MAX && tells(K_OVERLAY_BIND, m)) {
      p.told.erase(std::remove_if(p.told.begin(), p.told.end(),
                                  [&](const Program::Told &t) {
                                    return t.key == K_OVERLAY_BIND && msg::replaces(K_OVERLAY_BIND, m, t.m);
                                  }),
                   p.told.end());
      p.told.push_back({K_OVERLAY_BIND, std::move(m)});
      return true;
    }
    bound.erase(std::remove_if(bound.begin(), bound.end(),
                               [&](uint8_t was) { return !((was ^ what) & ~OVERLAY_BIND_OP) && (!n || was == what); }),
                bound.end());
    if (n)
      bound.push_back((uint8_t) what);
    p.binds = std::max(p.binds, (int) bound.size());
    p.frames |= to == OVERLAY_BIND_FRAME;
    b.push_back(OVERLAY_DO_BIND);
    b.push_back((uint8_t) what);
    append(o);
    return true;
  }

  // What a name binds, an OVERLAY_BIND_* target, and how fine its value is. A new name is a register of its own.
  bool bound_by(const std::string &w, bool make, unsigned &to, int &k) {
    int ch = index(CHANNELS, w), other = index(BOUND, w);
    if (ch >= 0) {
      to = (unsigned) ch;
      k = fine_of(ch);
    } else if (other >= 0) {
      to = (unsigned) (OVERLAY_BIND_FRAME + other);
      k = BOUND_FINE[other];
    } else {
      int r;
      if (!reg(w, make, r))
        return false;
      to = (unsigned) (OVERLAY_BIND_REG + r);
      k = 1;
    }
    return true;
  }

  // The register a name is, a new one of its own if it may be made
  bool reg(const std::string &w, bool make, int &r) {
    auto v = vars.find(w);
    if (v == vars.end()) {
      if (consts.count(w) || NAMED.count(w))
        return fail("'" + w + "' is a number");
      if (reserved(w) || !make)
        return fail("what's '" + w + "'?");
      if (own >= OVERLAY_OWN_REGS)
        return fail("only " + std::to_string(OVERLAY_OWN_REGS) + " of its own");
      v = vars.emplace(w, own++).first;
    }
    r = v->second;
    return true;
  }

  // Expressions apart by commas, each times its scale if it has one
  bool vals(std::vector<Val> &v, size_t n, const std::vector<int> &scale = {}) {
    v.resize(n);
    for (size_t k = 0; k < n; k++) {
      Node e;
      if ((k && !need(",")) || !expr(e))
        return false;
      if (!(k < scale.size() && scale[k] != 1 ? scaled(e, scale[k], v[k]) : val(e, v[k])))
        return false;
    }
    return true;
  }
  // Whether hi isn't below lo, or past it with past, where both are numbers
  static bool rising(const Val &lo, const Val &hi, bool past = false) {
    return !lo.num || !hi.num || (past ? *hi.num > *lo.num : *hi.num >= *lo.num);
  }
  bool optional_val(const char *w, Val &v, int dflt) {
    v = just(dflt);
    if (!take(w))
      return true;
    Node e;
    return expr(e) && val(e, v);
  }

  bool statement() {
    if (peek().kind == ';') {
      i++;
      return true;
    }
    bool ok = one();
    if (ok && peek().kind != ';')
      return fail("'" + (peek().kind == 'n' ? std::to_string(peek().v) : peek().s) + "' after the end of it");
    return ok;
  }

  // What a tell can send another copy
  static bool sends(const std::string &w) {
    static const char *const SENDS[] = {"move", "glide",  "drift", "scroll", "grow",   "spin",   "cycle",
                                        "flip", "repeat", "tint",  "depth",  "area",   "wander", "face",
                                        "wave", "map",    "pivot", "blend",  "smooth", "free"};
    return index(SENDS, w) >= 0 || index(CHANNELS, w) >= 0 || index(BOUND, w) > 0;
  }

  bool one() {
    static const char *const BINDS[] = {":=", "+=", "*=", "-="};  // OVERLAY_BIND_SET, _ADD and _MUL, and - as + of -
    std::string w;
    while (peek().kind == 'w' && is(":", 1) && !target) {
      w = t[i].s;
      i += 2;
      if (labels.count(w))
        return fail("'" + w + "' twice");
      labels[w] = (int) b.size();
      if (peek().kind == ';')
        return true;
    }
    if (peek().kind == 'w' && peek(1).kind == 'o' && index(BINDS, peek(1).s) >= 0) {
      unsigned to;
      int op = index(BINDS, peek(1).s), k;
      Node e;
      if (!word(w) || !bound_by(w, op == 0, to, k))
        return false;
      i++;
      if (!expr(e))
        return false;
      if (to >= OVERLAY_BIND_REG && op) {  // a register's own, the once
        static const uint8_t OPS[] = {0, OVERLAY_OP_ADD, OVERLAY_OP_MUL, OVERLAY_OP_SUB};
        Node r;
        r.kind = Node::REG;
        r.reg = (int) to - OVERLAY_BIND_REG;
        b.push_back(OVERLAY_DO_SET);
        b.push_back((uint8_t) r.reg);
        return !target ? put(Node::of(OPS[op], {r, std::move(e)})) : fail("a register is its own copy's");
      }
      if (to >= OVERLAY_CHANNELS && op)
        return fail("'" + w + "' is only bound with :=");
      if (op == 3)
        e = Node::of(OVERLAY_OP_NEG, {std::move(e)});
      return bind((unsigned) (op == 2 ? OVERLAY_BIND_MUL
                              : op    ? OVERLAY_BIND_ADD
                                      : OVERLAY_BIND_SET) |
                      to,
                  &e, op == 2 ? fine_of(OVERLAY_SIZE) : k);
    }
    if (peek().kind == 'w' && is("=", 1) && !reserved(peek().s) && !target)
      return assign();
    if (!word(w))
      return false;
    if (w == "tell") {
      std::string copy;
      if (!place(copy))
        return false;
      if (is("tell"))
        return fail("one tell at a time");
      if (peek().kind != 'w' || !sends(peek().s))
        return fail("tell sends another copy what moves, shows or binds it");
      target = copy;
      bool ok = one();
      target.reset();
      return ok;
    }
    if (w == "shared") {
      do {
        std::string v;
        if (!word(v))
          return false;
        if (reserved(v) || vars.count(v) || consts.count(v))
          return fail("'" + v + "' is taken");
        if (shared == OVERLAY_REGS)
          return fail("only " + std::to_string(OVERLAY_REGS - OVERLAY_OWN_REGS) + " shared");
        vars[v] = shared++;
      } while (take(","));
      return true;
    }
    if (w == "free") {
      do {
        unsigned to;
        int k;
        std::string v;
        if (!word(v) || !bound_by(v, false, to, k) || !bind(OVERLAY_BIND_FREE | to, nullptr, 1))
          return false;
      } while (take(","));
      return true;
    }
    if (w == "goto") {
      std::string l;
      if (!word(l))
        return false;
      b.push_back(OVERLAY_DO_GOTO);
      jump(l);
      return true;
    }
    if (w == "stop") {
      b.push_back(OVERLAY_DO_GOTO);
      jump("");  // past the end
      return true;
    }
    if (w == "if") {
      Node c;
      std::string to;
      if (!expr(c))
        return false;
      b.push_back(OVERLAY_DO_IF);
      if (!put(c) || !need("goto") || !word(to))
        return false;
      jump(to);
      return true;
    }
    if (w == "wait")
      return wait();
    if (w == "clip")
      return clip();
    if (w == "frame" || w == "rate") {
      Node e;
      if (!expr(e))
        return false;
      b.push_back(w == "frame" ? OVERLAY_DO_FRAME : OVERLAY_DO_RATE);
      p.frames = true;
      return put(e);
    }
    if (w == "move" || w == "glide") {
      std::vector<std::pair<int, Val>> to;  // channel and value
      int e = 0;
      Val ms;
      if (w == "move") {
        std::vector<Val> v;
        if (!vals(v, 2))
          return false;
        to = {{OVERLAY_X, v[0]}, {OVERLAY_Y, v[1]}};
      }
      while (w == "glide" && (to.empty() || (peek().kind == 'w' && index(CHANNELS, peek().s) >= 0))) {
        int ch;
        Node n;
        Val v;
        if (!choice(CHANNELS, ch) || !expr(n) || !val(n, v))
          return false;
        for (auto &c : to)
          if (c.first == ch)
            return fail(std::string(CHANNELS[ch]) + " twice");
        to.push_back({ch, v});
      }
      if (!optional_val("over", ms, 0) || (take("ease") && !choice(EASES, e)))
        return false;
      return glide(to, ms, e);
    }
    for (const Drift &d : DRIFTS)
      if (w == d.word) {
        std::vector<Val> v;
        if (!vals(v, (size_t) d.n, std::vector<int>((size_t) d.n, d.scale)))  // to a hundredth, a tenth on turn
          return false;
        std::vector<Field> f;
        std::vector<msg::Each> each;
        for (size_t k = 0; k < v.size(); k++) {
          f.push_back({OVERLAY_DRIFT_HEAD + OVERLAY_GLIDE_EACH * (int) k + 1, v[k]});
          each.push_back({d.first + (int) k, 0});
        }
        return send(K_OVERLAY_DRIFT, msg::drift(each), f);
      }
    if (w == "flip" || w == "repeat") {
      static const char *const AXES[] = {"none", "x", "y", "both"};  // a bit each
      int k;
      if (!choice(AXES, k))
        return false;
      if (w == "flip")
        return send(K_OVERLAY_VIEW, msg::flip(k & 1, k & 2), {});
      unsigned flags = (k & 1 ? OVERLAY_VIEW_REPEAT_X : 0) | (k & 2 ? OVERLAY_VIEW_REPEAT_Y : 0) |
                       (take("mirror") ? OVERLAY_VIEW_MIRROR : 0);
      return send(K_OVERLAY_VIEW, msg::view(OVERLAY_VIEW_REPEAT_X | OVERLAY_VIEW_REPEAT_Y | OVERLAY_VIEW_MIRROR, flags),
                  {});
    }
    if (w == "pivot") {
      std::vector<Val> v;
      if (!vals(v, 2, {10, 10}))  // to a tenth of a percent
        return false;
      for (const Val &k : v)
        if (k.num && (*k.num < 0 || *k.num > OVERLAY_PIVOT_MAX))
          return fail("pivot takes 0 to 100 percent of its box");
      return glide({{OVERLAY_PIVOT_X, v[0]}, {OVERLAY_PIVOT_Y, v[1]}}, just(0), 0);
    }
    if (w == "depth") {
      std::vector<Val> v;
      return vals(v, 1) && glide({{OVERLAY_DEPTH, v[0]}}, just(0), 0);
    }
    if (w == "blend") {
      int k;
      return choice(BLENDS, k) &&
             send(K_OVERLAY_VIEW, msg::view(OVERLAY_VIEW_BLEND, (unsigned) k * OVERLAY_BLEND_ADD), {});
    }
    if (w == "smooth") {
      static const char *const ONOFF[] = {"off", "on"};
      int k = 1;
      if (peek().kind == 'w' && !choice(ONOFF, k))
        return false;
      return send(K_OVERLAY_VIEW, msg::view(OVERLAY_VIEW_SMOOTH, k ? OVERLAY_VIEW_SMOOTH : 0), {});
    }
    if (w == "shape")
      return shape();
    if (w == "soft")
      return soft();
    if (w == "tint") {
      int32_t rgb;
      if (take("none"))
        return send(K_OVERLAY_TINT, msg::tint({}), {});
      if (!number(rgb, "tint"))
        return false;
      if (rgb < 0 || rgb > 0xFFFFFF)
        return fail("a tint is a colour like 0xFF77BB");
      return send(K_OVERLAY_TINT, msg::tint((uint32_t) rgb), {});
    }
    if (w == "area") {
      std::vector<Val> v;
      int ex = 0, ey = 0;
      if (!vals(v, 4) || (peek().kind == 'w' && !choice(EDGES, ex)))
        return false;
      if (!rising(v[0], v[2], true) || !rising(v[1], v[3], true))
        return fail("right and bottom have to be past left and top");
      ey = ex;
      if (peek().kind == 'w' && !choice(EDGES, ey))
        return false;
      return send(K_OVERLAY_AREA, msg::area(0, 0, 0, 0, ex, ey),
                  {{OVERLAY_AREA_X0, v[0]}, {OVERLAY_AREA_Y0, v[1]}, {OVERLAY_AREA_X1, v[2]}, {OVERLAY_AREA_Y1, v[3]}});
    }
    if (w == "wander") {
      std::vector<Val> v;
      if (!vals(v, 8, {10, 10}))
        return false;
      if (!rising(v[0], v[1]) || !rising(v[4], v[5]) || !rising(v[6], v[7]))
        return fail("each max has to be at least its min");
      for (int k : {0, 1, 4, 5, 6, 7})
        if (v[k].num && *v[k].num < 0)
          return fail("its speeds and times can't be below 0");
      std::vector<Field> f;
      static const int AT[] = {OVERLAY_WANDER_SPEED,  OVERLAY_WANDER_SPEED + 2, OVERLAY_WANDER_HEADING,
                               OVERLAY_WANDER_SPREAD, OVERLAY_WANDER_LEG,       OVERLAY_WANDER_LEG + 2,
                               OVERLAY_WANDER_REST,   OVERLAY_WANDER_REST + 2};
      for (int k = 0; k < 8; k++)
        f.push_back({AT[k], v[k]});
      return send(K_OVERLAY_WANDER, msg::wander(0, 0, 0, 0, 0, 0, 0, 0), f);
    }
    if (w == "face") {
      std::vector<Val> v;
      Val tilt, rate;
      if (!vals(v, 1))
        return false;
      bool mirror = take("mirror"), level = take("level");
      if (!optional_val("tilt", tilt, 0) || !optional_val("rate", rate, 0))
        return false;
      return send(K_OVERLAY_FACE, msg::face(0, mirror, 0, 0, level),
                  {{OVERLAY_FACE_FACING, v[0]}, {OVERLAY_FACE_TILT, tilt}, {OVERLAY_FACE_RATE, rate}});
    }
    if (w == "wave")
      return wave();
    if (w == "map") {
      int ch;
      std::vector<Node> v(4);
      if (!choice(CHANNELS, ch))
        return false;
      for (size_t k = 0; k < 4; k++)
        if ((k && !need(",")) || !expr(v[k]))
          return false;
      if (ch < OVERLAY_PAN_X)
        return fail("x and y don't follow where it is");
      // across and down its area, the two added, or a share's multiplied
      Node max = Node::num(OVERLAY_AT_MAX);
      Node x = Node::call("lerp", {v[0], v[1], Node::read(OVERLAY_GET_AT_X), max}),
           y = Node::call("lerp", {v[2], v[3], Node::read(OVERLAY_GET_AT_Y), max});
      Node both = share(ch) ? Node::of(OVERLAY_OP_SCALE, {x, y, Node::num(100)}) : Node::of(OVERLAY_OP_ADD, {x, y});
      return bind((unsigned) (share(ch) ? OVERLAY_BIND_MUL : OVERLAY_BIND_ADD) | (unsigned) ch, &both, fine_of(ch));
    }
    if (w == "mark") {
      int32_t k;
      if (!number(k, "mark"))
        return false;
      if (k < 0 || k > 255)
        return fail("marks are 0 to 255");
      b.push_back(OVERLAY_DO_MARK);
      b.push_back((uint8_t) k);
      return true;
    }
    if (w == "end") {
      b.push_back(OVERLAY_DO_END);
      if (take("fade")) {
        Node e;
        if (!expr(e) || !put(e))
          return false;
      } else {  // four bytes for the fade, whatever it is
        b.push_back(OVERLAY_V32);
        p.patches.push_back({(uint16_t) b.size(), Program::FADE, "", 0});
        b.insert(b.end(), 4, 0);
      }
      p.patches.push_back({(uint16_t) b.size(), Program::TAG, "", 0});
      b.push_back(0);
      return true;
    }
    return fail("what's '" + w + "'?");
  }

  // How long into a wave's rise it's at where of the way from low to high, slow as it is
  static double rise_to(double where, double rise, int slow) {
    double lo = 0, hi = 1;
    for (int k = 0; k < 30; k++) {  // its easing is the panel's eased(), which only goes up
      double f = (lo + hi) / 2, e = slow == 1   ? f * f
                                    : slow == 2 ? 1 - (1 - f) * (1 - f)
                                    : slow      ? f * f * (3 - 2 * f)
                                                : f;
      (e < where ? lo : hi) = f;
    }
    return std::round(lo * rise);
  }

  // wave: a channel between low and high on its own, bound to the panel's WAVE of the time since it's said. Without
  // jitter it starts where the channel is, if that's between the two.
  bool wave() {
    int ch, slow = 0;
    int32_t jitter = 0;
    std::vector<Node> v(4), hold = {Node::num(0), Node::num(0)};
    if (!choice(CHANNELS, ch))
      return false;
    for (size_t k = 0; k < 4; k++)
      if ((k && !need(",")) || !expr(v[k]))
        return false;
    if (take("hold") && (!expr(hold[0]) || !need(",") || !expr(hold[1])))
      return false;
    if (take("jitter") && !number(jitter, "jitter"))
      return false;
    auto lo = fold(v[0]), hi = fold(v[1]), rise = fold(v[2]);
    if (lo && hi && *hi < *lo)
      return fail("high has to be at least low");
    if (jitter < 0 || jitter > 100)
      return fail("jitter is 0 to 100");
    bool random = take("random");
    if (take("slow") && !choice(SLOWS, slow))
      return false;
    Node at = Node::read(OVERLAY_GET_MS);
    if (!tells(K_OVERLAY_BIND, {}) && !target) {  // from now, not from when it was placed
      if (since < 0 || since_at != b.size()) {
        if (!reg("wave " + std::to_string(own), true, since))
          return false;
        b.push_back(OVERLAY_DO_SET);
        b.push_back((uint8_t) since);
        if (!put(at))
          return false;
      }
      Node from;
      from.kind = Node::REG;
      from.reg = since;
      at = Node::of(OVERLAY_OP_SUB, {std::move(at), std::move(from)});
    }
    double still = no_change(ch);
    if (!jitter && !random && lo && hi && rise && *lo < still && still < *hi && *rise > 0)
      at = Node::of(OVERLAY_OP_ADD, {std::move(at), Node::num(rise_to((still - *lo) / (*hi - *lo), *rise, slow))});
    Node n = Node::call("wave", {std::move(at), v[0], v[1], v[2], v[3], hold[0], hold[1], Node::num(jitter),
                                 Node::num(slow | (random ? OVERLAY_WAVE_RANDOM : 0))});
    bool ok = bind((unsigned) (share(ch) ? OVERLAY_BIND_MUL : OVERLAY_BIND_ADD) | (unsigned) ch, &n, fine_of(ch));
    since_at = b.size();
    return ok;
  }

  // shape none, or shape and runs of three numbers: each run's width, top and the row past its bottom
  bool shape() {
    std::vector<int32_t> runs;
    if (!take("none"))
      do {
        int32_t v;
        if (!number(v, "shape"))
          return false;
        runs.push_back(v);
      } while (take(","));
    if (runs.size() % 3 || runs.size() > 3 * OVERLAY_SHAPE_RUNS)
      return fail("a shape is runs of width, top and bottom, " + std::to_string(OVERLAY_SHAPE_RUNS) + " at most");
    for (size_t k = 0; k < runs.size(); k += 3)
      if (runs[k] < 0 || runs[k + 2] < runs[k + 1] || runs[k] != clamp16(runs[k]) ||
          runs[k + 1] != clamp16(runs[k + 1]) || runs[k + 2] != clamp16(runs[k + 2]))
        return fail("a run's width is 0 or more, its bottom under its top");
    b.push_back(OVERLAY_DO_SHAPE);
    b.push_back((uint8_t) runs.size());
    for (int32_t v : runs) {
      b.push_back((uint8_t) v);
      b.push_back((uint8_t) ((uint16_t) v >> 8));
    }
    return true;
  }

  bool soft() {
    int32_t v[4] = {};
    for (int k = 0; k < 4 && !take("none"); k++)
      if ((k && !need(",")) || !number(v[k], "soft"))
        return false;
      else if (v[k] < 0 || v[k] > OVERLAY_BOX_MAX)
        return fail("soft takes 0 to " + std::to_string(OVERLAY_BOX_MAX) + " px for each edge");
    b.push_back(OVERLAY_DO_SOFT);
    for (int32_t k : v) {
      b.push_back((uint8_t) k);
      b.push_back((uint8_t) (k >> 8));
    }
    return true;
  }

  bool assign() {
    std::string w = t[i].s;
    int r;
    Node e;
    i += 2;
    if (!reg(w, true, r) || !expr(e))
      return false;
    b.push_back(OVERLAY_DO_SET);
    b.push_back((uint8_t) r);
    return put(e);
  }

  bool wait() {
    Node a;
    if (take("rounds")) {
      b.push_back(OVERLAY_DO_ROUNDS);
      return expr(a) && put(a);
    }
    if (take("until")) {
      b.push_back(OVERLAY_DO_UNTIL);
      return expr(a) && put(a);
    }
    if (!expr(a))
      return false;
    if (take("to")) {
      Node z;
      if (!expr(z))
        return false;
      a = Node::call("rand", {std::move(a), std::move(z)});
    }
    b.push_back(OVERLAY_DO_WAIT);
    return put(a);
  }

  bool clip() {
    Node first, last;
    Clip size = {};
    if (peek().kind == 'w' && clips.count(peek().s)) {
      size = clips.at(t[i++].s);
      first = Node::num(size.first);
      last = Node::num(size.last);
    } else {
      if (!expr(first))
        return false;
      last = first;
      if (take("to") && !expr(last))
        return false;
    }
    bool next = take("at") && need("end");
    if (!err.empty())
      return false;
    b.push_back(OVERLAY_DO_CLIP);
    if (!put(first) || !put(last))
      return false;
    b.push_back(next ? OVERLAY_CLIP_NEXT : OVERLAY_CLIP_NOW);
    for (int v : {size.w, size.h}) {
      b.push_back((uint8_t) v);
      b.push_back((uint8_t) (v >> 8));
    }
    p.frames = true;
    return true;
  }

  bool run(const std::string &text) {
    if (!lex(text))
      return false;
    while (peek().kind && err.empty())
      statement();
    if (!err.empty())
      return false;
    for (auto &j : jumps) {
      int to = (int) b.size();
      if (!j.label.empty()) {
        auto l = labels.find(j.label);
        if (l == labels.end()) {
          err = "line " + std::to_string(j.line) + ": no '" + j.label + ":'";
          return false;
        }
        to = l->second;
      }
      b[j.at] = (uint8_t) to;
      b[j.at + 1] = (uint8_t) (to >> 8);
    }
    if (b.size() > OVERLAY_SCRIPT_MAX) {
      err = "it's " + std::to_string(b.size()) + " bytes, the panel takes " + std::to_string(OVERLAY_SCRIPT_MAX);
      return false;
    }
    p.regs = own;
    return true;
  }
};

}  // namespace text

// The script in text made into steps for the panel, with an animation's clips and numbers by name. The reason in
// error if it can't be.
inline Program compile(const std::string &text, std::string &error, const Clips &clips = {},
                       const Consts &consts = {}) {
  if (!text::stack_has(text::STACK)) {
    error = "no room on the stack to read it";
    return {};
  }
  text::Compiler c(clips, consts);
  if (!c.run(text)) {
    error = c.err;
    return {};
  }
  error.clear();
  return std::move(c.p);
}

// "sail 0-3, tack 4-8" as clips, the reason in error for one that isn't
inline Clips parse_clips(const std::string &s, std::string &error) {
  Clips out;
  error.clear();
  size_t k = 0;
  while (k < s.size()) {
    size_t e = s.find(',', k);
    std::string part = s.substr(k, e == std::string::npos ? std::string::npos : e - k);
    k = e == std::string::npos ? s.size() : e + 1;
    char name[32];
    int first, last = -1, used = 0;
    if (part.find_first_not_of(" \t\n") == std::string::npos)
      continue;
    if (sscanf(part.c_str(), " %31[A-Za-z0-9_] %d%n - %d%n", name, &first, &used, &last, &used) < 2 ||
        part.find_first_not_of(" \t\n", used) != std::string::npos || first < 0 || (last >= 0 && last < first)) {
      error = "'" + part + "' isn't a name and frames like sail 0-3";
      return {};
    }
    out[name] = {first, last < 0 ? first : last};
  }
  return out;
}

}  // namespace flexit::panel::overlay
