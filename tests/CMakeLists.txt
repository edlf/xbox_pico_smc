# Host tests: the SMC state machines built for the PC, on a fake HAL and a few
# fake Pico SDK headers, with GoogleTest. Not part of the firmware build.
# Also builds og_smc_sim, the firmware's main loop running on the PC with
# models of the Xbox around it (sim/).
#
#   cmake -S tests -B build-tests
#   cmake --build build-tests
#   ctest --test-dir build-tests --output-on-failure
#
# Needs GoogleTest installed (MSYS2: pacman -S gtest). Uses the default
# firmware settings from src/config.hpp (P11).
cmake_minimum_required(VERSION 3.13)
project(og_smc_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(GTest REQUIRED)
find_package(Threads REQUIRED)
include(GoogleTest)

set(SMC_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../src)

# The firmware's own modules on the fakes, and the simulator
add_library(og_smc_host STATIC
  # Fakes
  fake_hal.cpp
  fake_debug.cpp
  fake_sdk.cpp
  fake_flash.cpp
  # The firmware's own modules
  ${SMC_SRC_DIR}/smc.cpp
  ${SMC_SRC_DIR}/smc_SMI.cpp
  ${SMC_SRC_DIR}/smc_audio_clamp.cpp
  ${SMC_SRC_DIR}/smc_boot_challenge.cpp
  ${SMC_SRC_DIR}/smc_dvd.cpp
  ${SMC_SRC_DIR}/smc_encoder_mode.cpp
  ${SMC_SRC_DIR}/smc_extra_temp.cpp
  ${SMC_SRC_DIR}/smc_fan.cpp
  ${SMC_SRC_DIR}/smc_fan_pic.cpp
  ${SMC_SRC_DIR}/smc_front_panel_sw.cpp
  ${SMC_SRC_DIR}/smc_led.cpp
  ${SMC_SRC_DIR}/smc_pll_reset.cpp
  ${SMC_SRC_DIR}/smc_power_standby.cpp
  ${SMC_SRC_DIR}/smc_rgb_led.cpp
  ${SMC_SRC_DIR}/smc_settings.cpp
  ${SMC_SRC_DIR}/smc_i2c_decoder.cpp
  ${SMC_SRC_DIR}/smc_smbus.cpp
  ${SMC_SRC_DIR}/smc_video.cpp
  # Simulator
  sim/simulator.cpp
)
# The fake SDK headers come first, so <pico/...> and <hardware/...> resolve to them
target_include_directories(og_smc_host PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/fake_sdk ${CMAKE_CURRENT_LIST_DIR}/sim ${SMC_SRC_DIR})
target_compile_options(og_smc_host PUBLIC -Wall -Wextra)
target_link_libraries(og_smc_host PUBLIC Threads::Threads)

add_executable(og_smc_tests
  test_encoder_mode.cpp
  test_smbus.cpp
  test_front_panel.cpp
  test_dvd.cpp
  test_led.cpp
  test_ring_buffer.cpp
  test_settings.cpp
  test_i2c_decoder.cpp
  test_reset_on_eject.cpp
  test_scenarios.cpp
)
target_link_libraries(og_smc_tests PRIVATE og_smc_host GTest::gtest_main)

add_executable(og_smc_sim sim/sim_main.cpp)
target_link_libraries(og_smc_sim PRIVATE og_smc_host)

enable_testing()
gtest_discover_tests(og_smc_tests)
