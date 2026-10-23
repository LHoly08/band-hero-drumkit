idf_component_register(
    SRCS 
      "main.cpp"
      "Pads.cpp"
      "Utils.cpp"
    INCLUDE_DIRS "../include"
    REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_timer
             esp_driver_i2c esp_driver_gpio u8g2 freertos
             fatfs sdmmc esp_driver_sdmmc esp_psram
)

set(MAC "000000000000" CACHE STRING "Hub MAC, 12 hex digits")
string(LENGTH "${MAC}" MAC_LENGTH)
if(NOT MAC_LENGTH EQUAL 12 OR NOT MAC MATCHES "^[0-9A-Fa-f]+$")
    message(FATAL_ERROR "MAC must contain exactly 12 hexadecimal digits, without colons")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE BH_MAC=0x${MAC}ULL)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_26)
