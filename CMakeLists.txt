# Generated Cmake Pico project file for BLE Beacon

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

project(blink C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable
add_executable(blink
    blink.c
    )

# Pull in common dependencies and BLE support
target_link_libraries(blink 
    pico_stdlib
    pico_cyw43_arch_none
    pico_btstack_ble
    pico_btstack_cyw43
)

# Build BTstack's ATT DB utility (required for att_db_util.h/.c)
target_sources(blink PRIVATE
    ${PICO_SDK_PATH}/lib/btstack/src/ble/att_db_util.c
)

# Include paths so the header is found
target_include_directories(blink PRIVATE
    ${PICO_SDK_PATH}/lib/btstack/src        # for btstack_util.h and friends
    ${PICO_SDK_PATH}/lib/btstack/src/ble    # for att_db_util.h
)

# Tells compiler where btstack_config.h is
target_include_directories(blink PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Enable USB output for printf
pico_enable_stdio_usb(blink 1)
pico_enable_stdio_uart(blink 0)

# Create map/bin/hex file etc.
pico_add_extra_outputs(blink)