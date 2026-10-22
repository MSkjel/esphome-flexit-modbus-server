// ESP-IDF's partition API, enough for update.h on a PC
#pragma once
#include <cstddef>
#include <cstdint>

typedef int esp_err_t;
#define ESP_OK 0

struct esp_partition_t {
  size_t size, erase_size;
};

esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *dst, size_t len);
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t off, size_t len);
esp_err_t esp_partition_write(const esp_partition_t *p, size_t off, const void *src, size_t len);
