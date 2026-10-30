// flexit_panel_camera as the API uses it: who waits for an image, who gets it, and its bytes a chunk at a time.
//   make -C tests camera_test
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include "check.h"
#include "esphome/core/log.h"
#include "panel_camera.h"

using namespace esphome;
using camera::API_REQUESTER;
using camera::WEB_REQUESTER;

// The API's side: the images it was handed, and the streams' starts and stops
struct Api : camera::CameraListener {
  std::vector<std::shared_ptr<camera::CameraImage>> images;
  int starts = 0, stops = 0;
  void on_camera_image(const std::shared_ptr<camera::CameraImage> &image) override { images.push_back(image); }
  void on_stream_start() override { starts++; }
  void on_stream_stop() override { stops++; }
};

static flexit_panel_camera::PanelCamera cam;
static Api api;

// An image of n bytes, byte i is b + i, in parts of 7 that only the image keeps
static void publish(uint8_t b, size_t n = 100) {
  std::vector<flexit_panel_camera::Part> parts;
  for (size_t i = 0; i < n; i += 7) {
    auto p = std::make_shared<std::vector<uint8_t>>();
    for (size_t k = i; k < n && k < i + 7; k++)
      p->push_back((uint8_t) (b + k));
    parts.push_back(p);
  }
  cam.publish(std::move(parts));
}

int main() {
  log_on = getenv("LOG") != nullptr;
  cam.add_listener(&api);
  CHECK(!cam.wanted());

  // One image: wanted until it's made, for the one who asked
  cam.request_image(API_REQUESTER);
  CHECK(cam.wanted());
  publish(1);
  CHECK(!cam.wanted());
  CHECK(api.images.size() == 1);
  CHECK(api.images[0]->was_requested_by(API_REQUESTER) && !api.images[0]->was_requested_by(WEB_REQUESTER));

  // The reader hands out the image across its parts until it's all gone
  std::unique_ptr<camera::CameraImageReader> r{cam.create_image_reader()};
  CHECK(!r->available());
  r->set_image(api.images[0]);
  CHECK(r->available() == 100 && r->peek_data_buffer()[0] == 1 && r->peek_data_buffer()[9] == 10);
  r->consume_data(60);
  CHECK(r->available() == 40 && r->peek_data_buffer()[0] == 61);
  r->consume_data(40);
  CHECK(!r->available());
  r->return_image();
  CHECK(!r->available());

  // A big one as the API reads it, at most 1390 bytes at a time, and in one piece for those that want it so
  cam.request_image(API_REQUESTER);
  publish(7, 5000);
  r->set_image(api.images.back());
  size_t at = 0;
  bool same = true;
  while (size_t n = std::min<size_t>(r->available(), 1390)) {
    const uint8_t *b = r->peek_data_buffer();
    for (size_t i = 0; i < n; i++)
      same &= b[i] == (uint8_t) (7 + at + i);
    r->consume_data(n);
    at += n;
  }
  CHECK(at == 5000 && same);
  auto &big = api.images.back();
  CHECK(big->get_data_length() == 5000 && big->get_data_buffer()[4999] == (uint8_t) (7 + 4999));
  api.images.pop_back();

  // A stream: wanted while it's on, every image for it
  cam.start_stream(API_REQUESTER);
  CHECK(cam.wanted() && api.starts == 1);
  publish(2);
  CHECK(cam.wanted() && api.images.back()->was_requested_by(API_REQUESTER));
  cam.start_stream(API_REQUESTER);  // Home Assistant asks for the next
  publish(3);
  cam.stop_stream(API_REQUESTER);  // 5 s after the last ask, with nothing asked since the image
  CHECK(!cam.wanted() && api.stops == 1);

  // Stopped before its image came: it still waits for one, then it's done
  cam.start_stream(API_REQUESTER);
  cam.stop_stream(API_REQUESTER);
  CHECK(cam.wanted());
  publish(4);
  CHECK(!cam.wanted() && api.images.back()->was_requested_by(API_REQUESTER));
  cam.stop_stream(API_REQUESTER);  // again, as the API's timer may
  CHECK(!cam.wanted() && api.stops == 2);

  // Two asking, one image for both
  cam.request_image(WEB_REQUESTER);
  cam.start_stream(API_REQUESTER);
  publish(5);
  CHECK(api.images.back()->was_requested_by(API_REQUESTER) && api.images.back()->was_requested_by(WEB_REQUESTER));
  cam.stop_stream(API_REQUESTER);
  CHECK(!cam.wanted());
  CHECK(api.images.size() == 5);

  printf("camera_test: %s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
