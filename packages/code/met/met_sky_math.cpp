// Dates and the sky's positions, see met_sky_math.h
#include "met_sky_math.h"

namespace flexit::panel::met {

uint32_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  int era = (y >= 0 ? y : y - 399) / 400;
  int yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  return (uint32_t) (era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468);
}

void civil_from_days(int32_t z, int *y, int *m, int *d) {
  z += 719468;
  int era = (z >= 0 ? z : z - 146096) / 146097;
  int doe = z - era * 146097;
  int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = yoe + era * 400 + (*m <= 2);
}

uint32_t unix_of(int y, int m, int d, int h, int mi, int s) {
  return days_from_civil(y, m, d) * 86400u + (uint32_t) (h * 3600 + mi * 60 + s);
}

int day_of_year(int y, int m, int d) { return (int) (days_from_civil(y, m, d) - days_from_civil(y, 1, 1)) + 1; }

int easter_day_of_year(int y) {
  int a = y % 19, b = y / 100, c = y % 100, d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
  int h = (19 * a + b - d - g + 15) % 30, i = c / 4, k = c % 4, l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m = (a + 11 * h + 22 * l) / 451, month = (h + l - 7 * m + 114) / 31, day = (h + l - 7 * m + 114) % 31 + 1;
  return day_of_year(y, month, day);
}

Turn turn_at(double lat, double lon, double d) {
  return {wrap360(280.46061837 + 360.98564736629 * d + lon) * kRad, sin(lat * kRad), cos(lat * kRad)};
}

template<typename T> static Pos horizontal_of(T ra, T dec, const Turn &s) {
  T ha = (T) s.lst - ra, sl = (T) s.sin_lat, cl = (T) s.cos_lat, rad = (T) kRad;
  T el = std::asin(sl * std::sin(dec) + cl * std::cos(dec) * std::cos(ha));
  T az = std::atan2(std::sin(ha), std::cos(ha) * sl - std::tan(dec) * cl) / rad + 180;  // from south + 180
  return {(float) (el / rad), (float) wrap360(az)};
}
Pos horizontal(double ra, double dec, const Turn &s) { return horizontal_of(ra, dec, s); }
Pos horizontal(float ra, float dec, const Turn &s) { return horizontal_of(ra, dec, s); }

static double sun_longitude(double d) {  // ecliptic longitude in radians, good to 0.01 degrees
  double g = (357.529 + 0.98560028 * d) * kRad;
  return (280.459 + 0.98564736 * d + 1.915 * sin(g) + 0.020 * sin(2 * g)) * kRad;
}

Pos sun_at(double lat, double lon, uint32_t t) {
  double d = j2000(t), l = sun_longitude(d), e = (23.439 - 0.00000036 * d) * kRad;
  return horizontal(atan2(cos(e) * sin(l), cos(l)), asin(sin(e) * sin(l)), turn_at(lat, lon, d));
}

Pos moon_at(double lat, double lon, uint32_t t, float *phase) {
  double d = j2000(t) + 1.5;  // Schlyter's day 0 is 1999-12-31 0h
  double n = (125.1228 - 0.0529538083 * d) * kRad, i = 5.1454 * kRad, w = (318.0634 + 0.1643573223 * d) * kRad;
  double e = 0.0549, m = wrap360(115.3654 + 13.0649929509 * d) * kRad;
  double ea = m + e * sin(m) * (1 + e * cos(m));
  double x = cos(ea) - e, y = sqrt(1 - e * e) * sin(ea), v = atan2(y, x);
  double lon_e =
      atan2(sin(n) * cos(v + w) + cos(n) * sin(v + w) * cos(i), cos(n) * cos(v + w) - sin(n) * sin(v + w) * cos(i));
  double lat_e = asin(sin(v + w) * sin(i));
  double ms = (356.0470 + 0.9856002585 * d) * kRad, ls = ms + (282.9404 + 4.70935e-5 * d) * kRad;
  double lm = m + w + n, dd = lm - ls, f = lm - n;
  lon_e += (-1.274 * sin(m - 2 * dd) + 0.658 * sin(2 * dd) - 0.186 * sin(ms) - 0.059 * sin(2 * m - 2 * dd) -
            0.057 * sin(m - 2 * dd + ms) + 0.053 * sin(m + 2 * dd)) *
           kRad;
  lat_e += (-0.173 * sin(f - 2 * dd) - 0.055 * sin(m - f - 2 * dd) - 0.046 * sin(m + f - 2 * dd)) * kRad;
  if (phase)
    *phase = (float) (wrap360((lon_e - sun_longitude(j2000(t))) / kRad) / 360);
  double ob = (23.4393 - 3.563e-7 * d) * kRad;
  double xe = cos(lon_e) * cos(lat_e), ye = sin(lon_e) * cos(lat_e), ze = sin(lat_e);
  double yq = ye * cos(ob) - ze * sin(ob), zq = ye * sin(ob) + ze * cos(ob);
  return horizontal(atan2(yq, xe), atan2(zq, sqrt(xe * xe + yq * yq)), turn_at(lat, lon, j2000(t)));
}

}  // namespace flexit::panel::met
