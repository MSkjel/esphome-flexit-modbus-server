// Mode arbiter for packages/controls.yaml
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
inline bool confirmed = true;  // the CS60 runs it, or 30 s have passed
inline int user_mode = -1;     // the user's mode when we last looked, -1 = not yet

// Runs the highest mode asked for as a temporary one, and the user's mode again once nobody asks. The server
// keeps the user's mode, and sends it again at boot if we went down with the mode up.
inline void arbiter() {
  auto &s = id(server);
  // The mode reads 0 until the CS60's first status broadcast
  if (!s.synced())
    return;
  const uint32_t now = millis();
  const int mode = s.read_holding_register(flexit_modbus_server::REG_MODE);
  const int base = s.real_value(flexit_modbus_server::REG_CMD_MODE);

  // The CS60 takes a few seconds to run a mode we sent. Until it does (or 30 s have passed) the mode it
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

  int want = 0;
  for (auto &r : requests)
    want = std::max(want, r.second);

  // Requests only raise the mode, and never start a stopped unit (0 is also not known yet)
  if (base == 0 || want <= base) {
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

// false once the arbiter dropped it after a mode change by someone else
inline bool requesting(const std::string &who) { return requests.count(who) != 0; }

inline std::string boosted_by() {
  std::string names;
  for (auto &r : requests) {
    if (!names.empty())
      names += ", ";
    names += r.first;
  }
  return names.empty() ? std::string("nothing") : names;
}

}  // namespace flexit::controls
