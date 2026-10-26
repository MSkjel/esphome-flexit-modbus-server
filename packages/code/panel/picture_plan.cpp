#include "picture_plan.h"
#include <algorithm>
#include <cstring>

namespace flexit::panel::picture {

uint32_t patches(const Pic &made, const Pic &shown, std::vector<Job> &jobs) {
  auto differs = [&](int r) {
    size_t a = made.rows[r + 1] - made.rows[r], b = shown.rows[r + 1] - shown.rows[r];
    return a != b || memcmp(made.data.data() + made.rows[r], shown.data.data() + shown.rows[r], a) != 0;
  };
  // The panel keeps a piece behind the picture in its memory until it's applied, so it has to fit there.
  // size is the panel's picture with the earlier pieces applied. Unchanged rows up to GAP between changed ones go
  // along: deflated against the panel's picture they cost a few bytes, another piece a round trip.
  static constexpr int GAP = 16;
  uint32_t bytes = 0, size = shown.data.size();
  bool fits = true;
  for (int r = 0; r < made.h;) {
    if (!differs(r)) {
      r++;
      continue;
    }
    int s = r, end = r;  // end: past the last changed row in this piece
    for (; r < made.h && r - end < GAP && size + (made.rows[r + 1] - made.rows[s]) <= PIC_POOL; r++)
      if (differs(r))
        end = r + 1;
    r = end;
    fits = end > s;
    if (!fits)
      break;  // not even one row fits, send the whole picture
    jobs.push_back({false, false, (uint16_t) s, (uint16_t) (end - s)});
    bytes += made.rows[end] - made.rows[s];
    size += (made.rows[end] - made.rows[s]) - (shown.rows[end] - shown.rows[s]);
  }
  if (memcmp(made.pal, shown.pal, sizeof made.pal)) {
    // The panel takes new colours at their piece's END, so rows a later piece changes would show in them with
    // their old indices till it comes. One piece from the first changed row to the last then, if it fits.
    if (jobs.size() > 1) {
      uint16_t s = jobs.front().y, end = jobs.back().y + jobs.back().count;
      bytes = made.rows[end] - made.rows[s];
      fits = shown.data.size() + bytes <= PIC_POOL;
      jobs.assign(1, {false, false, s, (uint16_t) (end - s)});
    }
    if (jobs.empty())
      jobs.push_back({false, true, 0, 0});  // only the colours changed
    else
      jobs[0].pal = true;
  }
  if (!fits)
    jobs.assign(1, {true, true, 0, made.h});
  return bytes;
}

void apply(Pic &shown, const Pic &made, const Job &j) {
  Pic &p = shown;
  const Pic &m = made;
  uint32_t a = p.rows[j.y], b = p.rows[j.y + j.count];
  int32_t delta = (int32_t) (m.rows[j.y + j.count] - m.rows[j.y]) - (int32_t) (b - a);
  p.data.erase(p.data.begin() + a, p.data.begin() + b);
  p.data.insert(p.data.begin() + a, m.data.begin() + m.rows[j.y], m.data.begin() + m.rows[j.y + j.count]);
  for (unsigned r = j.y + 1u; r <= j.y + j.count; r++)
    p.rows[r] = (uint16_t) (a + m.rows[r] - m.rows[j.y]);
  for (unsigned r = j.y + j.count + 1u; r <= p.h; r++)
    p.rows[r] = (uint16_t) (p.rows[r] + delta);
  if (j.pal)
    memcpy(p.pal, m.pal, sizeof p.pal);
}

// pzip's like() for a patch: the same place in the row it replaces on the panel
static int32_t like_row(void *ctx, uint32_t i) {
  Zip &z = *(Zip *) ctx;
  const Pic &m = *z.made, &p = *z.shown;
  while (z.row + 1u < m.h && m.rows[z.row + 1] <= z.at + i)
    z.row++;
  uint32_t at = p.rows[z.row] + (z.at + i - m.rows[z.row]);
  return at < p.rows[z.row + 1] ? (int32_t) at : -1;
}

void Zip::begin(const Pic &m, const Pic &s, const Job &j, uint32_t from, uint32_t len) {
  made = &m;
  shown = &s;
  at = from;
  z = {j.full ? nullptr : s.data.data(),
       j.full ? 0 : (uint32_t) s.data.size(),
       m.data.data() + from,
       len,
       0,
       table.empty() ? nullptr : table.data(),
       0,
       j.full ? nullptr : like_row,
       this};
  n = 0;
  row = j.y;
}

unsigned Zip::next(uint8_t *b, unsigned max) {
  for (unsigned k; n < max && (k = pzip(&z, buf + n));)
    n += k;
  unsigned k = std::min(n, max);
  memcpy(b, buf, k);
  memmove(buf, buf + k, n - k);
  n -= k;
  return k;
}

}  // namespace flexit::panel::picture
