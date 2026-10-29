#pragma once

#include "esphome/components/camera/camera.h"
#include "esphome/core/component.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <vector>

namespace esphome {
namespace flexit_panel_camera {

// A part of an image. One that changes for the next image is a new part, so an image being sent keeps its own.
using Part = std::shared_ptr<const std::vector<uint8_t>>;

// A JPEG in parts, shared by the connections sending it. The API reads it a chunk at a time, so it never has to be
// in one piece, which a big image may not find the RAM for.
class PanelImage : public camera::CameraImage {
 public:
  PanelImage(std::vector<Part> &&parts, uint8_t requesters);
  // In one piece, made on the first ask, for those that want it so (esp32_camera_web_server). Empty without the RAM.
  uint8_t *get_data_buffer() override { return whole_() ? whole_buf_.get() : nullptr; }
  size_t get_data_length() override { return whole_() ? length_ : 0; }
  bool was_requested_by(camera::CameraRequester r) const override { return requesters_ & (1 << r); }

  size_t length() const { return length_; }
  void read(size_t at, uint8_t *out, size_t n) const;  // n bytes from at on

 protected:
  bool whole_();
  std::vector<Part> parts_;
  size_t length_{0};
  uint8_t requesters_;
  bool tried_whole_{false};
  std::unique_ptr<uint8_t, decltype(&free)> whole_buf_{nullptr, free};
};

class PanelImageReader : public camera::CameraImageReader {
 public:
  void set_image(std::shared_ptr<camera::CameraImage> image) override;
  size_t available() const override { return image_ ? image_->length() - offset_ : 0; }
  uint8_t *peek_data_buffer() override;
  void consume_data(size_t consumed) override { offset_ += consumed; }
  void return_image() override { image_.reset(); }

 protected:
  std::shared_ptr<PanelImage> image_;
  size_t offset_{0};
  uint8_t chunk_[1500];  // the API takes at most an MTU's worth at a time, its MAX_BATCH_PACKET_SIZE is 1390
};

// A camera for images made elsewhere, the panel's screen in packages/panel/screen_camera.yaml. wanted() tells the
// maker someone waits, publish() hands over the JPEG for them.
class PanelCamera : public camera::Camera {
 public:
  void dump_config() override;
  void add_listener(camera::CameraListener *listener) override { listeners_.push_back(listener); }
  camera::CameraImageReader *create_image_reader() override { return new PanelImageReader; }
  void request_image(camera::CameraRequester requester) override { single_ |= 1 << requester; }
  void start_stream(camera::CameraRequester requester) override;
  void stop_stream(camera::CameraRequester requester) override;

  bool wanted() const { return single_ || stream_; }
  void publish(std::vector<Part> &&jpeg);

 protected:
  std::vector<camera::CameraListener *> listeners_;
  // Bit 1 << requester for each that waits for one image, streams, and asked for the stream's next
  uint8_t single_{0}, stream_{0}, stream_asked_{0};
};

}  // namespace flexit_panel_camera
}  // namespace esphome
