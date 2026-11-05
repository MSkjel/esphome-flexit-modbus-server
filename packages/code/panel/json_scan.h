// A JSON answer read as it streams, a piece at a time, without keeping it: each value goes to value() with its key
// and depth, each object's or array's end to end(). Keys over KEY_MAX and strings over VALUE_MAX are cut. Plain C++
// without ESPHome.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace flexit::panel {

struct JsonScan {
  static constexpr int KEY_MAX = 31, VALUE_MAX = 255, DEPTH_MAX = 15;
  // depth: of what holds it, 1 for the top object's own. key: its key, or its array's. index: in its array, else -1.
  virtual void value(int depth, const char *key, int index, const char *v) = 0;
  virtual void end(int depth, const char *key) = 0;  // depth: of what it held, key: its, or its array's

  // False once something isn't JSON, from then on it's ignored
  bool scan(const char *p, size_t n) {
    for (size_t i = 0; i < n && ok; i++)
      step(p[i]);
    return ok;
  }
  // In value(): the key of the object it's in, or of the one up - 1 further out. "" for one in an array.
  const char *parent(int up = 1) const { return depth > up ? key[depth - up] : ""; }

 private:
  enum { VALUE, STRING, ESCAPE, UNICODE, BARE };
  int state = VALUE, depth = 0, hex = 0, unicode = 0;
  bool ok = true, want_key[DEPTH_MAX + 1] = {}, in_key = false;
  bool object[DEPTH_MAX + 1] = {};
  int index[DEPTH_MAX + 1] = {};
  char key[DEPTH_MAX + 1][KEY_MAX + 1] = {};
  char val[VALUE_MAX + 1];
  int len = 0;

  void add(char c) {
    int max = in_key ? KEY_MAX : VALUE_MAX;
    char *to = in_key ? key[depth] : val;
    if (len < max)
      to[len++] = c;
  }
  void add_code(uint32_t cp) {  // \u as UTF-8
    if (cp < 0x80) {
      add((char) cp);
    } else if (cp < 0x800) {
      add((char) (0xC0 | cp >> 6));
      add((char) (0x80 | (cp & 0x3F)));
    } else {
      add((char) (0xE0 | cp >> 12));
      add((char) (0x80 | (cp >> 6 & 0x3F)));
      add((char) (0x80 | (cp & 0x3F)));
    }
  }
  // The key of what's at depth d, or of its array
  const char *key_at(int d) const { return d <= 0 ? "" : object[d] ? key[d] : d > 1 ? key[d - 1] : ""; }
  void got() {
    val[len] = 0;
    value(depth, key_at(depth), object[depth] ? -1 : index[depth], val);
    len = 0;
  }
  void open(bool obj) {
    if (depth == DEPTH_MAX) {
      ok = false;
      return;
    }
    depth++;
    object[depth] = obj;
    want_key[depth] = obj;
    index[depth] = 0;
    key[depth][0] = 0;
  }
  void close() {
    if (depth == 0) {
      ok = false;
      return;
    }
    end(depth, key_at(depth - 1));
    depth--;
  }
  void step(char c) {
    switch (state) {
      case STRING:
        if (c == '\\') {
          state = ESCAPE;
        } else if (c == '"') {
          state = VALUE;
          if (in_key) {
            key[depth][len] = 0;
            len = 0;
            in_key = false;
          } else {
            got();
          }
        } else {
          add(c);
        }
        return;
      case ESCAPE:
        state = STRING;
        if (c == 'u') {
          state = UNICODE;
          hex = unicode = 0;
        } else {
          add(c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 'b' ? '\b' : c == 'f' ? '\f' : c);
        }
        return;
      case UNICODE:
        unicode = unicode << 4 | (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
        if (++hex == 4) {
          add_code((uint32_t) unicode);
          state = STRING;
        }
        return;
      case BARE:  // a number, true, false or null, until what ends it
        if (c != ',' && c != '}' && c != ']' && c != ' ' && c != '\n' && c != '\r' && c != '\t') {
          add(c);
          return;
        }
        got();
        state = VALUE;
        break;
    }
    switch (c) {
      case '{':
      case '[':
        open(c == '{');
        break;
      case '}':
      case ']':
        close();
        break;
      case ',':
        if (object[depth])
          want_key[depth] = true;
        else
          index[depth]++;
        break;
      case ':':
        want_key[depth] = false;
        break;
      case '"':
        state = STRING;
        len = 0;
        in_key = object[depth] && want_key[depth];
        break;
      case ' ':
      case '\n':
      case '\r':
      case '\t':
        break;
      default:
        state = BARE;
        len = 0;
        in_key = false;
        add(c);
    }
  }
};

}  // namespace flexit::panel
