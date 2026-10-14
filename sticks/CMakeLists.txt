cmake_minimum_required(VERSION 3.31)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IDF_TARGET "esp32c6" CACHE STRING "Target ESP chip" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(band-hero)

# The pinned BNO085 component lists only the legacy driver umbrella dependency.
# Explicitly link the split drivers needed by its public headers on ESP-IDF 6.
idf_component_get_property(BNO085_LIB rinku404__bno085 COMPONENT_LIB)
target_link_libraries(${BNO085_LIB} PUBLIC idf::esp_driver_i2c idf::esp_driver_gpio)
