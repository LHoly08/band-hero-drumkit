cmake_minimum_required(VERSION 3.31)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IDF_TARGET "esp32s3" CACHE STRING "Target ESP chip" FORCE)

# Keep board defaults and downloaded dependencies in the build directory.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}")
set(BH_SDKCONFIG_DEFAULTS "${CMAKE_BINARY_DIR}/sdkconfig.xiao.defaults")
file(WRITE "${BH_SDKCONFIG_DEFAULTS}" "\
CONFIG_IDF_TARGET=\"esp32s3\"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
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
