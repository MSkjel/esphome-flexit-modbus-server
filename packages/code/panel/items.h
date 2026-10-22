// Items are the ESP's own entities on the panel. packages/panel/ adds them at boot and the panel gets them all
// after each hello, as the link has room. Messages are K_ITEM_* in plink_keys.h.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include "panel_link.h"
#include "plink.h"
#include "plink_icons.h"

namespace flexit::panel {

inline std::string lower(const char *s) {
  std::string r(s);
  for (auto &c : r)
    c = (char) tolower((unsigned char) c);
  return r;
}

// Index of the id whose name in lower case matches, or -1
template<size_t N> int find(const char *const (&ids)[N], const std::string &name) {
  for (size_t i = 0; i < N; i++)
    if (lower(ids[i]) == name)
      return (int) i;
  return -1;
}

#define ID_NAME(id, ...) #id,
static const char *const ICON_IDS[] = {PLINK_ICONS(ID_NAME)};
#undef ID_NAME

inline int icon(const std::string &name) {
  int i = find(ICON_IDS, name);
  if (i >= 0)
    return i;
  ESP_LOGW("panel", "No icon called '%s'", name.c_str());
  return ICN_CIRCLE;
}

// s cut to at most n bytes without splitting a UTF-8 character
inline std::string cut(const std::string &s, size_t n) {
  if (s.size() <= n)
    return s;
  while (n > 0 && ((uint8_t) s[n] & 0xC0) == 0x80)
    n--;
  return s.substr(0, n);
}

inline uint8_t language;  // the panel's LANGUAGE setting, picks the part of "English|Norsk" to use

// The part of "English|Norsk" for the panel's language, or the first part if that one is missing
inline std::string in_language(const std::string &both) {
  size_t from = 0;
  for (int k = 0; k < language; k++) {
    size_t bar = both.find('|', from);
    if (bar == std::string::npos)
      return both.substr(0, both.find('|'));
    from = bar + 1;
  }
  return both.substr(from, both.find('|', from) - from);
}

// Decimals a number with this step needs, like 1 for 0.5 and 2 for 0.25
inline int decimals_of(float step) {
  int d = 0;
  while (d < 3 && fabsf(step * powf(10, d) - roundf(step * powf(10, d))) > 1e-3f)
    d++;
  return d;
}

// What the panel still needs of an item
enum : uint8_t { NEED_DEF = 1, NEED_LABEL = 2, NEED_UNIT = 4, NEED_STATES = 8, NEED_VALUE = 16, NEED_TEXT = 32 };

class Item {
 public:
  Item(const std::string &name, uint8_t type) : name_(name), type_(type) {}
  Item &label(const std::string &s) {  // "English|Norsk"
    label_ = s;
    return *this;
  }
  Item &unit(const std::string &s) {
    unit_ = s;
    return *this;
  }
  Item &states(const std::string &on, const std::string &off) {  // a toggle's words, "" for On or Off
    on_ = on;
    off_ = off;
    return *this;
  }
  Item &help(const std::string &s) {  // shown when ? is pressed on its row
    help_ = s;
    return *this;
  }
  Item &icon(const std::string &s) {  // one of plink_icons.h, in lower case
    icon_ = s;
    return *this;
  }
  Item &decimals(int d) {  // 0 to 3
    dec_ = std::max(0, std::min(3, d));
    return *this;
  }
  // In the entity's units. Range and step for a number, ends of the gauge and bar for a sensor.
  Item &range(float min, float max, float step = 1) {
    min_ = min;
    max_ = max;
    step_ = step;
    return *this;
  }
  Item &page(const std::string &p) { return flag(ITEMF_COMFORT, p == "comfort"); }  // else smart_home
  Item &view(bool on) { return flag(ITEMF_VIEW, on); }
  Item &menu(bool on) { return flag(ITEMF_MENU, on); }
  Item &trend(bool on) { return flag(ITEMF_TREND, on); }
  Item &read_only(bool on = true) { return flag(ITEMF_RO, on); }
  // Colours the value. "bad below, good from, good to, bad above" in the entity's units, "" = none
  Item &levels(const std::string &four) {
    int n = 0;
    for (size_t at = 0; n < 4 && at < four.size(); n++) {
      size_t comma = four.find(',', at);
      level_[n] = std::strtof(four.substr(at, comma - at).c_str(), nullptr);
      at = comma == std::string::npos ? four.size() : comma + 1;
    }
    return flag(ITEMF_LEVELS, n == 4);
  }
  Item &on_set(std::function<void(float)> f) {  // changed on the panel, value in the entity's units
    set_ = std::move(f);
    return *this;
  }
  Item &echo_ms(uint32_t ms) {  // how long a change on the panel waits for the entity, see item_set()
    echo_ms_ = ms;
    return *this;
  }
  int add();  // index on the panel, -1 once N_ITEMS are taken

