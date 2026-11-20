// Mode arbiter for packages/controls.yaml, and what the packages that boost share
#pragma once
#include <algorithm>
#include <map>
#include <string>

namespace flexit::controls {

static constexpr const char *TAG = "flexit.mode";

// What a reboot loses
inline struct State {
  // Who wants a higher mode, and which. Fireplace, humidity boost etc. add one while they want it, the highest
  // runs. A mode change by anyone else drops them all.
  std::map<std::string, int> requests;

  int target = -1;            // the mode the boosts run, -1 = not boosting
  bool timer = false;         // true: max on our max timer. false: target sent as a temporary mode
  bool timer_redone = false;  // restarted once because the CX60 started it with the user's length
  uint32_t sent_ms = 0;       // when we sent target or last (re)started the timer
  bool confirmed = true;      // the CX60 has run target, or 30 s have passed
  bool rejected = false;      // the CX60 didn't run target within 30 s, we wait to retry
  int rejected_in = -1;       // the CX60's mode at the rejection, retry when it changes
  uint32_t rejected_ms = 0;   // when it was rejected, retry after RETRY_MS
  int user_mode = -1;         // the user's mode when we last looked, -1 = not yet
} state;

// The length we give the max timer, short so max ends soon if the ESP dies, and when we restart it
static constexpr uint16_t TIMER_MINUTES = 10;
static constexpr uint32_t TIMER_RESTART_BELOW_S = 5 * 60;
// A rejected target is retried after this, or sooner if the CX60's mode changes
static constexpr uint32_t RETRY_MS = 30 * 60000;

// The unit is stopped, or its mode isn't known yet
inline bool stopped() {
  return id(server).read_holding_register(flexit_modbus_server::REG_MODE) == flexit_modbus_server::MODE_STOP;
}

// Seconds left on the max timer, 0 = off
inline uint16_t timer_left() {
  return id(server).read_holding_register(flexit_modbus_server::REG_SECONDS_REMAINING_MAX_TIMER);
}

// Starts or restarts our max timer. Its length (and the timer's speed for AC fans) are sent as temporary settings,
// so a reboot puts the user's back.
inline void start_timer(uint32_t now) {
  namespace fms = flexit_modbus_server;
  auto &s = id(server);
  if (s.read_holding_register(fms::REG_CMD_MINUTES_MAX_TIMER) != TIMER_MINUTES)
    s.send_cmd(fms::REG_CMD_MINUTES_MAX_TIMER, TIMER_MINUTES, true);
  if (s.read_holding_register(fms::REG_DS4_FAN_TYPE) == 1 &&
      s.read_holding_register(fms::REG_CMD_MAX_TIMER_SPEED) != fms::MODE_MAX)
    s.send_cmd(fms::REG_CMD_MAX_TIMER_SPEED, fms::MODE_MAX, true);
  s.send_cmd(fms::REG_CMD_START_MAX_TIMER, 1);
  state.timer = true;
  state.sent_ms = now;
}

// Ends the boost: stops our max timer and puts its settings back, or puts the user's mode back
inline void end_boost() {
  namespace fms = flexit_modbus_server;
  auto &s = id(server);
  if (state.timer) {
    s.send_cmd(fms::REG_CMD_STOP_MAX_TIMER, 1);
    s.restore(fms::REG_CMD_MINUTES_MAX_TIMER);
    s.restore(fms::REG_CMD_MAX_TIMER_SPEED);
  } else {
    s.restore(fms::REG_CMD_MODE);
  }
  state.target = -1;
  state.timer = false;
  state.rejected = false;
}

// Runs the highest mode requested, or the user's mode when nothing is. Max runs on the CX60's max timer, because
// while the week program sets the mode the CX60 ignores mode commands but not the timer. Normal is sent as a
// temporary mode. A max timer someone else started is left alone.
inline void arbiter() {
  namespace fms = flexit_modbus_server;
  auto &s = id(server);
  // The mode reads 0 until the CX60's first status broadcast
  if (!s.synced())
    return;
  const uint32_t now = millis();
  const int mode = s.read_holding_register(fms::REG_MODE);
  const int base = s.real_value(fms::REG_CMD_MODE);

  // Not run within 30 s, most likely because the week program sets the mode. Take back what we sent, or the CX60
  // runs it at the week program's next change. Keep the requests and retry later.
  if (!state.confirmed && mode == state.target) {
    state.confirmed = true;
  } else if (!state.confirmed && now - state.sent_ms >= 30000) {
    ESP_LOGW(TAG, "The CX60 didn't go from %s to %s, the week program? Trying again later",
             fms::mode_to_string(mode).c_str(), fms::mode_to_string(state.target).c_str());
    int target = state.target;
    end_boost();
    state.target = target;
    state.confirmed = true;
    state.rejected = true;
    state.rejected_in = mode;
    state.rejected_ms = now;
  }
  if (state.rejected && (mode != state.rejected_in || now - state.rejected_ms >= RETRY_MS)) {
    state.target = -1;
    state.rejected = false;
  }

  // Somebody else changed the mode: by hand, the week program, an input, stopping our timer. The user's own mode
  // changing counts even when no request raises it right now, like CO2 asking for normal while it's in normal.
  if (((state.target >= 0 && state.confirmed && !state.rejected && mode != state.target) ||
       (state.user_mode >= 0 && base != state.user_mode)) &&
      !state.requests.empty()) {
    ESP_LOGI(TAG, "Mode was changed by hand, dropping all boosts");
    state.requests.clear();
  }
  state.user_mode = base;

  int want = fms::MODE_STOP;
  for (auto &r : state.requests)
    want = std::max(want, r.second);

  // Requests only raise the mode, and never start a stopped unit (stop is also not known yet)
  if (base == fms::MODE_STOP || want <= base) {
    if (state.target >= 0) {
      ESP_LOGI(TAG, "Boosts done, going back to %s", fms::mode_to_string(base).c_str());
      end_boost();
    }
    return;
  }
  if (want == state.target) {
    // Restart our timer before it runs out. Also once if it runs longer than ours: the CX60 started it before it had
    // our length.
    const uint16_t left = timer_left();
    const bool too_long = left > TIMER_MINUTES * 60 + 60 && !state.timer_redone;
    if (state.timer && !state.rejected && now - state.sent_ms >= 10000 && (left < TIMER_RESTART_BELOW_S || too_long)) {
      state.timer_redone |= too_long;
      start_timer(now);
    }
    return;
  }

  if (want == fms::MODE_MAX && state.target < 0 && mode == fms::MODE_MAX && timer_left() > 0)
    return;  // someone else's max timer runs max already. Ours starts if it ends first.
  ESP_LOGI(TAG, "Going from %s to %s", fms::mode_to_string(base).c_str(), fms::mode_to_string(want).c_str());
  if (state.target >= 0)
    end_boost();
  if (want == fms::MODE_MAX) {
    state.timer_redone = false;
    start_timer(now);
  } else
    s.send_cmd(fms::REG_CMD_MODE, want, true);
  state.target = want;
  state.sent_ms = now;
  state.confirmed = false;
}

// who wants a higher mode, and which
inline void request(const std::string &who, int mode) {
  state.requests[who] = mode;
  arbiter();
}

inline void release(const std::string &who) {
  state.requests.erase(who);
  arbiter();
}

// The mode `who` asks for, -1 if none or the arbiter dropped it after a mode change by someone else
inline int requested(const std::string &who) {
  auto r = state.requests.find(who);
  return r == state.requests.end() ? -1 : r->second;
}

inline std::string boosted_by() {
  std::string names;
  for (auto &r : state.requests) {
    if (!names.empty())
      names += ", ";
    names += r.first;
  }
  return names.empty() ? std::string("nothing") : names;
}

// A package asking for a higher mode until it's done. After somebody changes the mode by hand it's unarmed, and
// the package arms it again once there's no reason to boost anymore.
struct Boost {
  const char *who;  // its name with the arbiter
  const char *tag;  // for the log
  bool active = false;
  bool armed = true;
  uint32_t start_ms = 0;

