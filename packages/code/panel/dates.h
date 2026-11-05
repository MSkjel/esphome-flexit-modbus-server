// Dates as Unix time and back, UTC, and read from text. Plain C++ without ESPHome.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstring>

namespace flexit::panel {

inline constexpr const char *MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// Days since 1970-01-01
inline uint32_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return (uint32_t) (era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468);
}

// Days since 1970-01-01 to a date
inline void civil_from_days(int32_t z, int *y, int *m, int *d) {
  z += 719468;
  int era = (z >= 0 ? z : z - 146096) / 146097;
  int doe = z - era * 146097;
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = yoe + era * 400 + (*m <= 2);
}

// Unix time of a UTC date and time
inline uint32_t unix_of(int y, int m, int d, int h = 0, int mi = 0, int s = 0) {
  return days_from_civil(y, m, d) * 86400u + (uint32_t) (h * 3600 + mi * 60 + s);
}

// The next whole number in s, skipping what comes before it, and s moved past it. -1 if there's none.
inline int next_int(const char *&s) {
  while (*s && !isdigit((unsigned char) *s))
    s++;
  if (!*s)
    return -1;
  int v = 0;
  while (isdigit((unsigned char) *s))
    v = v * 10 + (*s++ - '0');
  return v;
}

// "2026-10-30T09:21:53.576" or "2026-11-01 22:41:05", UTC, as Unix time. 0 if it isn't one.
inline uint32_t utc_of(const char *s) {
  int v[6];
  for (int &n : v)
    if ((n = next_int(s)) < 0)
      return 0;
  return unix_of(v[0], v[1], v[2], v[3], v[4], v[5]);
}

// "Oct" as 10, 0 if it isn't a month
inline int month_of(const char *s) {
  for (int m = 0; m < 12; m++)
    if (!strncmp(s, MONTHS[m], 3))
      return m + 1;
  return 0;
}

}  // namespace flexit::panel
