# CMakeLists.txt
cmake_minimum_required(VERSION 3.20.0)

# Set device tree overlay file for LED0 (must happen before find_package)
set(DTC_OVERLAY_FILE ${CMAKE_SOURCE_DIR}/boards/esp32_devkitc.overlay)

# This locates Zephyr and sets up all include paths/flags/toolchain
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(maya_firmware)

# Add your sources
target_sources(app PRIVATE src/main.c)
