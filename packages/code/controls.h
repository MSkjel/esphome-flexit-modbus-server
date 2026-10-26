// Mode arbiter for packages/controls.yaml, and what the packages that boost share
#pragma once
#include <algorithm>
#include <map>
#include <string>

namespace flexit::controls {

// Who wants a higher mode, and which. Fireplace, humidity boost etc. add one while they want it, the highest
// runs. A mode change by anyone else drops them all.
inline std::map<std::string, int> requests;

inline int target = -1;        // the mode we sent while boosting, -1 = not boosting
inline uint32_t sent_ms;       // when
inline bool confirmed = true;  // the CX60 runs it, or 30 s have passed
inline int user_mode = -1;     // the user's mode when we last looked, -1 = not yet

// The unit is stopped, or its mode isn't known yet
inline bool stopped() {
  return id(server).read_holding_register(flexit_modbus_server::REG_MODE) == flexit_modbus_server::MODE_STOP;
}

// Runs the highest mode asked for as a temporary one, and the user's mode again once nobody asks. The server
// keeps the user's mode, and sends it again at boot if we went down with the mode up.
inline void arbiter() {
  auto &s = id(server);
  // The mode reads 0 until the CX60's first status broadcast
  if (!s.synced())
    return;
  const uint32_t now = millis();
  const int mode = s.read_holding_register(flexit_modbus_server::REG_MODE);
  const int base = s.real_value(flexit_modbus_server::REG_CMD_MODE);

  // The CX60 takes a few seconds to run a mode we sent. Until it does (or 30 s have passed) the mode it
  // reports says nothing about anyone else.
  if (!confirmed && (mode == target || now - sent_ms >= 30000))
    confirmed = true;

  // Somebody else changed the mode: by hand, the week program, an input. The user's own mode changing counts
  // even when no request raises it right now, like CO2 asking for normal while it's in normal.
  if (((target >= 0 && confirmed && mode != target) || (user_mode >= 0 && base != user_mode)) && !requests.empty()) {
    ESP_LOGW("mode", "Mode was changed by hand, dropping all boosts");
    requests.clear();
  }
  user_mode = base;

  int want = flexit_modbus_server::MODE_STOP;
  for (auto &r : requests)
    want = std::max(want, r.second);

  // Requests only raise the mode, and never start a stopped unit (stop is also not known yet)
  if (base == flexit_modbus_server::MODE_STOP || want <= base) {
    if (target >= 0) {
      ESP_LOGI("mode", "Boosts done, going back to %s", flexit_modbus_server::mode_to_string(base).c_str());
      s.restore(flexit_modbus_server::REG_CMD_MODE);
      target = -1;
    }
    return;
  }
  if (want == target)
    return;
  ESP_LOGI("mode", "Going from %s to %s", flexit_modbus_server::mode_to_string(base).c_str(),
           flexit_modbus_server::mode_to_string(want).c_str());
  s.send_cmd(flexit_modbus_server::REG_CMD_MODE, want, true);
  target = want;
  sent_ms = now;
  confirmed = false;
}

// who wants a higher mode, and which
inline void request(const std::string &who, int mode) {
  requests[who] = mode;
  arbiter();
}

inline void release(const std::string &who) {
  requests.erase(who);
  arbiter();
}

// The mode `who` asks for, -1 if none or the arbiter dropped it after a mode change by someone else
inline int requested(const std::string &who) {
  auto r = requests.find(who);
  return r == requests.end() ? -1 : r->second;
}

inline std::string boosted_by() {
  std::string names;
  for (auto &r : requests) {
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
    ESP_LOGW(tag, "Stopping, the mode was changed by hand");
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
