# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

set(FREERTOS_KERNEL_PATH "E:/Documentos/EMBARCATECH/fase2/FreeRTOS-Kernel")
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

project(estacao-receptor C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Bibliotecas externas
add_subdirectory(lib/ssd1306)
add_subdirectory(lib/sx127x)

# Add executable. Default name is the project name, version 0.1

add_executable(estacao-receptor estacao-receptor.c )

pico_set_program_name(estacao-receptor "estacao-receptor")
pico_set_program_version(estacao-receptor "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(estacao-receptor 0)
pico_enable_stdio_usb(estacao-receptor 1)

# Add the standard include files to the build
target_include_directories(estacao-receptor PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/lib
)

# Add any user requested libraries
target_link_libraries(estacao-receptor 
        pico_stdlib
        FreeRTOS-Kernel 
        FreeRTOS-Kernel-Heap4
        ssd1306
        sx127x
        )

pico_add_extra_outputs(estacao-receptor)

