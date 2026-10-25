#include "sysInfo.h"
#include "config.h"

#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"

static const char *tag = "sysInfo";

namespace sysInfo {

void printBanner()
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flashBytes = 0;
    esp_flash_get_size(nullptr, &flashBytes);

    ESP_LOGI(tag, "%s v%s on %s", config::osName, config::osVersion, board::name);
    ESP_LOGI(tag, "chip: %s | cores: %d | rev: %d", CONFIG_IDF_TARGET, chip.cores, chip.revision);
    ESP_LOGI(tag, "flash: %u MB", (unsigned)(flashBytes / (1024 * 1024)));

    ESP_LOGI(tag, "internal ram: %u KB free, largest block %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));

    ESP_LOGI(tag, "psram: %u KB free, largest block %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024));
}

} // namespace sysInfo