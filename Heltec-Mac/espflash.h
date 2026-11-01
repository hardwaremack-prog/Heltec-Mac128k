// FlashArea on an ESP32 data partition (see partitions.csv)
#pragma once
#include <esp_partition.h>
#include <spi_flash_mmap.h>
#include "macmachine.h"

struct PartitionArea : FlashArea {
  const esp_partition_t* p = nullptr;
  esp_partition_mmap_handle_t h = 0;
  const void* mapped = nullptr;

  bool open(const char* label) {
    p = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, label);
    return p != nullptr;
  }
  size_t size() const override { return p ? p->size : 0; }
  bool read(uint32_t off, void* buf, size_t n) override { return esp_partition_read(p, off, buf, n) == ESP_OK; }
  bool write(uint32_t off, const void* buf, size_t n) override { return esp_partition_write(p, off, buf, n) == ESP_OK; }
  bool erase(uint32_t off, size_t n) override { return esp_partition_erase_range(p, off, n) == ESP_OK; }
  const uint8_t* map(uint32_t off, size_t n) override {
    unmap();
    if (esp_partition_mmap(p, off, n, ESP_PARTITION_MMAP_DATA, &mapped, &h) != ESP_OK) { mapped = nullptr; return nullptr; }
    return (const uint8_t*)mapped;
  }
  void unmap() override {
    if (mapped) { esp_partition_munmap(h); mapped = nullptr; }
  }
};
