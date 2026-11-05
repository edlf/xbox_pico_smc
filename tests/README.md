# Host tests

The SMC's state machines and SMBus handling, built for the PC and run without
a Pico. The hardware is a fake HAL (`fake_hal.cpp`) whose pins are plain
variables, a few fake Pico SDK headers (`fake_sdk/`) and a debug log that
records every line (`fake_debug.cpp`). The tests use GoogleTest fixtures
(`TEST_F`).

## Running them

Everything CI runs (lint, these tests, the config tool tests below and the
firmware for the four boards), in one go:

```
python tools/check.py              # all of it
python tools/check.py lint tests   # some steps: lint, tests, tools, firmware
python tools/check.py --fix lint   # reformat instead of checking
```

It builds in `build-check/` and finds MSYS2 (C:/msys64 or D:/msys64, or
`--msys <folder>`) for these tests on Windows, and cmake/ninja from the Pico
VS Code extension for the firmware. The lint tools run at CI's pinned versions
through uvx when it's installed. Or by hand:

Any GCC or Clang with C++17 and GoogleTest works. With MSYS2, install
GoogleTest once (`pacman -S gtest` in the MSYS shell), then from the MSYS shell,
or with `D:\msys64\usr\bin` first on `PATH`:

```
cmake -S tests -B build-tests -G "Unix Makefiles"
cmake --build build-tests -j 8
ctest --test-dir build-tests --output-on-failure
```

Or run `build-tests/smcx_tests.exe` directly to see every test by name;
`--gtest_filter=SmbusTest.*` runs one fixture.

They build with the default settings from `src/config.hpp` (P11).

### The config tools

`tests/tools/` tests the Python tools with pytest, without a Pico: the `get`
reply parser, the commands the CLI and the GUI's Apply send, the JSON export
and import, and the serial
handshake against a fake Pico. A sample `get` reply (`conftest.py`) is
checked against the keys `Settings::describe()` sends, so renaming or adding a
setting in the firmware fails here until the tools follow. Needs pyserial and
Tk (the GUI tests skip without a display; the window is never shown):

```
pip install pytest pyserial
python -m pytest tests/tools
```

## What's covered

- `test_encoder_mode.cpp`: video mode decoding from encoder writes (Xcalibur
  rasters, Conexant and Focus SD/HD, the X-HD mode packet), and that the mode
  is only logged once a mode set settles and when it changes.
- `test_front_panel.cpp`: button debounce (two readings), a held button counting
  once, glitches, the power/eject interlock, the power and eject buttons turned
  off, and BOOTSEL as a power button.
- `test_smbus.cpp`: the SMBus commands, sent through `handle_SMBus_interrupt()`
  as the I2C slave does: version string, reset/power requests, fan override,
  temperatures, OS answers (0x0D), interrupt reason, RAM test rule,
  reset-on-eject, scratch, and the extra commands 0x30-0x34.
- `test_dvd.cpp`: the tray state debounce (start-up delay, two matching
  reads, which states raise SMIs), the eject pulse, the open/close decision
  (OS request, eject button, shutdown, the P2L "follow a tray pushed closed")
  and the tray moving bit.
- `test_led.cpp`: which LED pattern wins (boot failure, overheat, AV missing,
  SMBus override, power off, tray moving), the power down fade, and the fade
  turned off (before and during one).
- `test_fan.cpp`: the fan's ramp steps (multiplied, limited to 10-50) and the
  glide between speeds (up and down separately, an overheat skips it), and
  the temperatures from the ADM1032 or, with the sensor bypass, the NTC and
  the Pico with their offsets (failing without an NTC reading, never touching
  the bus).
- `test_settings.cpp`: settings saved to and loaded from a fake flash
  (`fake_flash.cpp`): round trip, nothing written when unchanged, corrupt
  records, thresholds from another firmware, thresholds below the 40 °C
  minimum, invalid Wi-Fi values, the access point password (saved, or the
  board ID one), the reported version, the fan ramp, the bypass offsets, the
  buttons, the settings that used to be build options (boot challenge, AV
  pack, log level, DVD, sniffer, NTC), and a record of another layout version
  being ignored.
