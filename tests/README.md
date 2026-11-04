# Host tests

The SMC's state machines and SMBus handling, built for the PC and run without
a Pico. The hardware is a fake HAL (`fake_hal.cpp`) whose pins are plain
variables, a few fake Pico SDK headers (`fake_sdk/`) and a debug log that
records every line (`fake_debug.cpp`). The tests use GoogleTest fixtures
(`TEST_F`).

## Running them

Any GCC or Clang with C++17 and GoogleTest works. With MSYS2, install
GoogleTest once (`pacman -S gtest` in the MSYS shell), then from the MSYS shell,
or with `D:\msys64\usr\bin` first on `PATH`:

```
cmake -S tests -B build-tests -G "Unix Makefiles"
cmake --build build-tests -j 8
ctest --test-dir build-tests --output-on-failure
```

Or run `build-tests/og_smc_tests.exe` directly to see every test by name;
`--gtest_filter=SmbusTest.*` runs one fixture.

They build with the default settings from `src/config.hpp` (P11).

## What's covered

- `test_encoder_mode.cpp`: video mode decoding from encoder writes (Xcalibur
  rasters, Conexant and Focus SD/HD, the X-HD mode packet), and that the mode
  is only logged once a mode set settles and when it changes.
- `test_front_panel.cpp`: button debounce (two readings), a held button counting
  once, glitches, and the power/eject interlock.
- `test_smbus.cpp`: the SMBus commands, sent through `handle_SMBus_interrupt()`
  as the I2C slave does: version string, reset/power requests, fan override,
  temperatures, OS answers (0x0D), interrupt reason, RAM test rule,
  reset-on-eject, scratch, and the extra commands 0x30-0x34.

## Adding tests

Each file has a fixture whose `SetUp()` resets the fakes and the modules it
uses. A test sets inputs, runs ticks and checks outputs:

```cpp
class FrontPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    SMC::globals_init();
    SMC::FrontPanelSW::init();
  }
};

TEST_F(FrontPanelTest, EjectPressCountsAfterTwoReadings) {
  fake::hal.eject_button = true;
  SMC::FrontPanelSW::update();  // 2 ticks
  SMC::FrontPanelSW::update();
  EXPECT_TRUE(SMC::Dvd::flags.eject_change_requested);
}
```

Each `update()` call is one 20 ms tick. Inputs and outputs are in
`fake::hal` (`fakes.hpp`), log lines in `fake::log` (`fake::logged(text)`).
New test files go in `add_executable()` in `CMakeLists.txt`.

Not covered: anything that only runs on the Pico itself (`hal.cpp`, the web
interface, USB config, the sniffer's PIO/DMA capture, flash), and real timing.