  // In the panel's units. Never INT32_MIN, that's ITEM_UNKNOWN.
  int32_t scaled(float v) const {
    float x = roundf(v * powf(10, dec_));
    return x >= 2147483648.0f ? INT32_MAX : x <= -2147483648.0f ? -INT32_MAX : (int32_t) x;
  }
  // What goes again in the panel's new language. An ITEM_TEXT's value is a text too.
  uint8_t texts() const {
    return NEED_LABEL | NEED_UNIT | (type_ == ITEM_TOGGLE ? NEED_STATES : 0) |
           (type_ == ITEM_TEXT && known_ ? NEED_TEXT : 0);
  }
  uint8_t value() const { return known_ && type_ != ITEM_TEXT ? NEED_VALUE : 0; }

  std::string name_, label_, unit_, help_, icon_ = "circle", on_, off_, text_;
  uint8_t type_, flags_ = ITEMF_VIEW | ITEMF_MENU;
  int dec_ = 0;
  float min_ = 0, max_ = 1, step_ = 1, level_[4] = {};
  std::function<void(float)> set_;
  int32_t value_ = 0;
  bool known_ = false;  // has a value or text to send
  uint16_t id_ = 0;
  uint8_t need_ = NEED_DEF;
  bool echo_ = false;  // changed on the panel, send the value back at echo_at_
  uint32_t echo_at_ = 0, echo_ms_ = 3000;

