// The panel's screensaver picture as a camera for Home Assistant. It knows nothing about pictures:
// packages/code/panel/snapshot.h makes a JPEG when wanted() and hands it over with publish(). That's kept a while
// for the requests that follow, then freed.
#pragma once
#include <memory>
#include <vector>
#include "esphome/components/camera/camera.h"
#include "esphome/core/component.h"

namespace esphome::flexit_panel_camera {

using Jpeg = std::shared_ptr<const std::vector<uint8_t>>;

// One JPEG for the requesters in a mask, shared by the connections sending it
class PanelImage : public camera::CameraImage {
 public:
  PanelImage(Jpeg jpeg, uint8_t requesters) : jpeg_(std::move(jpeg)), requesters_(requesters) {}
  uint8_t *get_data_buffer() override { return const_cast<uint8_t *>(this->jpeg_->data()); }
  size_t get_data_length() override { return this->jpeg_->size(); }
  bool was_requested_by(camera::CameraRequester requester) const override {
    return this->requesters_ & (1U << requester);
  }

 protected:
  Jpeg jpeg_;
  uint8_t requesters_;
};

class PanelImageReader : public camera::CameraImageReader {
 public:
  void set_image(std::shared_ptr<camera::CameraImage> image) override {
    this->image_ = std::move(image);
    this->offset_ = 0;
  }
  size_t available() const override { return this->image_ ? this->image_->get_data_length() - this->offset_ : 0; }
  uint8_t *peek_data_buffer() override { return this->image_->get_data_buffer() + this->offset_; }
  void consume_data(size_t consumed) override { this->offset_ += consumed; }
  void return_image() override { this->image_.reset(); }

 protected:
  std::shared_ptr<camera::CameraImage> image_;
  size_t offset_{0};
};

class PanelCamera : public camera::Camera {
 public:
  static constexpr uint32_t KEEP_MS = 30000;  // the JPEG is freed this long after it was last sent
  // A stream asks again as soon as it has one, so the same JPEG goes this often
  static constexpr uint32_t STREAM_MS = 2000;

  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void add_listener(camera::CameraListener *listener) override { this->listeners_.push_back(listener); }
  camera::CameraImageReader *create_image_reader() override { return new PanelImageReader; }
  void request_image(camera::CameraRequester requester) override { this->single_ |= 1U << requester; }
  void start_stream(camera::CameraRequester requester) override;
  void stop_stream(camera::CameraRequester requester) override;

  // For snapshot.h: a request waits for a JPEG
  bool wanted() const { return (this->single_ || this->stream_) && !this->jpeg_; }
  // The JPEG of what the panel shows, sent to the requests in loop()
  void publish(Jpeg jpeg) {
    this->jpeg_ = std::move(jpeg);
    this->fresh_ = true;
  }
  // What the panel shows changed
  void invalidate() { this->jpeg_.reset(); }

 protected:
  std::vector<camera::CameraListener *> listeners_;
  Jpeg jpeg_;
  uint32_t sent_ms_{0};
  uint8_t single_{0}, stream_{0};  // requesters waiting, a bit for each
  bool fresh_{false};              // jpeg_ hasn't been sent yet
};

}  // namespace esphome::flexit_panel_camera
