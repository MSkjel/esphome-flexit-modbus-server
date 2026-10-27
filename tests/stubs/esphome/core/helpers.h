#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <vector>

namespace esphome {

template<typename T> using optional = std::optional<T>;
using std::clamp;

// ESPHome's is on pointers only, the tests keep some of their entities by value
template<typename T> T &id(T *value) { return *value; }
template<typename T> T &id(T &value) { return value; }

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

inline bool no_ram;  // RAMAllocator fails while set
template<typename T> struct RAMAllocator {
  T *allocate(size_t n) { return no_ram ? nullptr : (T *) malloc(n * sizeof(T)); }
  void deallocate(T *p, size_t) { free(p); }
};

}  // namespace esphome
