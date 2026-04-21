cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(my_blink C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(my_blink
  main.c
  flash_ops.c
  filesystem.c
)

pico_enable_stdio_usb(my_blink 1)
pico_enable_stdio_uart(my_blink 0)

pico_add_extra_outputs(my_blink)

target_link_libraries(my_blink pico_stdlib)
