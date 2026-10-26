// Dates, and where the sun, the moon and the bright stars are in the sky. Plain C++ without ESPHome, see met_scene.h.
#pragma once
#include <cmath>
#include <cstdint>

namespace flexit::panel::met {

// ---------------------------------------------------------------- dates

uint32_t days_from_civil(int y, int m, int d);            // days since 1970-01-01
void civil_from_days(int32_t z, int *y, int *m, int *d);  // days since 1970-01-01 to a date

// Unix time of a UTC date and time
uint32_t unix_of(int y, int m, int d, int h = 0, int mi = 0, int s = 0);

// The day of the year, 1 = Jan 1
int day_of_year(int y, int m, int d);

int easter_day_of_year(int y);  // Gregorian Easter Sunday as day of the year, 1 = Jan 1

// ---------------------------------------------------------------- positions of the sun, moon and stars

inline constexpr double kPi = 3.14159265358979323846, kRad = kPi / 180;  // not PI, that's a macro in Arduino.h
inline constexpr float kPiF = (float) kPi;

inline int iround(float v) { return (int) lroundf(v); }

struct Pos {
  float el, az;  // degrees above the horizon, degrees clockwise from north
};

inline double wrap360(double a) { return a - 360 * floor(a / 360); }

// How the sky is turned at a place, d days after J2000: the local sidereal angle and the latitude's sine and cosine
struct Turn {
  double lst, sin_lat, cos_lat;
};
Turn turn_at(double lat, double lon, double d);

// Right ascension and declination in radians to elevation and azimuth. In float for the stars, where a hundredth
// of a degree is plenty.
Pos horizontal(double ra, double dec, const Turn &s);
Pos horizontal(float ra, float dec, const Turn &s);

inline double j2000(uint32_t t) { return t / 86400.0 - 10957.5; }

Pos sun_at(double lat, double lon, uint32_t t);

// Moon position from Schlyter's elements and the largest perturbations, good to a degree or two.
// phase 0 new, 0.25 first quarter, 0.5 full, 0.75 last quarter
Pos moon_at(double lat, double lon, uint32_t t, float *phase);

// Bright stars. ra in hours, dec in degrees, bright 1 is drawn bigger
struct Star {
  float ra, dec;
  uint8_t bright;
};
// clang-format off: laid out by hand
inline constexpr Star STARS[] = {
    // 0 Polaris
    {2.530f, 89.26f, 0},
    // 1-7 the Big Dipper
    {11.062f, 61.75f, 1}, {11.031f, 56.38f, 0}, {11.897f, 53.69f, 0}, {12.257f, 57.03f, 0},
    {12.900f, 55.96f, 1}, {13.399f, 54.93f, 0}, {13.792f, 49.31f, 1},
    // 8-12 Cassiopeia
    {0.153f, 59.15f, 0}, {0.675f, 56.54f, 1}, {0.945f, 60.72f, 0}, {1.430f, 60.24f, 0}, {1.907f, 63.67f, 0},
    // 13 Vega, 14 Deneb, 15 Altair
    {18.616f, 38.78f, 1}, {20.690f, 45.28f, 1}, {19.846f, 8.87f, 1},
    // 16-19 Cygnus
    {20.370f, 40.26f, 0}, {19.512f, 27.96f, 0}, {20.770f, 33.97f, 0}, {19.750f, 45.13f, 0},
    // 20 Capella, 21 Aldebaran, 22 Pleiades
    {5.278f, 45.998f, 1}, {4.599f, 16.51f, 1}, {3.790f, 24.10f, 0},
    // 23-26 Orion: Betelgeuse, Rigel, Bellatrix, Saiph
    {5.919f, 7.41f, 1}, {5.242f, -8.20f, 1}, {5.419f, 6.35f, 0}, {5.796f, -9.67f, 0},
    // 27-29 Orion's belt
    {5.679f, -1.94f, 0}, {5.604f, -1.20f, 0}, {5.533f, -0.30f, 0},
    // 30 Sirius, 31 Procyon, 32 Castor, 33 Pollux
    {6.752f, -16.72f, 1}, {7.655f, 5.22f, 1}, {7.577f, 31.89f, 0}, {7.755f, 28.03f, 1},
    // 34 Regulus, 35 Denebola, 36 Arcturus, 37 Spica
    {10.139f, 11.97f, 1}, {11.818f, 14.57f, 0}, {14.261f, 19.18f, 1}, {13.420f, -11.16f, 1},
    // 38 Antares, 39 Mirfak, 40 Algol
    {16.490f, -26.43f, 1}, {3.405f, 49.86f, 0}, {3.136f, 40.96f, 0},
    // 41-43 Andromeda
    {0.140f, 29.09f, 0}, {1.162f, 35.62f, 0}, {2.065f, 42.33f, 0},
    // 44-46 Pegasus, with Alpheratz
    {23.063f, 28.08f, 0}, {23.079f, 15.21f, 0}, {0.220f, 15.18f, 0},
    // 47 Hamal, 48 Elnath, 49 Menkalinan, 50 Kochab
    {2.120f, 23.46f, 0}, {5.438f, 28.61f, 0}, {5.992f, 44.95f, 0}, {14.845f, 74.16f, 0},
};
// clang-format on

}  // namespace flexit::panel::met
