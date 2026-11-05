# USB configuration

The saved settings (LED brightness and fade, RGB LED mode and brightness, fan thresholds, Wi-Fi access point name
and password, web interface on or off, the version reported on SMBus 0x01, the boot challenge, the AV pack, the DVD drive, the log level, the encoder sniffer and the NTC) can be changed over USB
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

The boot log has two sections. "Build options" is what's fixed in the
firmware: `GIT_HASH`, `PICO_BOARD`, `SMC_FIRMWARE`, `SMC_RGB_LED` and
`SMC_WEB_INTERFACE_SUPPORT`. "Settings" follows once the saved settings are
loaded, `(saved)` or `(none saved, the defaults)`, with the same lines as the `get`
reply below (e.g. `> brightness 100`), the Wi-Fi password included
(`password`, or only `board-password` when none is set), but without the fan
thresholds. It's left out below log level INFO. The other CMake options are only the defaults of those settings.

The settings are in cards, in three columns:

- **Front panel** (LED brightness and fade, the power and eject buttons,
  BOOTSEL as a power button), **RGB LED**, **Web interface**.
- **SMC** (reported version), **Enhancements** (AV pack, DVD drive, boot
  challenge), **NTC thermistor** (its values, the shutdown limit, and the
  sensor bypass that uses it), **Diagnostics** (log level, encoder sniffer).
- **Fan** (thresholds, ramp and glide).

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
- **Export...** writes the settings as the window shows them (applied or not)
  to a JSON file, **Import...** reads one into the window. An import only fills
  in the fields: Apply or Save to flash sends it, as if you'd typed it.

