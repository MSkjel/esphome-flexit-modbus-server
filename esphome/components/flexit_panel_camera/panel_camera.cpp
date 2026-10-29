#include "panel_camera.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

namespace esphome {
namespace flexit_panel_camera {

static const char *const TAG = "flexit_panel_camera";

PanelImage::PanelImage(std::vector<Part> &&parts, uint8_t requesters)
    : parts_(std::move(parts)), requesters_(requesters) {
  for (auto &p : parts_)
    length_ += p->size();
}

void PanelImage::read(size_t at, uint8_t *out, size_t n) const {
  for (auto &p : parts_) {
    if (!n)
      return;
    if (at >= p->size()) {
      at -= p->size();
      continue;
    }
    size_t k = std::min(n, p->size() - at);
    memcpy(out, p->data() + at, k);
    out += k;
    n -= k;
    at = 0;
  }
}

bool PanelImage::whole_() {
  if (!tried_whole_) {
    tried_whole_ = true;
    whole_buf_.reset(static_cast<uint8_t *>(malloc(length_)));  // a new that fails aborts
    if (whole_buf_) {
      read(0, whole_buf_.get(), length_);
    } else {
      ESP_LOGW(TAG, "No RAM for the image in one piece, %u bytes", (unsigned) length_);
    }
  }
  return whole_buf_ != nullptr;
}

// Only our images come here, from our listeners
void PanelImageReader::set_image(std::shared_ptr<camera::CameraImage> image) {
  image_ = std::static_pointer_cast<PanelImage>(std::move(image));
  offset_ = 0;
}

uint8_t *PanelImageReader::peek_data_buffer() {
  image_->read(offset_, chunk_, std::min(available(), sizeof chunk_));
  return chunk_;
}

void PanelCamera::dump_config() { ESP_LOGCONFIG(TAG, "Flexit Panel Camera"); }

// Home Assistant asks for each of a stream's images, and the API stops the stream 5 s after the last ask
void PanelCamera::start_stream(camera::CameraRequester requester) {
  bool started = !stream_;
  stream_ |= 1 << requester;
  stream_asked_ |= 1 << requester;
  if (started)
    for (auto *l : listeners_)
      l->on_stream_start();
}

// An image can take longer than that, the first one of the panel's screen does. It still waits for it then, as one
// image, or the stream would wait for one that never comes.
void PanelCamera::stop_stream(camera::CameraRequester requester) {
  uint8_t bit = 1 << requester;
  if (!(stream_ & bit))
    return;
  single_ |= stream_asked_ & bit;
  stream_ &= ~bit;
  stream_asked_ &= ~bit;
  if (!stream_)
    for (auto *l : listeners_)
      l->on_stream_stop();
}

void PanelCamera::publish(std::vector<Part> &&jpeg) {
  auto image = std::make_shared<PanelImage>(std::move(jpeg), single_ | stream_);
  single_ = stream_asked_ = 0;
  for (auto *l : listeners_)
    l->on_camera_image(image);
}

}  // namespace flexit_panel_camera
}  // namespace esphome
