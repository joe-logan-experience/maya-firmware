# CMakeLists.txt
cmake_minimum_required(VERSION 3.20.0)

# This locates Zephyr and sets up all include paths/flags/toolchain
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(maya_firmware)

# Print app version string
message(STATUS "App version: ${APP_VERSION_STRING}")

# Add your sources
target_sources(app PRIVATE src/main.c)
