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
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(OG_SMC C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add I2C slave library
add_subdirectory(i2c_slave)


# Add executable. Default name is the project name, version 0.1
add_executable(OG_SMC main.cpp pico_hal.cpp smc.cpp smc_debug.cpp)


pico_set_program_name(OG_SMC "OG_SMC")
pico_set_program_version(OG_SMC "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(OG_SMC 1)
pico_enable_stdio_usb(OG_SMC 0)

# Add the standard library to the build
target_link_libraries(OG_SMC
        pico_stdlib
        hardware_pwm)

# Add the standard include files to the build
target_include_directories(OG_SMC PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)



# Add any user requested libraries
target_link_libraries(OG_SMC
        hardware_i2c
        hardware_watchdog
        i2c_slave
        )

pico_add_extra_outputs(OG_SMC)
