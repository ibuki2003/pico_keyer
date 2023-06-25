cmake_minimum_required(VERSION 3.13)

# initialize the SDK directly
include($ENV{PICO_SDK_PATH}/pico_sdk_init.cmake)
project(keyer)

# initialize the Raspberry Pi Pico SDK
pico_sdk_init()

file(GLOB SRC_FILES
    src/*.c
    src/**/*.c
    )

add_executable(keyer ${SRC_FILES})

target_include_directories(keyer PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

# Add pico_stdlib library which aggregates commonly used features
target_link_libraries(keyer PRIVATE
  pico_stdlib
  pico_stdio
  hardware_irq
  )

target_compile_definitions(keyer PRIVATE
  USE_USB=1
  )

pico_enable_stdio_usb(keyer 1)
pico_enable_stdio_uart(keyer 0)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(keyer)
