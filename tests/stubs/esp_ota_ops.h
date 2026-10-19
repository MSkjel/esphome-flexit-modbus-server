// ESP-IDF's OTA API, enough for update.h on a PC
#pragma once
#include "esp_partition.h"

typedef enum { ESP_OTA_IMG_VALID, ESP_OTA_IMG_PENDING_VERIFY } esp_ota_img_states_t;

const esp_partition_t *esp_ota_get_running_partition();
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *);
inline esp_err_t esp_ota_get_state_partition(const esp_partition_t *, esp_ota_img_states_t *s) {
  *s = ESP_OTA_IMG_VALID;
  return ESP_OK;
}