 private:
  Item &flag(uint8_t f, bool on) {
    flags_ = on ? flags_ | f : flags_ & ~f;
    return *this;
  }
};

inline std::vector<Item> items;

inline int Item::add() {
  if (items.size() >= N_ITEMS) {
    ESP_LOGW("panel", "The panel takes %d items: '%s' isn't one of them", N_ITEMS, name_.c_str());
    return -1;
  }
  uint32_t h = 2166136261u;  // id is a 14-bit FNV-1a of the name, never 0 and unique among the items
  for (char c : name_)
    h = (h ^ (uint8_t) c) * 16777619u;
  id_ = (uint16_t) (h & 0x3FFF ? h & 0x3FFF : 1);
  for (bool taken = true; taken;) {
    taken = false;
    for (auto &o : items)
      if (o.id_ == id_) {
        id_ = (uint16_t) ((id_ * 31u + 7u) & 0x3FFF ? (id_ * 31u + 7u) & 0x3FFF : 1);
        taken = true;
      }
  }
  if (type_ == ITEM_SENSOR || type_ == ITEM_TEXT)
    flags_ |= ITEMF_RO;
  // Longer texts get cut for the panel, each language's part on its own
  auto room = [this](const char *what, const std::string &both, size_t max) {
    for (size_t at = 0; at <= both.size();) {
      size_t bar = std::min(both.find('|', at), both.size());
      if (bar - at > max) {
        ESP_LOGW("panel", "'%s': the panel shows %u bytes of its %s, '%s' is cut", name_.c_str(), (unsigned) max, what,
                 both.substr(at, bar - at).c_str());
      }
      at = bar + 1;
    }
  };
  room("label", label_, ITEM_LABEL - 1);
  room("unit", unit_, ITEM_UNIT - 1);
  room("on_text", on_, ITEM_STATE - 1);
  room("off_text", off_, ITEM_STATE - 1);
  room("help", help_, PLINK_HELP_MAX);
  // The panel takes the step as 16 bits and the levels as signed 16 bits, in the value's units: 900 ppm with 1
  // decimal is 9000. Fewer decimals until they fit.
  int dec = dec_;
  while (dec_ > 0 && scaled(step_) > 65535)
    dec_--;
  if (dec_ != dec) {
    ESP_LOGW("panel", "'%s': %d decimals on the panel, so its step fits", name_.c_str(), dec_);
  }
  if (flags_ & ITEMF_LEVELS) {
    dec = dec_;
    auto fit = [this] {
      for (float l : level_)
        if (std::abs(scaled(l)) > 32767)
          return false;
      return true;
    };
    while (dec_ > 0 && !fit())
      dec_--;
    if (!fit()) {
      ESP_LOGW("panel", "'%s': its levels don't fit the panel, so it isn't coloured", name_.c_str());
      dec_ = dec;
      flags_ &= ~ITEMF_LEVELS;
    } else if (dec_ != dec) {
      ESP_LOGW("panel", "'%s': %d decimals on the panel, so its levels fit", name_.c_str(), dec_);
    }
  }
  need_ |= texts();
  items.push_back(*this);
  return (int) items.size() - 1;
}

// Layout at K_ITEM_DEF in plink_keys.h
inline bool send_def(int i) {
  const Item &it = items[i];
  uint8_t d[ITEM_DEF_LEN] = {it.type_, it.flags_, (uint8_t) icon(it.icon_), (uint8_t) it.dec_};
  auto put = [&d](int at, uint32_t v, int n) {
    for (int k = 0; k < n; k++)
      d[at + k] = (uint8_t) (v >> 8 * k);
  };
  put(4, (uint32_t) std::min<int32_t>(65535, std::max<int32_t>(1, it.scaled(it.step_))), 2);
  put(6, it.id_, 2);
  put(8, (uint32_t) it.scaled(it.min_), 4);
  put(12, (uint32_t) it.scaled(it.max_), 4);
  for (int k = 0; k < 4; k++)
    put(16 + 2 * k, (uint32_t) it.scaled(it.level_[k]), 2);
  return link::send(K_ITEM_DEF + i, d, sizeof d);
}

inline bool send_text(uint16_t key, const std::string &both, size_t max) {
  std::string t = cut(in_language(both), max);
  return link::send(key, (const uint8_t *) t.data(), (int) t.size());
}

// Every 20 ms while the panel is connected. Sends what it still needs as the link has room, so the lot after a
// hello doesn't overflow the queue, and anything dropped goes again.
inline void items_loop() {
  for (size_t i = 0; i < items.size(); i++) {
    Item &it = items[i];
    if (it.echo_ && (int32_t) (millis() - it.echo_at_) >= 0) {
      it.echo_ = false;
      if (it.known_)
        it.need_ |= NEED_VALUE;
    }
    while (it.need_ && link::waiting() < link::BACKLOG) {
      uint8_t part = it.need_ & -it.need_;  // the lowest, so the definition goes first
      bool sent = part == NEED_DEF      ? send_def((int) i)
                  : part == NEED_LABEL  ? send_text(K_ITEM_LABEL + i, it.label_, ITEM_LABEL - 1)
                  : part == NEED_UNIT   ? send_text(K_ITEM_UNIT + i, it.unit_, ITEM_UNIT - 1)
                  : part == NEED_STATES ? send_text(K_ITEM_ON + i, it.on_, ITEM_STATE - 1) &&
                                              send_text(K_ITEM_OFF + i, it.off_, ITEM_STATE - 1)
                  : part == NEED_TEXT   ? send_text(K_ITEM_TEXT + i, it.text_, ITEM_TEXT_LEN - 1)
                                        : link::send(K_ITEM_VALUE + i, (uint32_t) it.value_);
      if (!sent)
        return;
      it.need_ &= ~part;
    }
  }
}

// After the panel's hello, it needs them all
inline void items_hello() {
  for (auto &it : items)
    it.need_ = NEED_DEF | it.texts() | it.value();
}

// In the entity's units, NaN when it has none. Only sent if it changed.
inline void item_value(int i, float v) {
  if (i < 0 || i >= (int) items.size())
    return;
  Item &it = items[i];
  int32_t s = std::isnan(v) ? ITEM_UNKNOWN : it.scaled(v);
  if (it.known_ && s == it.value_)
    return;
  it.value_ = s;
  it.known_ = true;
  it.need_ |= NEED_VALUE;
  it.echo_ = false;  // the new value goes anyway
}

// An ITEM_TEXT's value, "English|Norsk" like the label. The panel shows a dash for "".
inline void item_value_text(int i, const std::string &both) {
  if (i < 0 || i >= (int) items.size() || (items[i].known_ && items[i].text_ == both))
    return;
  Item &it = items[i];
  it.text_ = both;
  it.known_ = true;
  it.need_ |= NEED_TEXT;
}

// met.h uses these to name the weather item after the weather
inline void item_label(int i, const std::string &both) {
  if (i < 0 || i >= (int) items.size() || items[i].label_ == both)
    return;
  items[i].label_ = both;
  items[i].need_ |= NEED_LABEL;
}

inline void item_icon(int i, const std::string &name) {
  if (i < 0 || i >= (int) items.size() || items[i].icon_ == name)
    return;
  items[i].icon_ = name;
  items[i].need_ |= NEED_DEF;
}

// Changed on the panel. If the entity hasn't changed within its echo_ms we send back the value it has, so a refused
// change shows. An entity that answers later than that would flip the panel back and forth.
inline void item_set(int i, uint32_t v) {
  if (i >= (int) items.size() || !items[i].set_)
    return;
  Item &it = items[i];
  it.set_((int32_t) v / powf(10, it.dec_));
  it.echo_ = true;
  it.echo_at_ = millis() + it.echo_ms_;
}

// ? pressed on item i
inline void item_help(uint32_t i) {
  if (i >= items.size())
    return;
  std::string t = std::string(1, (char) i) + cut(in_language(items[i].help_), PLINK_HELP_MAX);
  link::send(K_ITEM_HELP, (const uint8_t *) t.data(), (int) t.size());
}

inline void set_language(uint32_t v) {
  if (v == language)
    return;
  language = (uint8_t) v;
  for (auto &it : items)
    it.need_ |= it.texts();
}

}  // namespace flexit::panel
