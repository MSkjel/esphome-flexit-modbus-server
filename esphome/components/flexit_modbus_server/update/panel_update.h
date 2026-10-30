#pragma once

#include "esphome/components/update/update_entity.h"

#include <functional>
#include <string>
#include <utility>

namespace esphome {
namespace flexit_modbus_server {

// Knows nothing about flashing. update.yaml says what to do on a check or an install, and packages/code/panel's
// update_yaml.h what to show.
class PanelUpdate : public update::UpdateEntity {
 public:
  void perform(bool) override {  // the Panel Firmware Force switch is what forces a flash
    if (this->install_)
      this->install_();
  }
  void check() override {
    if (this->check_)
      this->check_();
  }

  void on_check(std::function<void()> f) { this->check_ = std::move(f); }
  void on_install(std::function<void()> f) { this->install_ = std::move(f); }

  // pct < 0 while there's no percentage to show
  void show(const std::string &current, const std::string &latest, bool newer, bool installing, int pct,
            const std::string &summary) {
    bool known = this->state_ == update::UPDATE_STATE_AVAILABLE || this->state_ == update::UPDATE_STATE_INSTALLING;
    this->update_info_.title = "CI600 panel";
    this->update_info_.release_url = "https://github.com/MSkjel/flexit-ci600-firmware/releases";
    this->update_info_.current_version = current;
    this->update_info_.latest_version = latest;
    this->update_info_.summary = summary;
    this->update_info_.has_progress = pct >= 0;
    this->update_info_.progress = pct;
    this->state_ = installing ? update::UPDATE_STATE_INSTALLING
                   : newer    ? update::UPDATE_STATE_AVAILABLE
                              : update::UPDATE_STATE_NO_UPDATE;
    this->publish_state();
    if (this->state_ == update::UPDATE_STATE_AVAILABLE && !known && this->update_available_trigger_)
      this->update_available_trigger_->trigger(this->update_info_);
  }

 protected:
  std::function<void()> check_, install_;
};

}  // namespace flexit_modbus_server
}  // namespace esphome
