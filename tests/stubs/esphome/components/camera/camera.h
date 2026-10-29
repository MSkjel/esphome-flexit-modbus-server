// The part of ESPHome's camera.h that flexit_panel_camera uses
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include "esphome/core/component.h"

namespace esphome::camera {

enum CameraRequester : uint8_t { IDLE, API_REQUESTER, WEB_REQUESTER };

class CameraImage {
 public:
  virtual uint8_t *get_data_buffer() = 0;
  virtual size_t get_data_length() = 0;
  virtual bool was_requested_by(CameraRequester requester) const = 0;
  virtual ~CameraImage() {}
};

class CameraListener {
 public:
  virtual void on_camera_image(const std::shared_ptr<CameraImage> &) {}
  virtual void on_stream_start() {}
  virtual void on_stream_stop() {}
};

class CameraImageReader {
 public:
  virtual void set_image(std::shared_ptr<CameraImage> image) = 0;
  virtual size_t available() const = 0;
  virtual uint8_t *peek_data_buffer() = 0;
  virtual void consume_data(size_t consumed) = 0;
  virtual void return_image() = 0;
  virtual ~CameraImageReader() {}
};

class Camera : public Component {
 public:
  virtual void add_listener(CameraListener *listener) = 0;
  virtual CameraImageReader *create_image_reader() = 0;
  virtual void request_image(CameraRequester requester) = 0;
  virtual void start_stream(CameraRequester requester) = 0;
  virtual void stop_stream(CameraRequester requester) = 0;
};

}  // namespace esphome::camera
