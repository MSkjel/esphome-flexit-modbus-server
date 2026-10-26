// The camera's JPEG of a 16-colour picture: rows unpacked as plink_pack_row packed them, and a whole JPEG out.
//   snapshot_test [out.jpg]  writes the test picture's JPEG to look at
#include <cstdio>
#include <cstdlib>
#include "check.h"
#include "panel/picture_snap.h"
#include "panel/plink.h"

using namespace flexit::panel::picture;

// Rows with runs and literals of every length, packed and unpacked again
static void round_trip() {
  srand(1);
  for (int t = 0; t < 2000; t++) {
    size_t n = 1 + rand() % 200;
    std::vector<uint8_t> row(n), packed(n + n / 128 + 1), back(n);
    for (size_t i = 0; i < n;) {
      size_t run = 1 + rand() % (rand() % 4 ? 3 : 140);
      uint8_t v = (uint8_t) rand();
      for (size_t k = 0; k < run && i < n; k++, i++)
        row[i] = rand() % 3 ? v : (uint8_t) rand();
    }
    packed.resize(plink_pack_row(row.data(), (unsigned) n, packed.data()));
    unpack_row(packed.data(), back.data(), n);
    CHECK(back == row);
  }
}

// A w x h picture in 16 colours: bands of colour with a diagonal across them
static Pic picture(int w, int h) {
  Pic p;
  p.w = (uint16_t) w;
  p.h = (uint16_t) h;
  for (int i = 0; i < 16; i++)
    p.pal[i] = (uint32_t) (i * 16) << 16 | (uint32_t) (255 - i * 16) << 8 | (uint32_t) (i * 40 % 256);
  size_t row_n = (w + 1) / 2;
  std::vector<uint8_t> row(row_n);
  p.rows.push_back(0);
  for (int y = 0; y < h; y++) {
    for (size_t k = 0; k < row_n; k++) {
      int x = (int) k * 2;
      auto idx = [&](int x) { return (uint8_t) (abs(x - y) < 3 ? 15 : y * 15 / h); };
      row[k] = (uint8_t) (idx(x) << 4 | idx(x + 1));
    }
    size_t at = p.data.size();
    p.data.resize(at + row_n + row_n / 128 + 1);
    p.data.resize(at + plink_pack_row(row.data(), (unsigned) row_n, p.data.data() + at));
    p.rows.push_back((uint16_t) p.data.size());
  }
  return p;
}

static uint32_t encode(const Pic *p, int quality, std::vector<uint8_t> &out) {
  Snap s;
  s.start(p, quality, out);
  while (s.step()) {
  }
  return s.finish();
}

static bool jpeg(const std::vector<uint8_t> &j, uint32_t n) {
  return n >= 4 && n <= j.size() && j[0] == 0xFF && j[1] == 0xD8 && j[n - 2] == 0xFF && j[n - 1] == 0xD9;
}

int main(int argc, char **argv) {
  round_trip();

  Pic p = picture(320, 240);
  std::vector<uint8_t> out(24 * 1024);
  uint32_t n = encode(&p, 85, out);
  CHECK(jpeg(out, n));
  printf("320x240 in 16 colours: %u bytes at quality 85\n", (unsigned) n);
  if (argc > 1) {
    FILE *f = fopen(argv[1], "wb");
    fwrite(out.data(), 1, n, f);
    fclose(f);
  }

  Pic small = picture(300, 170);  // padded to whole MCUs, placed as Search does
  n = encode(&small, 85, out);
  CHECK(jpeg(out, n));

  std::vector<uint8_t> tiny(512);  // too small: said so, not written past
  CHECK(encode(&p, 85, tiny) > tiny.size());

  std::vector<uint8_t> black(4096);
  n = encode(nullptr, 50, black);
  CHECK(jpeg(black, n));
  printf("black screen: %u bytes\n", (unsigned) n);

  printf(fails ? "%d failed\n" : "ok\n", fails);
  return fails != 0;
}
