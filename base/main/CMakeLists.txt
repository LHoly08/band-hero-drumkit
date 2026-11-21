idf_component_register(
    SRCS 
      "main.cpp"
      "Pads.cpp"
      "Playing.cpp"
      "Utils.cpp"
      "Audio.cpp"
      "Config.cpp"
      "Oled.cpp"
    INCLUDE_DIRS "../include" "../../shared"
    REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_timer
             esp_driver_i2c esp_driver_gpio esp_driver_i2s u8g2 freertos
             fatfs sdmmc esp_driver_sdmmc esp_psram
    PRIV_REQUIRES esp_audio_codec esp_audio_effects
)

set(MAC "000000000000" CACHE STRING "Hub MAC, 12 hex digits")
string(LENGTH "${MAC}" MAC_LENGTH)
if(NOT MAC_LENGTH EQUAL 12 OR NOT MAC MATCHES "^[0-9A-Fa-f]+$")
    message(FATAL_ERROR "MAC must contain exactly 12 hexadecimal digits, without colons")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE BH_MAC=0x${MAC}ULL)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_26)

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-freflection>
)

set(web_dir "${CMAKE_CURRENT_SOURCE_DIR}/web")
find_program(web_tsc NAMES tsc
    HINTS "${web_dir}/node_modules/.bin"
    PATHS "$ENV{HOME}/.local/share/nvim/mason/packages/typescript-language-server/node_modules/.bin"
    REQUIRED NO_CACHE NO_CMAKE_FIND_ROOT_PATH
)
add_custom_command(
    OUTPUT "${web_dir}/app.js"
    COMMAND "${web_tsc}" --strict --noEmitOnError --target ES2020 --module ES2020 --moduleDetection legacy
            --outDir "${web_dir}" "${web_dir}/app.ts"
    DEPENDS "${web_dir}/app.ts" "${web_tsc}"
    COMMENT "Compiling the drumkit configurator TypeScript"
    VERBATIM
)
add_custom_target(drumkit_web DEPENDS "${web_dir}/app.js")
add_dependencies(${COMPONENT_LIB} drumkit_web)
