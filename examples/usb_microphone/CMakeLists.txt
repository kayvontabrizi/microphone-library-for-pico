cmake_minimum_required(VERSION 3.12)

# rest of your project
add_executable(usb_microphone
    main.c
    usb_descriptors.c
    usb_microphone.c
)

target_include_directories(usb_microphone PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(usb_microphone PRIVATE tinyusb_device tinyusb_board pico_pdm_microphone)

# create map/bin/hex/uf2 file in addition to ELF.
pico_add_extra_outputs(usb_microphone)

# this is apparently necessary for debugging
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    pico_enable_stdio_usb(usb_microphone 0)
    pico_enable_stdio_uart(usb_microphone 1)
endif (CMAKE_BUILD_TYPE STREQUAL "Debug")