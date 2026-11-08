// The messages that move a copy on the panel, and the scripts copies run there, see K_OVERLAY_SCRIPT. A script is
// text like this, made into the panel's steps by compile():
//   clip sail                    # the frames of a clip of the animation's, or clip 4 to 8, or clip 3
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
#include "plink.h"

namespace flexit::panel::overlay {

inline int16_t clamp16(int v) { return (int16_t) std::clamp(v, -32768, 32767); }
inline uint16_t clamp_u16(int v) { return (uint16_t) std::clamp(v, 0, 65535); }
inline int tenths(float v) { return clamp16((int) lroundf(v * 10)); }
inline int hundredths(float v) { return clamp16((int) lroundf(v * 100)); }

inline constexpr const char *CHANNELS[] = {"x", "y", "turn", "size", "opacity", "speed", "light"};
inline constexpr const char *EASES[] = {"none", "in", "out", "both"};    // OVERLAY_EASE_* bits
inline constexpr const char *SLOWS[] = {"none", "low", "high", "both"};  // OVERLAY_WAVE_SLOW_* bits
inline constexpr const char *EDGES[] = {"screen", "cross", "bounce"};    // OVERLAY_EDGE_*
static_assert(sizeof CHANNELS / sizeof *CHANNELS == OVERLAY_CHANNELS, "a name for each channel");

// ---- the messages, laid out as in plink_keys.h, the place left for whoever sends them

namespace msg {

inline void put16(std::vector<uint8_t> &m, int at, int v) { plink_put16(&m[at], (uint16_t) v); }

// The channels from ch on to v over ms, in the channels' units
inline std::vector<uint8_t> glide(int ch, const std::vector<int> &v, int ms, int ease) {
  std::vector<uint8_t> m(OVERLAY_GLIDE_HEAD + 2 * v.size());
  m[OVERLAY_GLIDE_CHANNELS] = (uint8_t) (((1u << v.size()) - 1) << ch);
  put16(m, OVERLAY_GLIDE_MS, clamp_u16(ms));
  m[OVERLAY_GLIDE_EASE] = (uint8_t) ease;
  for (size_t i = 0; i < v.size(); i++)
    put16(m, OVERLAY_GLIDE_HEAD + 2 * (int) i, clamp16(v[i]));
  return m;
}

// The channels from ch on drifting at v a second, in hundredths of a px on x and y, tenths of a degree on turn
inline std::vector<uint8_t> drift(int ch, const std::vector<int> &v) {
  std::vector<uint8_t> m(OVERLAY_DRIFT_HEAD + 2 * v.size());
  m[OVERLAY_DRIFT_CHANNELS] = (uint8_t) (((1u << v.size()) - 1) << ch);
  for (size_t i = 0; i < v.size(); i++)
    put16(m, OVERLAY_DRIFT_HEAD + 2 * (int) i, clamp16(v[i]));
  return m;
}

inline std::vector<uint8_t> flip(bool x, bool y) {
  std::vector<uint8_t> m(OVERLAY_FLIP_LEN);
  m[OVERLAY_FLIP_FLAGS] = (uint8_t) ((x ? OVERLAY_FLIP_X : 0) | (y ? OVERLAY_FLIP_Y : 0));
  return m;
}

inline std::vector<uint8_t> depth(int d) {
  std::vector<uint8_t> m(OVERLAY_DEPTH_LEN);
  m[OVERLAY_DEPTH_DEPTH] = (uint8_t) std::clamp(d, 0, 255);
  return m;
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

// flags: OVERLAY_WAVE_*
inline std::vector<uint8_t> wave(int ch, int low, int high, int rise, int fall, int hold_high, int hold_low, int jitter,
                                 int flags) {
  std::vector<uint8_t> m(OVERLAY_WAVE_LEN);
  m[OVERLAY_WAVE_CHANNEL] = (uint8_t) ch;
  put16(m, OVERLAY_WAVE_LO, clamp16(low));
  put16(m, OVERLAY_WAVE_HI, clamp16(high));
  put16(m, OVERLAY_WAVE_MS, clamp_u16(rise));
  put16(m, OVERLAY_WAVE_MS + 2, clamp_u16(fall));
  put16(m, OVERLAY_WAVE_HOLD, clamp_u16(hold_high));
  put16(m, OVERLAY_WAVE_HOLD + 2, clamp_u16(hold_low));
  m[OVERLAY_WAVE_JITTER] = (uint8_t) std::clamp(jitter, 0, 100);
  m[OVERLAY_WAVE_FLAGS] = (uint8_t) flags;
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

inline std::vector<uint8_t> map(int ch, int left, int right, int top, int bottom) {
  std::vector<uint8_t> m(OVERLAY_MAP_LEN);
  m[OVERLAY_MAP_CHANNEL] = (uint8_t) ch;
  int at = OVERLAY_MAP_VALUES;
  for (int v : {left, right, top, bottom}) {
    put16(m, at, clamp16(v));
    at += 2;
  }
  return m;
}

}  // namespace msg

// ---- scripts

// A script made into the panel's steps, with what's filled in as it's sent
struct Program {
  enum { PLACE, TAG, FADE };  // the place of another copy, its END's tag, a fade as long as its own (2 bytes)
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
  bool frames = false;  // it plays frames, so it needs a frame of its own
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

// What a GET reads, as the script calls it, OVERLAY_GET_*
inline constexpr const char *GETS[] = {"x",       "y",     "w",     "h",      "turn", "size",
                                       "opacity", "light", "frame", "rounds", "here", "ms"};
static_assert(sizeof GETS / sizeof *GETS == OVERLAY_GETS, "a name for each");
inline constexpr const char *FUNCS[] = {"rand", "dist", "angle", "sin", "cos", "abs", "min", "max", "pick"};
inline constexpr const char *WORDS[] = {"goto", "if",   "wait", "until",  "to",     "clip",   "at",     "end",  "rate",
                                        "move", "over", "ease", "glide",  "drift",  "spin",   "flip",   "self", "area",
                                        "mark", "fade", "stop", "depth",  "shared", "wander", "face",   "wave", "map",
                                        "hold", "slow", "tilt", "mirror", "level",  "random", "jitter", "tint"};

template<size_t N> int index(const char *const (&names)[N], const std::string &s) {
  for (size_t i = 0; i < N; i++)
    if (s == names[i])
      return (int) i;
  return -1;
}

// A part of an expression
struct Node {
  enum Kind { NUM, REG, GET, CALL, OP, NEG } kind = NUM;
  double v = 0;  // NUM's
  int reg = 0;   // REG's
  int fn = 0;    // GET's OVERLAY_GET_*, CALL's in FUNCS
  char op = 0;   // OP's + - * / %
  std::vector<Node> args;
  std::vector<std::string> places;  // GET's, DIST's and ANGLE's copies, "" for its own
  static Node of(Kind k, double v = 0) {
    Node n;
    n.kind = k;
    n.v = v;
    return n;
  }
};

// A value as a step takes it: a register, or a number
struct Val {
  bool reg;
  int32_t v;
};

struct Compiler {
  std::vector<Tok> t;
  size_t i = 0;
  std::string err;
  const Clips &clips;
  const Consts &consts;
  std::map<std::string, int> vars;                    // registers by name, 0..7 its own, from 0 up, and the shared ones
  int own = 0, shared = OVERLAY_OWN_REGS, temps = 0;  // temps from 7 down
  Program p;
  std::vector<uint8_t> &b = p.bytes;
  std::map<std::string, int> labels;
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
        std::string two = s.substr(k, 2);
        bool pair = two == "<=" || two == ">=" || two == "==" || two == "!=";
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
    return index(GETS, w) >= 0 || index(FUNCS, w) >= 0 || index(WORDS, w) >= 0;
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

  bool expr(Node &n);
  bool atom(Node &n) {
    const Tok &k = peek();
    if (k.kind == 'n') {
      n = Node::of(Node::NUM, k.v);
      i++;
      return true;
    }
    if (take("(")) {
      return expr(n) && need(")");
    }
    if (take("-")) {
      Node a;
      if (!atom(a))
        return false;
      n = Node::of(Node::NEG);
      n.args.push_back(std::move(a));
      return true;
    }
    std::string w;
    if (peek().kind != 'w')
      return fail("a number or a name expected");
    word(w);
    if (auto c = consts.find(w); c != consts.end()) {
      n = Node::of(Node::NUM, c->second);
      return true;
    }
    if (auto v = vars.find(w); v != vars.end()) {
      n = Node::of(Node::REG);
      n.reg = v->second;
      return true;
    }
    if (int g = index(GETS, w); g >= 0) {
      n = Node::of(Node::GET);
      n.fn = g;
      n.places.emplace_back();
      return !take("(") || (place(n.places[0]) && need(")"));
    }
    int f = index(FUNCS, w);
    if (f < 0)
      return fail("what's '" + w + "'?");
    n = Node::of(Node::CALL);
    n.fn = f;
    if (!need("("))
      return false;
    bool places = w == "dist" || w == "angle", pick = w == "pick";
    for (int k = 0; pick ? !k || take(",") : k < (w == "abs" ? 1 : 2); k++) {
      if (k && !pick && !need(","))
        return false;
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
    return need(")");
  }
  bool term(Node &n) {
    if (!atom(n))
      return false;
    while (is("*") || is("/") || is("%")) {
      Node o = Node::of(Node::OP);
      o.op = t[i++].s[0];
      o.args.push_back(std::move(n));
      o.args.emplace_back();
      if (!atom(o.args.back()))
        return false;
      n = std::move(o);
    }
    return true;
  }

  // ---- working it out

  static std::optional<double> fold(const Node &n) {
    switch (n.kind) {
      case Node::NUM:
        return n.v;
      case Node::NEG:
        if (auto a = fold(n.args[0]))
          return -*a;
        return {};
      case Node::OP: {
        auto a = fold(n.args[0]), c = fold(n.args[1]);
        if (!a || !c)
          return {};
        switch (n.op) {
          case '+':
            return *a + *c;
          case '-':
            return *a - *c;
          case '*':
            return *a * *c;
          case '/':
            return *c ? std::trunc(*a / *c) : 0;
          default:
            return *c ? std::fmod(std::trunc(*a), std::trunc(*c)) : 0;
        }
      }
      default:
        return {};
    }
  }
  static bool uses(const Node &n, int reg) {
    if (n.kind == Node::REG && n.reg == reg)
      return true;
    for (auto &a : n.args)
      if (uses(a, reg))
        return true;
    return false;
  }

  int temp() {
    int r = OVERLAY_OWN_REGS - 1 - temps++;
    if (r < own)
      fail("too much to work out, the panel has " + std::to_string(OVERLAY_OWN_REGS) + " registers for it all");
    return std::max(r, 0);
  }

  void num(int32_t v) {
    if (v >= -32768 && v <= 32767) {
      b.push_back(OVERLAY_V16);
      b.push_back((uint8_t) v);
      b.push_back((uint8_t) ((uint16_t) v >> 8));
    } else {
      b.push_back(OVERLAY_V32);
      for (int k = 0; k < 4; k++)
        b.push_back((uint8_t) ((uint32_t) v >> 8 * k));
    }
  }
  void put(Val v) {
    if (v.reg)
      b.push_back((uint8_t) (OVERLAY_VREG + v.v));
    else
      num(v.v);
  }
  bool whole(double v, int32_t &out) {
    if (std::fabs(v) > 2147483647.0)
      return fail("a number too big for the panel");
    out = (int32_t) std::lround(v);
    return std::fabs(v - out) < 1e-9 || fail("only whole numbers there, the panel has no fractions");
  }
  void place_byte(const std::string &name) {
    if (name.empty()) {
      b.push_back(OVERLAY_SELF);
      return;
    }
    size_t dot = name.rfind('.');
    int copy = 0;
    std::string anim = name;
    if (dot != std::string::npos && dot + 1 < name.size() && isdigit((unsigned char) name[dot + 1])) {
      copy = atoi(name.c_str() + dot + 1);
      anim = name.substr(0, dot);
    }
    p.patches.push_back({(uint16_t) b.size(), Program::PLACE, anim, copy});
    b.push_back(0);
  }

  // n into register r
  bool gen(const Node &n, int r) {
    if (auto c = fold(n)) {
      int32_t v;
      if (!whole(*c, v))
        return false;
      step(OVERLAY_DO_SET, r);
      num(v);
      return true;
    }
    switch (n.kind) {
      case Node::REG:
        if (n.reg != r) {
          step(OVERLAY_DO_SET, r);
          b.push_back((uint8_t) (OVERLAY_VREG + n.reg));
        }
        return true;
      case Node::NEG:
        if (!gen(n.args[0], r))
          return false;
        step(OVERLAY_DO_MUL, r);
        num(-1);
        return true;
      case Node::OP: {
        static const char OPS[] = "+-*/%";
        static const uint8_t DO[] = {OVERLAY_DO_ADD, OVERLAY_DO_SUB, OVERLAY_DO_MUL, OVERLAY_DO_DIV, OVERLAY_DO_MOD};
        if (!gen(n.args[0], r))
          return false;
        return op(DO[strchr(OPS, n.op) - OPS], r, n.args[1]);
      }
      case Node::GET:
        step(OVERLAY_DO_GET, r);
        b.push_back((uint8_t) n.fn);
        place_byte(n.places[0]);
        return true;
      default:
        return call(n, r);
    }
  }
  bool op(uint8_t o, int r, const Node &rhs) {
    int mark = temps;
    Val v;
    if (!val(rhs, v))
      return false;
    step(o, r);
    put(v);
    temps = mark;
    return true;
  }
  bool call(const Node &n, int r) {
    const std::string f = FUNCS[n.fn];
    if (f == "pick")
      return pick(n, r);
    if (f == "dist" || f == "angle") {
      step(f == "dist" ? OVERLAY_DO_DIST : OVERLAY_DO_ANGLE, r);
      place_byte(n.places[0]);
      place_byte(n.places[1]);
      return true;
    }
    if (f == "min" || f == "max")
      return gen(n.args[0], r) && op(f == "min" ? OVERLAY_DO_MIN : OVERLAY_DO_MAX, r, n.args[1]);
    int mark = temps;
    std::vector<Val> v(n.args.size());
    for (size_t k = 0; k < v.size(); k++)
      if (!val(n.args[k], v[k]))
        return false;
    step(f == "rand" ? OVERLAY_DO_RAND : f == "sin" ? OVERLAY_DO_SIN : f == "cos" ? OVERLAY_DO_COS : OVERLAY_DO_ABS, r);
    for (Val x : v)
      put(x);
    temps = mark;
    return true;
  }
  // pick(k, a, b, ...): number k of the numbers after it, from 0
  bool pick(const Node &n, int r) {
    if (n.args.size() < 2 || n.args.size() > 256)
      return fail("pick takes 1 to 255 numbers after the first");
    std::vector<int32_t> list;
    for (size_t k = 1; k < n.args.size(); k++) {
      auto c = fold(n.args[k]);
      int32_t v;
      if (!c)
        return fail("pick takes numbers after the first");
      if (!whole(*c, v))
        return false;
      if (v != clamp16(v))
        return fail("pick's numbers are -32768 to 32767");
      list.push_back(v);
    }
    int mark = temps;
    Val k;
    if (!val(n.args[0], k))
      return false;
    step(OVERLAY_DO_PICK, r);
    put(k);
    b.push_back((uint8_t) list.size());
    for (int32_t v : list) {
      b.push_back((uint8_t) v);
      b.push_back((uint8_t) ((uint16_t) v >> 8));
    }
    temps = mark;
    return true;
  }
  // n as a value: a number or a register as it is, else worked out into a temp
  bool val(const Node &n, Val &v) {
    if (auto c = fold(n)) {
      v.reg = false;
      return whole(*c, v.v);
    }
    if (n.kind == Node::REG) {
      v = {true, n.reg};
      return true;
    }
    v = {true, temp()};
    return gen(n, v.v);
  }
  // n times scale, the number as it is if it's one: speeds in tenths or hundredths
  bool scaled(const Node &n, int scale, Val &v) {
    if (auto c = fold(n)) {
      v.reg = false;
      return whole(std::round(*c * scale), v.v);
    }
    Node m = Node::of(Node::OP);
    m.op = '*';
    m.args.push_back(n);
    m.args.push_back(Node::of(Node::NUM, (double) scale));
    return val(m, v);
  }
  void step(uint8_t o, int r) {
    b.push_back(o);
    b.push_back((uint8_t) r);
  }

  // ---- statements

  bool cond(Val &l, uint8_t &c, Val &r) {
    static const char *const CMP[] = {"<", "<=", "==", "!=", ">=", ">"};  // OVERLAY_IF_*
    Node a, z;
    if (!expr(a))
      return false;
    int k = index(CMP, peek().s);
    if (peek().kind != 'o' || k < 0)
      return fail("<, <=, ==, !=, >= or > expected");
    i++;
    c = (uint8_t) k;
    return expr(z) && val(a, l) && val(z, r);
  }
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

  // A SEND of key's message m, the fields at the given bytes from v where they aren't numbers. The message has the
  // numbers in already.
  struct Field {
    int at;
    Val v;
  };
  bool send(uint16_t key, std::vector<uint8_t> m, const std::vector<Field> &fields) {
    int regs = 0;
    for (auto &f : fields) {
      if (f.v.reg)
        regs++;
      else
        msg::put16(m, f.at, std::clamp<int32_t>(f.v.v, -32768, 65535));  // as the panel takes a register's
    }
    if (tells(key, m, fields)) {
      p.told.push_back({key, std::move(m)});
      return true;
    }
    b.push_back(OVERLAY_DO_SEND);
    b.push_back((uint8_t) (key - K_OVERLAY_FRAMES));
    b.push_back((uint8_t) m.size());
    b.insert(b.end(), m.begin(), m.end());
    b.push_back((uint8_t) regs);
    for (auto &f : fields)
      if (f.v.reg) {
        b.push_back((uint8_t) f.at);
        put(f.v);
      }
    return true;
  }
  // Whether m with fields is told before the script, rather than sent as it runs: all of it known, before its first
  // step and label
  bool tells(uint16_t key, const std::vector<uint8_t> &m, const std::vector<Field> &fields) const {
    for (auto &f : fields)
      if (f.v.reg)
        return false;
    return b.empty() && labels.empty() && !retold(key, m);
  }
  // Whether m would take the place of one told already, on a channel it has. Then it and the rest go to the panel
  // as steps, so each comes in its turn.
  bool retold(uint16_t key, const std::vector<uint8_t> &m) const {
    bool bits = key == K_OVERLAY_GLIDE || key == K_OVERLAY_DRIFT,
         ch = bits || key == K_OVERLAY_WAVE || key == K_OVERLAY_MAP;
    for (auto &t : p.told)
      if (t.key == key && (!ch || (bits ? (t.m[OVERLAY_GLIDE_CHANNELS] & m[OVERLAY_GLIDE_CHANNELS]) != 0
                                        : t.m[OVERLAY_GLIDE_CHANNELS] == m[OVERLAY_GLIDE_CHANNELS])))
        return true;
    return false;
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
  bool optional_val(const char *w, Val &v, int dflt) {
    v = {false, dflt};
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
    int mark = temps;
    bool ok = one();
    temps = mark;
    if (ok && peek().kind != ';')
      return fail("'" + (peek().kind == 'n' ? std::to_string(peek().v) : peek().s) + "' after the end of it");
    return ok;
  }

  bool one() {
    std::string w;
    if (peek().kind == 'w' && is(":", 1)) {
      w = t[i].s;
      i += 2;
      if (labels.count(w))
        return fail("'" + w + "' twice");
      labels[w] = (int) b.size();
      return peek().kind == ';' || one();
    }
    if (peek().kind == 'w' && is("=", 1) && !reserved(peek().s))
      return assign();
    if (!word(w))
      return false;
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
      Val l, r;
      uint8_t c;
      std::string to;
      if (!cond(l, c, r) || !need("goto") || !word(to))
        return false;
      b.push_back(OVERLAY_DO_IF);
      put(l);
      b.push_back(c);
      put(r);
      jump(to);
      return true;
    }
    if (w == "wait")
      return wait();
    if (w == "clip")
      return clip();
    if (w == "frame" || w == "rate") {
      Node e;
      Val v;
      if (!expr(e) || !val(e, v))
        return false;
      b.push_back(w == "frame" ? OVERLAY_DO_FRAME : OVERLAY_DO_RATE);
      put(v);
      p.frames = true;
      return true;
    }
    if (w == "move" || w == "glide") {
      int ch = OVERLAY_X, e = 0;
      std::vector<Val> v;
      Val ms;
      if (w == "glide" && !choice(CHANNELS, ch))
        return false;
      if (!vals(v, w == "move" ? 2 : 1) || !optional_val("over", ms, 0))
        return false;
      if (take("ease") && !choice(EASES, e))
        return false;
      std::vector<Field> f = {{OVERLAY_GLIDE_MS, ms}};
      for (size_t k = 0; k < v.size(); k++)
        f.push_back({OVERLAY_GLIDE_HEAD + 2 * (int) k, v[k]});
      std::vector<uint8_t> m = msg::glide(ch, std::vector<int>(v.size()), 0, e);
      if (w == "glide" || tells(K_OVERLAY_GLIDE, m, f))
        return send(K_OVERLAY_GLIDE, m, f);
      b.push_back(OVERLAY_DO_MOVE);  // a step of its own, smaller than the message
      put(v[0]);
      put(v[1]);
      put(ms);
      b.push_back((uint8_t) e);
      return true;
    }
    if (w == "drift" || w == "spin") {
      std::vector<Val> v;
      bool xy = w == "drift";
      if (!vals(v, xy ? 2 : 1, xy ? std::vector<int>{100, 100} : std::vector<int>{10}))  // px to a hundredth
        return false;
      std::vector<Field> f;
      for (size_t k = 0; k < v.size(); k++)
        f.push_back({OVERLAY_DRIFT_HEAD + 2 * (int) k, v[k]});
      return send(K_OVERLAY_DRIFT, msg::drift(xy ? OVERLAY_X : OVERLAY_TURN, std::vector<int>(v.size())), f);
    }
    if (w == "flip") {
      static const char *const FLIPS[] = {"none", "x", "y", "both"};
      int k;
      return choice(FLIPS, k) && send(K_OVERLAY_FLIP, msg::flip(k & 1, k & 2), {});
    }
    if (w == "depth") {
      int32_t d;
      return number(d, "depth") && send(K_OVERLAY_DEPTH, msg::depth(d), {});
    }
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
    if (w == "wave") {
      int ch, slow = 0;
      int32_t jitter = 0;
      std::vector<Val> v, hold = {{false, 0}, {false, 0}};
      if (!choice(CHANNELS, ch) || !vals(v, 4))
        return false;
      if (take("hold") && !vals(hold, 2))
        return false;
      if (take("jitter") && !number(jitter, "jitter"))
        return false;
      bool random = take("random");
      if (take("slow") && !choice(SLOWS, slow))
        return false;
      return send(K_OVERLAY_WAVE, msg::wave(ch, 0, 0, 0, 0, 0, 0, jitter, slow | (random ? OVERLAY_WAVE_RANDOM : 0)),
                  {{OVERLAY_WAVE_LO, v[0]},
                   {OVERLAY_WAVE_HI, v[1]},
                   {OVERLAY_WAVE_MS, v[2]},
                   {OVERLAY_WAVE_MS + 2, v[3]},
                   {OVERLAY_WAVE_HOLD, hold[0]},
                   {OVERLAY_WAVE_HOLD + 2, hold[1]}});
    }
    if (w == "map") {
      int ch;
      std::vector<Val> v;
      if (!choice(CHANNELS, ch) || !vals(v, 4))
        return false;
      if (ch < OVERLAY_TURN)
        return fail("x and y don't follow where it is");
      return send(K_OVERLAY_MAP, msg::map(ch, 0, 0, 0, 0),
                  {{OVERLAY_MAP_VALUES, v[0]},
                   {OVERLAY_MAP_VALUES + 2, v[1]},
                   {OVERLAY_MAP_VALUES + 4, v[2]},
                   {OVERLAY_MAP_VALUES + 6, v[3]}});
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
        Val v;
        if (!expr(e) || !val(e, v))
          return false;
        put(v);
      } else {
        p.patches.push_back({(uint16_t) (b.size() + 1), Program::FADE, "", 0});
        num(0x7FFF);  // two bytes for the fade, whatever it is
      }
      p.patches.push_back({(uint16_t) b.size(), Program::TAG, "", 0});
      b.push_back(0);
      return true;
    }
    return fail("what's '" + w + "'?");
  }

  bool assign() {
    std::string w = t[i].s;
    i += 2;
    if (consts.count(w))
      return fail("'" + w + "' is a number");
    auto v = vars.find(w);
    if (v == vars.end()) {
      if (own + temps >= OVERLAY_OWN_REGS)
        return fail("only " + std::to_string(OVERLAY_OWN_REGS) + " of its own");
      v = vars.emplace(w, own++).first;
    }
    Node e;
    if (!expr(e))
      return false;
    if (!uses(e, v->second) || e.kind == Node::REG)
      return gen(e, v->second);
    int r = temp();
    if (!gen(e, r))
      return false;
    step(OVERLAY_DO_SET, v->second);
    b.push_back((uint8_t) (OVERLAY_VREG + r));
    return true;
  }

  bool wait() {
    if (take("rounds")) {
      Node e;
      Val v;
      if (!expr(e) || !val(e, v))
        return false;
      b.push_back(OVERLAY_DO_ROUNDS);
      put(v);
      return true;
    }
    if (take("until")) {
      size_t at = b.size();
      Val l, r;
      uint8_t c;
      if (!cond(l, c, r))
        return false;
      b.push_back(OVERLAY_DO_UNTIL);
      put(l);
      b.push_back(c);
      put(r);
      b.push_back((uint8_t) at);  // what it waits on worked out again each time
      b.push_back((uint8_t) (at >> 8));
      return true;
    }
    Node a;
    Val v;
    if (!expr(a))
      return false;
    if (take("to")) {
      Node z;
      Val lo, hi;
      if (!expr(z) || !val(a, lo) || !val(z, hi))
        return false;
      v = {true, temp()};
      step(OVERLAY_DO_RAND, v.v);
      put(lo);
      put(hi);
    } else if (!val(a, v)) {
      return false;
    }
    b.push_back(OVERLAY_DO_WAIT);
    put(v);
    return true;
  }

  bool clip() {
    Val first, last;
    Clip size = {};
    if (peek().kind == 'w' && clips.count(peek().s)) {
      size = clips.at(t[i++].s);
      first = {false, size.first};
      last = {false, size.last};
    } else {
      Node a, z;
      if (!expr(a) || !val(a, first))
        return false;
      last = first;
      if (take("to") && (!expr(z) || !val(z, last)))
        return false;
    }
    bool next = take("at") && need("end");
    if (!err.empty())
      return false;
    b.push_back(OVERLAY_DO_CLIP);
    put(first);
    put(last);
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
    return true;
  }
};

inline bool Compiler::expr(Node &n) {
  if (!term(n))
    return false;
  while (is("+") || is("-")) {
    Node o = Node::of(Node::OP);
    o.op = t[i++].s[0];
    o.args.push_back(std::move(n));
    o.args.emplace_back();
    if (!term(o.args.back()))
      return false;
    n = std::move(o);
  }
  return true;
}

}  // namespace text

// The script in text made into steps for the panel, with an animation's clips and numbers by name. The reason in
// error if it can't be.
inline Program compile(const std::string &text, std::string &error, const Clips &clips = {},
                       const Consts &consts = {}) {
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
