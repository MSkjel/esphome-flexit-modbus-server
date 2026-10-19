#pragma once
#include <cstring>

namespace esphome {

class StringRef {
 public:
  StringRef(const char *str) : str_(str) {}
  friend bool operator==(const StringRef &a, const char *b) { return strcmp(a.str_, b) == 0; }

 protected:
  const char *str_;
};

}  // namespace esphome
