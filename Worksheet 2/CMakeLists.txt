cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(picocomm C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(picocomm
    main.c
    protocol.c
    flash_ops.c
)

target_include_directories(picocomm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(picocomm
    pico_stdlib
    hardware_flash
    hardware_sync
)

pico_enable_stdio_usb(picocomm 1)
pico_enable_stdio_uart(picocomm 0)

pico_add_extra_outputs(picocomm)
