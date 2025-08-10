add_library(sx127x STATIC
    sx127x.c
)

target_include_directories(sx127x PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(sx127x
    pico_stdlib
    hardware_spi 
    hardware_gpio
)
