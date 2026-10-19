cmake_minimum_required(VERSION 3.16)
project(stick_hit_test LANGUAGES CXX)

enable_testing()
add_executable(sphere_position_test sphere_position_test.cpp)
target_include_directories(sphere_position_test PRIVATE "../include")
target_compile_features(sphere_position_test PRIVATE cxx_std_20)
add_test(NAME sphere_position COMMAND sphere_position_test)

set(BAND_HERO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../band-hero" CACHE PATH
    "Band Hero checkout containing include/serial/serialib.h and src/serial/serialib.cpp")
if(NOT EXISTS "${BAND_HERO_DIR}/src/serial/serialib.cpp")
  message(FATAL_ERROR "Set BAND_HERO_DIR to your Band Hero checkout")
endif()

add_executable(stick_hit_test main.cpp "${BAND_HERO_DIR}/src/serial/serialib.cpp")
target_include_directories(stick_hit_test PRIVATE "${BAND_HERO_DIR}/include")
target_compile_features(stick_hit_test PRIVATE cxx_std_20)
set_target_properties(stick_hit_test PROPERTIES CXX_EXTENSIONS OFF)
