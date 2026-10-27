#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace esphome {

class ESPPreferenceObject {
 public:
  ESPPreferenceObject() = default;
  explicit ESPPreferenceObject(uint32_t key) : key_(key) {}
  template<typename T> bool save(const T *src);
  template<typename T> bool load(T *dest);

 protected:
  uint32_t key_{0};
};

// Flash as ESPHome's preferences: save() queues a record, sync() writes what's queued
struct ESPPreferences {
  std::map<uint32_t, std::vector<uint8_t>> flash, queued;
  int writes = 0;  // records sync() changed in flash
  template<typename T> ESPPreferenceObject make_preference(uint32_t key, bool) { return ESPPreferenceObject(key); }
  bool sync() {
    for (auto &record : queued) {
      writes += flash[record.first] != record.second;
      flash[record.first] = record.second;
    }
    queued.clear();
    return true;
  }
};

extern ESPPreferences *global_preferences;

template<typename T> bool ESPPreferenceObject::save(const T *src) {
  auto *bytes = reinterpret_cast<const uint8_t *>(src);
  global_preferences->queued[key_].assign(bytes, bytes + sizeof(T));
  return true;
}

template<typename T> bool ESPPreferenceObject::load(T *dest) {
  auto it = global_preferences->flash.find(key_);
  if (it == global_preferences->flash.end() || it->second.size() != sizeof(T))
    return false;
  memcpy(dest, it->second.data(), sizeof(T));
  return true;
}

}  // namespace esphome
