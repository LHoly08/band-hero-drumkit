cmake_minimum_required(VERSION 3.31)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IDF_TARGET "esp32s3" CACHE STRING "Target ESP chip")
if(IDF_TARGET STREQUAL "esp32")
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig.esp32" CACHE FILEPATH "Classic ESP32 configuration")
    set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.classic.defaults")
endif()

# Use the checked-in sdkconfig.defaults; download dependencies into the build directory.
include(FetchContent)
option(ESP32_CAM_SD "Use the AI-Thinker ESP32-CAM onboard microSD slot" OFF)
if(ESP32_CAM_SD AND NOT IDF_TARGET STREQUAL "esp32")
    message(FATAL_ERROR "ESP32_CAM_SD requires IDF_TARGET=esp32")
endif()
option(OLED "Enable the drum base OLED display" ON)
if(OLED)
    # Same OLED component/revision used by band-hero-hub (nixy4/u8g2 0.1.4).
    FetchContent_Declare(bh_u8g2
        GIT_REPOSITORY https://github.com/Nixy4/u8g2.git
        GIT_TAG ce5c8623d477bce4a10a84b079039b342d217dce
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/u8g2"
        SOURCE_SUBDIR _populate_only
    )
    FetchContent_MakeAvailable(bh_u8g2)
    list(APPEND EXTRA_COMPONENT_DIRS "${bh_u8g2_SOURCE_DIR}")
endif()
FetchContent_Declare(bh_lua
    URL https://www.lua.org/ftp/lua-5.5.0.tar.gz
    URL_HASH SHA256=57ccc32bbbd005cab75bcc52444052535af691789dba2b9016d5c50640d68b3d
    SOURCE_SUBDIR _populate_only
)
FetchContent_MakeAvailable(bh_lua)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(IDF_TARGET STREQUAL "esp32")
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_BINARY_DIR}/dependencies.lock")
endif()

project(band-hero)

if(OLED)
    # The pinned u8g2 port uses GPIO directly, but only declares the legacy driver
    # component, which no longer exports GPIO headers in ESP-IDF 6.
    idf_component_get_property(BH_U8G2_LIB u8g2 COMPONENT_LIB)
    target_link_libraries(${BH_U8G2_LIB} PRIVATE idf::esp_driver_gpio)

    # The SH1106 family glob misses the shared SSD1306/SH1106 128x64 driver.
    if(CONFIG_U8G2_DISPLAY_DRIVER_SH1106)
        target_sources(${BH_U8G2_LIB} PRIVATE
            "${bh_u8g2_SOURCE_DIR}/src/driver/u8x8_d_ssd1306_128x64_noname.c")
    endif()
endif()

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
