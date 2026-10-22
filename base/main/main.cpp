#include <cstdio>

#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include "driver/sdmmc_host.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main() {
  ::sdmmc_host_t host = SDMMC_HOST_DEFAULT();

  ::sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  ::esp_vfs_fat_sdmmc_mount_config_t config{};
  config.format_if_mount_failed = false;
  config.max_files = 5;
  config.allocation_unit_size = 16 * 1024;

  ::sdmmc_card_t *card = nullptr;
  ::esp_err_t err =
      ::esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot, &config, &card);

  if (err != ESP_OK) {
    std::printf("SD mount failed: %s\n", ::esp_err_to_name(err));
    return;
  }

  ::sdmmc_card_print_info(stdout, card);

  // Append a line each time the board boots.
  std::FILE *file = std::fopen("/sdcard/test.txt", "a");
  if (file) {
    if (std::fputs("Hello from ESP32-CAM!\n", file) < 0)
      std::perror("Write failed");

    if (std::fclose(file) != 0)
      std::perror("Close failed");
  } else {
    std::perror("Open failed");
  }

  ESP_ERROR_CHECK(::esp_vfs_fat_sdcard_unmount("/sdcard", card));
}