The exported file (`"format": "smcx-settings"`) has every setting by the names
in the `get` reply, and the firmware it came from. It has the Wi-Fi password
in it as it is (empty: the board ID one), so keep the file to yourself. An
import is checked like typed values; settings the Pico's firmware doesn't have
are left out (listed in the log), and so are the fan thresholds when it's for
another firmware (P01, P11 and P2L thresholds differ). A file with only some
settings changes only those.

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
python tools/smcx_config.py export smcx.json
python tools/smcx_config.py import smcx.json save
```

`export` and `import` are the GUI's: `import` sends only what differs from the
Pico's settings, then `save` keeps it.

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
| `get` | `firmware <name>`, `git <hash>` (short git hash of the build, `-dirty` if it had uncommitted changes), `brightness <percent>`, `fade on` or `fade off`, `rgb-mode <mode>`, `rgb-brightness <percent>`, `ssid <name>`, `password <password>` (empty: from the board ID), `board-password smcx-<id>` (the one the access point uses while the password is empty), `web on` or `web off` (with a note on builds without a web interface), `report-version <auto|name> default <name>` (the version reported on SMBus 0x01: `auto` is the build's, named after `default`), `boot-challenge on|off`, `av-pack detect|<0-6>`, `log-level <LEVEL>`, `no-dvd on|off`, `sniffer on|off`, `ntc on|off beta <K> r25 <ohms> series <ohms>`, `fan-ramp up <n> down <n> glide-up <ms> glide-down <ms>`, `sensor-bypass on|off`, `bypass-offsets cpu <C> board <C>`, `ntc-shutdown on|off temp <C>`, `power-button on|off`, `eject-button on|off`, `bootsel-power on|off` (as set: it's in use anyway while the power button is off), then per fan threshold `threshold <index> <value> default <d> min <m> label <text>` |
| `set brightness <1-100>` | LED brightness in percent |
| `set fade <on\|off>` | Fade the front panel LEDs in at power up and out at power down (default `SMC_LED_FADE`). Off: they switch at once, like the PIC |
| `set rgb-mode <off\|boot\|mirror>` | RGB LED: `off` (dark, even while booting), `boot` (the boot phases, then dark), `mirror` (the boot phases, then what the front panel LEDs show) |
| `set rgb-brightness <1-100>` | RGB LED brightness in percent |
| `set fan-ramp-up <1-5>`, `set fan-ramp-down <1-5>` | How far each fan decision (every 20 s) moves the speed: up multiplies the warm +1 / hot +3 steps, down is the step when cool. 1 is the PIC's |
| `set fan-glide-up <0-1000>`, `set fan-glide-down <0-1000>` | ms per speed step while the fan speeds up / slows down to a new speed. 0 (default) changes at once, like the PIC. An overheat always goes straight to full speed |
| `set threshold <index> <value>` | A fan threshold, between the minimum and the firmware's default |
| `set ssid <name>` | Wi-Fi access point name, 1-32 printable characters (spaces allowed) |
| `set password [<password>]` | Wi-Fi password, 8-63 printable characters. Without one: made from the board's unique ID (`get` shows it as `board-password`) |
| `set web on`, `set web off` | Start the web interface at power-up (default off, `SMC_WEB_INTERFACE_DEFAULT`). Kept on builds without one, but ignored there |
| `set boot-challenge <on\|off>` | Enforce the boot challenge: a failed RAM test or challenge resets the Xbox (default `SMC_BOOT_CHALLENGE`). Off, failures are only logged |
| `set av-pack <detect\|0-6>` | Read the AV connector, or report this AV pack code (SMBus 0x04: 0 SCART, 1 HD AV pack, 2 VGA, 3 RF, 4 Advanced AV, 5 none known, 6 Standard AV). Default `SMC_AV_PACK_OVERRIDE` / `SMC_AV_PACK_MODE` |
| `set no-dvd <on\|off>` | No DVD drive, for drive-less systems. On: the tray is reported closed and empty. Off (the default, `SMC_NO_DVD=OFF`): the drive is used |
| `set log-level <silent\|critical\|warnings\|info\|debug>` | Console log level (default `SMC_DEBUG_LEVEL`) |
| `set sniffer <on\|off>` | Log the video encoder the CPU talks to (default `SMC_I2C_SNIFFER`). Started at boot, so a change needs a restart |
| `set ntc <on\|off>` | NTC thermistor on GP28 (default `SMC_NTC_SENSOR`) |
| `set bypass-cpu-offset <-30-30>`, `set bypass-board-offset <-30-30>` | With the sensor bypass: °C added to the NTC's reading for the CPU temperature, and to the Pico's for the board temperature (default +10 each). SMBus 0x31/0x32 stay raw |
| `set power-button <on\|off>` | The front panel power button. Off: the Pico's BOOTSEL button is the power button instead |
| `set eject-button <on\|off>` | The front panel eject button. Off: the tray only moves when the OS asks |
| `set bootsel-power <on\|off>` | The Pico's BOOTSEL button as a power button, as well as the front panel's (default off, `SMC_BOOTSEL_POWER`). Can't be turned off while the power button is off (turn that on first) |
| `set sensor-bypass <on\|off>` | For a bad ADM1032: CPU temperature from the NTC, board temperature from the Pico, the ADM1032 not read (default off). Needs the NTC on and fitted, otherwise the fan control sees a dead sensor and forces an overheat |
| `set ntc-beta <1000-10000>`, `set ntc-r25 <100-1000000>`, `set ntc-series <100-1000000>` | The NTC's beta (K), resistance at 25 °C and the series resistor from 3.3 V (ohms) |
| `set ntc-shutdown <on\|off>`, `set ntc-shutdown-temp <40-120>` | Shut the Xbox down, as an overheat, once the NTC reads that temperature (°C) or more: for an NTC on something the ADM1032 doesn't see (default off, 80 °C; `SMC_NTC_SHUTDOWN` / `SMC_NTC_SHUTDOWN_TEMP` in `src/config.hpp`). Needs the NTC on; an open or shorted NTC doesn't trigger it. Ignored while the sensor bypass is on: the NTC is the CPU temperature then, with the CPU thresholds |
| `set report-version <auto\|P01\|P11\|D01\|DXF\|P2L>` | Version reported on SMBus 0x01. `auto`: the build's (`SMC_REPORT_VERSION`). Only what the Xbox is told changes, the SMC still behaves as the firmware it was built for |
| `defaults` | Brightness back to `SMC_LED_BRIGHTNESS`, fade to `SMC_LED_FADE`, thresholds to the firmware's defaults, Wi-Fi back to `SMC_WIFI_SSID` / `SMC_WIFI_PASSWORD`, web interface off (`SMC_WEB_INTERFACE_DEFAULT`), reported version `auto`, the fan ramp to 1/1 and no glide, the sensor bypass off with +10 offsets, the NTC shutdown off at 80 °C, both buttons on and BOOTSEL unused (`SMC_BOOTSEL_POWER`), and the settings above back to their CMake defaults |
| `save` | Writes the settings to flash (`SETTINGS: Saved`) |
| `reboot` | Restarts the Pico |
| `bootloader` | Restarts the Pico into the USB bootloader (BOOTSEL) for a firmware update. Only in config mode: the SMBus and the web interface have no way to do this |
