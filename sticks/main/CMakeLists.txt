idf_component_register(
        SRCS "main.cpp" "Drumstick.cpp"
    INCLUDE_DIRS 
      "../include"
    REQUIRES 
        esp_adc
        nvs_flash
        esp_wifi
        esp_netif
        freertos
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_gptimer
        driver
        esp_event
        esp_hw_support
        esp_timer
)

set(MAC "000000000000" CACHE STRING "Base MAC, 12 hex digits")
string(LENGTH "${MAC}" MAC_LENGTH)
if(NOT MAC_LENGTH EQUAL 12 OR NOT MAC MATCHES "^[0-9A-Fa-f]+$")
    message(FATAL_ERROR "MAC must contain exactly 12 hexadecimal digits, without colons")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE BH_MAC=0x${MAC}ULL)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_26)

