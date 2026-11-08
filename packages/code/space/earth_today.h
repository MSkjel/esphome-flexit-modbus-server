// earth_today.yaml: the whole sunlit Earth as DSCOVR's EPIC camera saw it last, from a million and a half km. Its
// list of the newest day's photos is asked for a few times a day and read as it streams. The newest photo's
// thumbnail is downloaded as an online picture when it's shown and drawn at the right, with when it was taken, how
// far away it was seen from and what it faces as marks at the left. Photos: NASA EPIC Team.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include "../panel/picture_online.h"
#include "space.h"

namespace flexit::panel::space::earth {

inline constexpr const char *LIST = "https://epic.gsfc.nasa.gov/api/natural";
inline constexpr const char *ARCHIVE = "https://epic.gsfc.nasa.gov/archive/natural/";
inline constexpr int DISC_X = marks::W - 246, DISC_Y = -6, DISC = 252;  // where the photo goes, and its size

struct Frame {
  std::string image, day;      // its name, and the day "2026/11/01" its archive has it under
  uint32_t taken = 0;          // UTC
  float lat = 0, lon = 0;      // where it faces
  double x = 0, y = 0, z = 0;  // DSCOVR from the Earth's centre, km
};

// The list's photos as they come, the newest kept
struct List : Ask {
  Frame now, best;

