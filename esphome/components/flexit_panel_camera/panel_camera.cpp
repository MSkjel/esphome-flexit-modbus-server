#include "panel_camera.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::flexit_panel_camera {

static const char *const TAG = "flexit_panel_camera";

void PanelCamera::dump_config() { ESP_LOGCONFIG(TAG, "Flexit panel camera: the screensaver picture"); }

void PanelCamera::start_stream(camera::CameraRequester requester) {
  this->stream_ |= 1U << requester;
  for (auto *listener : this->listeners_)
    listener->on_stream_start();
}

void PanelCamera::stop_stream(camera::CameraRequester requester) {
  this->stream_ &= ~(1U << requester);
  for (auto *listener : this->listeners_)
    listener->on_stream_stop();
}

void PanelCamera::loop() {
  uint32_t now = millis();
  if (!this->bytes_)
    return;
  bool stream = this->stream_ && (this->fresh_ || now - this->sent_ms_ >= STREAM_MS);
  if (this->single_ || stream) {
    auto image = std::make_shared<PanelImage>(this->bytes_, this->single_ | this->stream_);
    this->single_ = 0;
    this->stream_ = 0;
    this->fresh_ = false;
    this->sent_ms_ = now;
    for (auto *listener : this->listeners_)
      listener->on_camera_image(image);
  } else if (!this->stream_ && now - this->sent_ms_ > KEEP_MS) {
    this->bytes_.reset();  // nobody's looking, the connections still sending it keep theirs
  }
}

}  // namespace esphome::flexit_panel_camera
