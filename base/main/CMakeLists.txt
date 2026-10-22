idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "../include"
    REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_timer
             esp_driver_i2c esp_driver_gpio u8g2 freertos
             fatfs sdmmc esp_driver_sdmmc
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_26)
