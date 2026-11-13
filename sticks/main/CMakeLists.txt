idf_component_register(
  SRCS "main.cpp" "Drumstick.cpp"
    INCLUDE_DIRS 
      "../include"
      "../../shared"
    REQUIRES 
        nvs_flash
        esp_wifi
        esp_netif
        freertos
        esp_driver_gpio
        esp_driver_i2c
        driver
        esp_event
        esp_hw_support
        esp_timer
)

option(DEBUG "Enable debug-only ESP-NOW delivery diagnostics" OFF)

set(MAC "000000000000" CACHE STRING "Base MAC, 12 hex digits")
string(LENGTH "${MAC}" MAC_LENGTH)
if(NOT MAC_LENGTH EQUAL 12 OR NOT MAC MATCHES "^[0-9A-Fa-f]+$")
    message(FATAL_ERROR "MAC must contain exactly 12 hexadecimal digits, without colons")
endif()

set(RADIUS "0.275" CACHE STRING "Sphere radius in meters")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/ValidateRadius.cmake")
bh_validate_radius("${RADIUS}" BH_RADIUS_LITERAL)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BH_DEBUG=$<BOOL:${DEBUG}>
    BH_MAC=0x${MAC}ULL
    BH_RADIUS=${BH_RADIUS_LITERAL}
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)

