#pragma once
#include <cstdint>
#include <functional>
#include <vector>

namespace esphome {

template<typename... X> class CallbackManager;
template<typename... Ts> class CallbackManager<void(Ts...)> {
 public:
  void add(std::function<void(Ts...)> &&callback) { callbacks_.push_back(std::move(callback)); }
  void call(Ts... args) {
    for (auto &callback : callbacks_)
      callback(args...);
  }

 protected:
  std::vector<std::function<void(Ts...)>> callbacks_;
};

inline uint32_t fnv1_hash(const char *str) {
  uint32_t hash = 2166136261UL;
  for (; *str; str++) {
    hash *= 16777619UL;
    hash ^= (uint8_t) *str;
  }
  return hash;
}

}  // namespace esphome