  void value(int depth, const char *key, int, const char *v) override {
    const char *in = parent();
    if (depth == 2 && !strcmp(key, "image")) {
      now.image = v;
    } else if (depth == 2 && !strcmp(key, "date")) {
      now.taken = utc_of(v);
      if (strlen(v) >= 10)
        now.day = std::string(v, 4) + "/" + std::string(v + 5, 2) + "/" + std::string(v + 8, 2);
    } else if (depth == 3 && !strcmp(in, "centroid_coordinates")) {
      (!strcmp(key, "lat") ? now.lat : now.lon) = (float) atof(v);
    } else if (depth == 3 && !strcmp(in, "dscovr_j2000_position")) {
      (*key == 'x' ? now.x : *key == 'y' ? now.y : now.z) = atof(v);
    }
  }
  void end(int depth, const char *) override {
    if (depth != 2)
      return;
    if (!now.image.empty() && !now.day.empty() && now.taken >= best.taken)
      best = now;
    now = Frame();
  }
};

// EPIC's list of the newest day's
struct Photos : Listing<List> {
  bool got(fetch::Request &r) override;
};

// What's in the middle of the disc, by its longitude, a line break as |
inline const char *facing(float lon) {
  static const struct {
    float from;
    const char *what;
  } SIDES[] = {{-180, "the Pacific"},
               {-130, "the Americas|and the Pacific"},
               {-95, "the Americas"},
               {-55, "South America|and the Atlantic"},
               {-25, "Africa and|the Atlantic"},
               {15, "Africa and Asia"},
               {50, "Asia and the|Indian Ocean"},
               {85, "Asia"},
               {120, "Asia and|Australia"},
               {150, "Australia and|the Pacific"}};
  const char *w = SIDES[0].what;
  for (const auto &s : SIDES)
    if (lon >= s.from)
      w = s.what;
  return w;
}

// The towns the middle of the disc is likely near: it's over the tropics, as far north or south as the sun is
struct Town {
  const char *name;
  float lat, lon;
};
inline constexpr Town TOWNS[] = {
    {"Honolulu", 21.3f, -157.9f},
    {"Papeete", -17.5f, -149.6f},
    {"Apia", -13.8f, -171.8f},
    {"Suva", -18.1f, 178.4f},
    {"Nouméa", -22.3f, 166.5f},
    {"Port Moresby", -9.4f, 147.2f},
    {"Darwin", -12.5f, 130.8f},
    {"Guam", 13.5f, 144.8f},
    {"Manila", 14.6f, 121.0f},
    {"Hong Kong", 22.3f, 114.2f},
    {"Ho Chi Minh City", 10.8f, 106.7f},
    {"Bangkok", 13.8f, 100.5f},
    {"Singapore", 1.35f, 103.8f},
    {"Jakarta", -6.2f, 106.8f},
    {"Dhaka", 23.8f, 90.4f},
    {"Chennai", 13.1f, 80.3f},
    {"Colombo", 6.9f, 79.9f},
    {"Mumbai", 19.1f, 72.9f},
    {"Karachi", 24.9f, 67.0f},
    {"Muscat", 23.6f, 58.4f},
    {"Riyadh", 24.7f, 46.7f},
    {"Cairo", 30.0f, 31.2f},
    {"Khartoum", 15.6f, 32.5f},
    {"Addis Ababa", 9.0f, 38.7f},
    {"Nairobi", -1.3f, 36.8f},
    {"Dar es Salaam", -6.8f, 39.3f},
    {"Antananarivo", -18.9f, 47.5f},
    {"Harare", -17.8f, 31.0f},
    {"Johannesburg", -26.2f, 28.0f},
    {"Luanda", -8.8f, 13.2f},
    {"Kinshasa", -4.3f, 15.3f},
    {"Lagos", 6.5f, 3.4f},
    {"Accra", 5.6f, -0.2f},
    {"Dakar", 14.7f, -17.5f},
    {"Praia", 14.9f, -23.5f},
    {"Recife", -8.0f, -34.9f},
    {"Salvador", -13.0f, -38.5f},
    {"Rio de Janeiro", -22.9f, -43.2f},
    {"Brasília", -15.8f, -47.9f},
    {"Belém", -1.5f, -48.5f},
    {"Manaus", -3.1f, -60.0f},
    {"La Paz", -16.5f, -68.1f},
    {"Lima", -12.0f, -77.0f},
    {"Bogotá", 4.7f, -74.1f},
    {"Caracas", 10.5f, -66.9f},
    {"Panama City", 9.0f, -79.5f},
    {"Havana", 23.1f, -82.4f},
    {"Mexico City", 19.4f, -99.1f},
    {"the Galápagos", -0.7f, -90.3f},
    {"Easter Island", -27.1f, -109.4f},
    {"Tarawa", 1.4f, 173.0f},
};

// km between two places on the Earth
inline float km_between(float lat1, float lon1, float lat2, float lon2) {
  const float r = (float) M_PI / 180;
  float a = sinf((lat2 - lat1) * r / 2), b = sinf((lon2 - lon1) * r / 2);
  float h = a * a + cosf(lat1 * r) * cosf(lat2 * r) * b * b;
  return 2 * 6371 * asinf(std::min(1.0f, sqrtf(h)));
}

// The nearest town within 900 km, nullptr if none is
inline const char *near(float lat, float lon) {
  const char *best = nullptr;
  float d = 900;
  for (const Town &t : TOWNS) {
    float k = km_between(lat, lon, t.lat, t.lon);
    if (k < d) {
      d = k;
      best = t.name;
    }
  }
  return best;
}

inline Photos list;
inline Frame frame;  // shown, or to be once downloaded
inline Today today;

// Degrees as "12° S"
inline std::string degrees(float v, char pos, char neg) {
  char b[16];
  snprintf(b, sizeof b, "%.0f° %c", fabsf(v), v < 0 ? neg : pos);
  return b;
}

inline marks::Layer layer_of(const Frame &f) {
  marks::Layer l;
  l.text(12, 24, "Earth", marks::BIG);
  l.caps(12, 36, when(f.taken));
  l.caps(12, 70, "Seen from");
  char d[16];
  snprintf(d, sizeof d, "%.2f", sqrt(f.x * f.x + f.y * f.y + f.z * f.z) / 1e6);
  float w = l.text(12, 86, d, marks::HUGE);
  l.text(12 + w + 3, 86, "million km", marks::SMALL, marks::WHITE, 0.75f);
  l.caps(12, 116, "Facing");
  std::string side = facing(f.lon);
  size_t bar = side.find('|');
  l.text(12, 130, side.substr(0, bar), marks::TEXT, marks::WHITE, 0.9f);
  if (bar != std::string::npos)
    l.text(12, 142, side.substr(bar + 1), marks::TEXT, marks::WHITE, 0.9f);
  l.caps(12, 172, "Centre");
  l.text(12, 186, degrees(f.lat, 'N', 'S') + " " + degrees(f.lon, 'E', 'W'), marks::TEXT, marks::WHITE, 0.9f);
  if (const char *t = near(f.lat, f.lon))
    l.text(12, 198, std::string("near ") + t, marks::TEXT, marks::WHITE, 0.9f);
  l.caps(12, marks::H - 12, "NASA EPIC", 0.4f);
  return l;
}

inline bool Photos::got(fetch::Request &q) {
  const List &r = static_cast<List &>(q);
  if (r.best.image.empty())
    return false;
  say(r.best.image + ", " + when(r.best.taken));
  std::string u = std::string(ARCHIVE) + r.best.day + "/thumbs/" + r.best.image + ".jpg";
  if (u == today.url)
    return true;
  ESP_LOGI("space", "Earth today: %s", u.c_str());
  frame = r.best;
  today.show(u);
  marks::set(today.name, layer_of(frame));
  return true;
}

// The photo at the right, smoothly scaled up, black around it
inline uint32_t pixel(int x, int y) {
  int w, h;
  today.dims(w, h);
  return today.at((x + 0.5f - DISC_X) * w / DISC - 0.5f, (y + 0.5f - DISC_Y) * h / DISC - 0.5f, w, h);
}

// From earth_today.yaml's on_boot, after online_picture.yaml's. every: how often to look for a newer photo, status:
// its sensor.
template<typename Img, typename S> void setup(const std::string &name, Img &&image, const char *every, S &&status) {
  list.what = "photo";
  list.server = "NASA EPIC";
  list.url = LIST;
  ask_every(list, every, status);
  space::today(today, name, image, pixel);
}

}  // namespace flexit::panel::space::earth
