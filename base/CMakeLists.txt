cmake_minimum_required(VERSION 3.31)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IDF_TARGET "esp32" CACHE STRING "Target ESP chip")

# Keep board defaults and downloaded dependencies in the build directory.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(BH_SDKCONFIG_DEFAULTS "${CMAKE_BINARY_DIR}/sdkconfig.board.defaults")
if(IDF_TARGET STREQUAL "esp32")
    # Keep the ESP32 test configuration separate from the existing S3 sdkconfig.
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig.esp32" CACHE FILEPATH "SDK configuration")
    file(WRITE "${BH_SDKCONFIG_DEFAULTS}" "\
CONFIG_IDF_TARGET=\"esp32\"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y
# CONFIG_SPIRAM is not set
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
")
elseif(IDF_TARGET STREQUAL "esp32s3")
    file(WRITE "${BH_SDKCONFIG_DEFAULTS}" "\
CONFIG_IDF_TARGET=\"esp32s3\"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
")
else()
    message(FATAL_ERROR "Unsupported target: ${IDF_TARGET}. Use esp32 or esp32s3.")
endif()
file(APPEND "${BH_SDKCONFIG_DEFAULTS}" "\
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_U8G2_DISPLAY_DRIVER_SH1106=y
")
set(SDKCONFIG_DEFAULTS "${BH_SDKCONFIG_DEFAULTS}")

include(FetchContent)
# Same OLED component/revision used by band-hero-hub (nixy4/u8g2 0.1.4).
FetchContent_Declare(bh_u8g2
    GIT_REPOSITORY https://github.com/Nixy4/u8g2.git
    GIT_TAG ce5c8623d477bce4a10a84b079039b342d217dce
    SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/u8g2"
    SOURCE_SUBDIR _populate_only
)
FetchContent_Declare(bh_lua
    URL https://www.lua.org/ftp/lua-5.5.0.tar.gz
    URL_HASH SHA256=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d
    SOURCE_SUBDIR _populate_only
)
FetchContent_MakeAvailable(bh_u8g2 bh_lua)
list(APPEND EXTRA_COMPONENT_DIRS "${bh_u8g2_SOURCE_DIR}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(band-hero)

# The pinned u8g2 port uses GPIO directly, but only declares the legacy driver
# component, which no longer exports GPIO headers in ESP-IDF 6.
idf_component_get_property(BH_U8G2_LIB u8g2 COMPONENT_LIB)
target_link_libraries(${BH_U8G2_LIB} PRIVATE idf::esp_driver_gpio)

idf_build_set_property(COMPILE_OPTIONS "-O2" APPEND)

# Embed the Lua C API and standard libraries, excluding the CLI tools.
file(GLOB BH_LUA_SOURCES CONFIGURE_DEPENDS "${bh_lua_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM BH_LUA_SOURCES
    "${bh_lua_SOURCE_DIR}/src/lua.c"
    "${bh_lua_SOURCE_DIR}/src/luac.c"
)
add_library(bh_lua STATIC ${BH_LUA_SOURCES})
target_include_directories(bh_lua PUBLIC "${bh_lua_SOURCE_DIR}/src")
target_compile_definitions(bh_lua PUBLIC LUA_32BITS=1)
target_compile_options(bh_lua PRIVATE -O2)
if(TARGET idf::esp_libc)
    target_link_libraries(bh_lua PUBLIC idf::esp_libc m)
else()
    target_link_libraries(bh_lua PUBLIC idf::newlib m)
endif()

idf_component_get_property(BH_MAIN_LIB main COMPONENT_LIB)
target_link_libraries(${BH_MAIN_LIB} PUBLIC bh_lua)
