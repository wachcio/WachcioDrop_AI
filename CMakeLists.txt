cmake_minimum_required(VERSION 3.16)

# Komponenty zewnętrzne:
# - esp-idf-lib: ds3231, i2cdev w płaskiej strukturze
# - ssd1306: komponent w podkatalogu components/ssd1306/components/ssd1306
set(EXTRA_COMPONENT_DIRS
    ${CMAKE_SOURCE_DIR}/components/ssd1306/components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(irrigation_controller)