- `test_i2c_decoder.cpp`: the sniffer's SMBus decoder fed SDA/SCL levels:
  encoders found, writes logged (long, NACKed, two per tick), reads, other
  devices and our own transfers not logged, and a video mode decoded from bus
  writes.
- `test_ring_buffer.cpp`: the log buffer's order, wrap-around and drops.
- `test_reset_on_eject.cpp`: reset-on-eject (SMBus 0x19) across power cycles.
- `test_rgb_led.cpp`: the RGB LED through a boot in the simulator: the phases
  in order, the modes (off, boot, mirror), brightness and the SMBus colour.
- `test_scenarios.cpp`: whole-Xbox scenarios in the simulator (below): boot,
  power off through the OS (answering, hung, busy), the tray, reset-on-eject,
  reset and power cycle requests, a PSU dropout, the eject storm after a
  power cycle that used to reset the Xbox, a failed RAM test with the boot
  challenge enforced or not, a fixed AV pack, no DVD drive, and the sensor
  bypass with and without the NTC, BOOTSEL as the power button, the eject
  button off, and the UART console's power and eject commands.

## Simulator

`sim/` runs the firmware's real `main_loop()` on the PC, with simple models of
the Xbox around it:

- **PSU**: POWER_OK 60 ms after POWON, off at once, and dropouts on request
  (which fire the firmware's POWER_OK interrupt).
- **DVD drive**: the eject line toggles the tray (opening and closing take
  1.2 s, a moving tray reverses, a spinning disc unloads first), and a closed
  tray reports a disc (0x60) or none (0x40) a second later. 0x70 while unpowered.
- **BIOS and OS**: once the CPU runs (power, PLL, no reset) the BIOS does what a
  real one does over SMBus (version, RAM test, boot challenge answer), and a
  second later the dashboard writes 0x1A and answers SMIs: it reads 0x11,
  answers 0x0D, and on an eject press asks for the tray with 0x0C and writes
  0x19 = 0x01, as seen in real logs.

Time is simulated: `hal::timer1_wait()` hands each tick to the simulator,
which moves the clock on 20 ms and runs the models. A minute of Xbox time takes
milliseconds. The watchdog works too: 1 s without a pet reboots the "Pico"
(`main_loop()` starts again).

### Console

`build-tests/smcx_sim` takes commands typed in, or a script file:

```
build-tests/smcx_sim tests/sim/eject_storm.txt
```

It prints the firmware's log as the serial console would, with the models'
lines tagged `(SIM  )`. `help` lists the commands: `power`, `eject`,
`run 2s`, `push`, `disc on|off`, `av 0x0E`, `psu-drop 200ms`, `os hang`,
`os busy`, `write 0x19 0x00`, `read 0x03`, `status`, `debug on`, ...

### Scenario tests

`test_scenarios.cpp` uses the same `sim::Simulator` (`sim/simulator.hpp`):

```cpp
TEST_F(ScenarioTest, EjectOpensAndClosesTheTray) {
  boot();               // Power button, then until the dashboard runs
  sim->pressEject();
  sim->run(3000);
  EXPECT_EQ(sim->trayPins(), 0x10);
}
```

`sim->os` and `sim->drive` change how the OS and the drive behave, and
`resets()`, `powerOns()`, `watchdogReboots()` and `fake::logged()` say what
happened. A log from the real Xbox is usually easy to turn into one.

The models are simple. They're for the SMC's side of things and will miss
anything that depends on how the real kernel or drive behave beyond what's
modelled.

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
New test files go in `add_executable(smcx_tests ...)` in `CMakeLists.txt`.

Not covered: anything that only runs on the Pico itself (`hal*.cpp`, the web
interface, USB config, the sniffer's PIO/DMA capture, real flash), and real timing.