  void start(int mode) {
    active = true;
    start_ms = millis();
    request(who, mode);
  }
  void stop() {
    active = false;
    release(who);
  }
  // Running, but the arbiter dropped it. Unarms it, the package stops it.
  bool dropped() {
    if (!active || requested(who) >= 0)
      return false;
    ESP_LOGI(tag, "Stopping, the mode was changed by hand");
    armed = false;
    return true;
  }
  uint32_t runtime_ms() const { return millis() - start_ms; }
};

// The supply and extract fan speed settings of a mode, which fireplace and CO2 raise for a while. The server
// keeps the real ones.
struct Fans {
  uint16_t regs[2];  // supply, extract

  // the real speed, 0 = not known yet
  int real(int k) const { return id(server).real_value(regs[k]); }
  bool known() const { return real(0) != 0 && real(1) != 0; }
  // ours, as temporary values
  void set(int supply, int extract) const {
    id(server).send_cmd(regs[0], supply, true);
    id(server).send_cmd(regs[1], extract, true);
  }
  // the real ones back. After a reboot the server does that itself.
  void restore(const char *tag) const {
    ESP_LOGI(tag, "Fan speeds back to supply %d%%, extract %d%%", real(0), real(1));
    id(server).restore(regs[0]);
    id(server).restore(regs[1]);
  }
};
inline constexpr Fans NORMAL_FANS{{flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_NORMAL,
                                   flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_NORMAL}};
inline constexpr Fans MAX_FANS{{flexit_modbus_server::REG_CMD_PERCENTAGE_SUPPLY_FAN_MAX,
                                flexit_modbus_server::REG_CMD_PERCENTAGE_EXTRACT_FAN_MAX}};

}  // namespace flexit::controls
