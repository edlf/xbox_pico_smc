# USB configuration

The saved settings (LED brightness, RGB LED mode and brightness, fan thresholds, Wi-Fi access point name
and password, web interface on or off, the version reported on SMBus 0x01) can be changed over USB
serial, with `tools/smcx_config_gui.py` (a window) or `tools/smcx_config.py`
(the command line). This only works with the Pico **out of
the Xbox**, powered from USB alone.

## How it starts

At start-up, before the SMC touches any pin (`src/usb_config.cpp`):

1. AV mode 0 and 2 are read with the Pico's pull-down. In the Xbox they're
   pulled up to the standby rail through 10k and read high: the SMC starts as
   normal.
2. Pico / Pico 2: no USB power (VBUS, GP24): the SMC starts. The W boards read
   VBUS through the wireless chip, so they skip this.
3. USB is started and given a second for a host (PC) to enumerate it. No host:
   USB is stopped again and the SMC starts.

Only when all three pass does it enter config mode: the SMC doesn't run (no
pins driven, no web interface), and the Pico answers commands on its USB serial
port until it's reset. The log says `USB: Config mode, the SMC is not running`.
If the standby pull-ups show up later (checked every 0.5 s), it restarts as the
SMC.

On the W boards (no VBUS check) the SMC starts about a second later in the
Xbox, while it waits for a USB host that never comes.

An AV cable that grounds both mode pins (SCART) hides the standby pull-ups, so
don't power the Pico from USB while it's wired into an Xbox: the Xbox's standby
rail and the Pico's own 3.3 V regulator would drive the same net (see
`docs/Wiring.md`).

## Finding the Pico

In config mode the Pico shows up as a USB serial port with the product name
`SMCX` (`SMC_USB_PRODUCT` in `CMakeLists.txt`). The tools only open ports
with that name and the Raspberry Pi vendor ID (0x2E8A), so other serial
devices, other Picos included, are left alone. On Linux and macOS pyserial
reports the name; on Windows the tools ask Windows for it (the device's
bus-reported description), with no extra install.

The vendor and product IDs stay the Pico SDK's (0x2E8A:0x000A on RP2040,
0x2E8A:0x0009 on RP2350). `-DSMC_USB_PID=` sets another product ID, but only
use one Raspberry Pi has allocated to this project
(github.com/raspberrypi/usb-pid): the tools find the Pico by name either way.

Firmware from before the USB name shows up as `Pico`: pick its port with
`--port`, or tick "Show all ports" in the GUI.

## The GUI

```
pip install pyserial
pip install sv-ttk
python tools/smcx_config_gui.py
```

It needs tkinter, which comes with Python on Windows and macOS (on Linux it can
be a separate package, e.g. `python3-tk`). sv-ttk is optional: it gives the
window the Windows 11 look, with a light and dark theme that starts as the
system's and has a switch in the corner. Without it the window uses the plain
tkinter style.

It picks the SMCX's port (Refresh after plugging it in), and Connect reads
the settings. The header shows the firmware and its build: the short git hash
it was built from (`-dirty` if it had uncommitted changes), also printed as
`GIT_HASH` in the boot log.

Change the settings, then:

- **Apply** sends what changed to the Pico. It's used at once in config mode,
  but lost when the Pico restarts.
- **Save to flash** applies and saves, so the SMC loads them next time.
- **Defaults** goes back to the firmware's defaults (not saved until you save).
- **Reboot Pico** restarts it.
- **Firmware update** restarts it into the USB bootloader (BOOTSEL): it shows
  up as an `RPI-RP2` (Pico, Pico W) or `RP2350` (Pico 2, Pico 2 W) drive, and
  copying `SMCX.uf2` onto it installs the new firmware. Same as holding BOOTSEL
  while plugging it in.

While no Wi-Fi password is saved, the access point uses one made from the
board's unique ID (`smcx-<id>`, different for every Pico). With "Use the
password made from the board ID" ticked, the password field shows it
(read-only, "Show" reveals it, and it can be copied). Unticking it lets you
type your own; ticking it again goes back to the board ID one when you apply.
Firmware older than the `board-password` reply only shows it in the boot log.

The fan thresholds can only be lowered, down to the minimum. The log at the
bottom shows each command sent and the Pico's log lines. The window warns before
quitting or rebooting with applied changes that aren't saved.

## The script

```
pip install pyserial
python tools/smcx_config.py get
python tools/smcx_config.py set-brightness 40 set-threshold 2 65 save
python tools/smcx_config.py defaults save
python tools/smcx_config.py reboot
python tools/smcx_config.py bootloader
```

It finds the Pico by its USB name (`--port` to pick a port yourself). Changes only
apply to the settings in flash after `save`; they're loaded the next time the
SMC starts.

## Commands

The shell can also be used from any serial terminal (no echo). Each command is
one line, and its reply ends with `OK` or `ERR <reason>`. Lines starting with
`[` are log lines.

| Command | Reply |
|---------|-------|
| `help` | The commands |
| `get` | `firmware <name>`, `git <hash>` (short git hash of the build, `-dirty` if it had uncommitted changes), `brightness <percent>`, `rgb-mode <mode>`, `rgb-brightness <percent>`, `ssid <name>`, `password <password>` (empty: from the board ID), `board-password smcx-<id>` (the one the access point uses while the password is empty), `web on` or `web off` (with a note on builds without a web interface), `report-version <auto|name> default <name>` (the version reported on SMBus 0x01: `auto` is the build's, named after `default`), then per fan threshold `threshold <index> <value> default <d> min <m> label <text>` |
| `set brightness <1-100>` | LED brightness in percent |
| `set rgb-mode <off\|boot\|mirror>` | RGB LED: `off` (dark, even while booting), `boot` (the boot phases, then dark), `mirror` (the boot phases, then what the front panel LEDs show) |
| `set rgb-brightness <1-100>` | RGB LED brightness in percent |
| `set threshold <index> <value>` | A fan threshold, between the minimum and the firmware's default |
| `set ssid <name>` | Wi-Fi access point name, 1-32 printable characters (spaces allowed) |
| `set password [<password>]` | Wi-Fi password, 8-63 printable characters. Without one: made from the board's unique ID (`get` shows it as `board-password`) |
| `set web on`, `set web off` | Start the web interface at power-up (default on). Kept on builds without one, but ignored there |
| `set report-version <auto\|P01\|P11\|D01\|DXF\|P2L>` | Version reported on SMBus 0x01. `auto`: the build's (`SMC_REPORT_VERSION`). Only what the Xbox is told changes, the SMC still behaves as the firmware it was built for |
| `defaults` | Brightness back to `SMC_LED_BRIGHTNESS`, thresholds to the firmware's defaults, Wi-Fi back to `SMC_WIFI_SSID` / `SMC_WIFI_PASSWORD`, web interface on, reported version `auto` |
| `save` | Writes the settings to flash (`SETTINGS: Saved`) |
| `reboot` | Restarts the Pico |
| `bootloader` | Restarts the Pico into the USB bootloader (BOOTSEL) for a firmware update. Only in config mode: the SMBus and the web interface have no way to do this |
