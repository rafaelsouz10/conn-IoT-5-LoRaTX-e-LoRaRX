add_library(aht20 STATIC
    aht20.c
)

target_include_directories(aht20 PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(aht20
    pico_stdlib
    hardware_i2c
)
