# USB configuration

The saved settings (LED brightness, RGB LED mode and brightness, fan thresholds, Wi-Fi access point name
and password, web interface on or off) can be changed over USB
serial, with `tools/og_smc_config.py`. This only works with the Pico **out of
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

## The script

```
pip install pyserial
python tools/og_smc_config.py get
python tools/og_smc_config.py set-brightness 40 set-threshold 2 65 save
python tools/og_smc_config.py defaults save
python tools/og_smc_config.py reboot
```

It finds the Pico by its USB vendor ID (`--port` to pick one). Changes only
apply to the settings in flash after `save`; they're loaded the next time the
SMC starts.

## Commands

The shell can also be used from any serial terminal (no echo). Each command is
one line, and its reply ends with `OK` or `ERR <reason>`. Lines starting with
`[` are log lines.

| Command | Reply |
|---------|-------|
| `help` | The commands |
| `get` | `firmware <name>`, `brightness <percent>`, `rgb-mode <mode>`, `rgb-brightness <percent>`, `ssid <name>`, `password <password>` (empty: from the board ID), `web on` or `web off` (with a note on builds without a web interface), then per fan threshold `threshold <index> <value> default <d> min <m> label <text>` |
| `set brightness <1-100>` | LED brightness in percent |
| `set rgb-mode <off\|boot\|mirror>` | RGB LED: `off` (dark, even while booting), `boot` (the boot phases, then dark), `mirror` (the boot phases, then what the front panel LEDs show) |
| `set rgb-brightness <1-100>` | RGB LED brightness in percent |
| `set threshold <index> <value>` | A fan threshold, between the minimum and the firmware's default |
| `set ssid <name>` | Wi-Fi access point name, 1-32 printable characters (spaces allowed) |
| `set password [<password>]` | Wi-Fi password, 8-63 printable characters. Without one: made from the board's unique ID |
| `set web on`, `set web off` | Start the web interface at power-up (default on). Kept on builds without one, but ignored there |
| `defaults` | Brightness back to `SMC_LED_BRIGHTNESS`, thresholds to the firmware's defaults, Wi-Fi back to `SMC_WIFI_SSID` / `SMC_WIFI_PASSWORD`, web interface on |
| `save` | Writes the settings to flash (`SETTINGS: Saved`) |
| `reboot` | Restarts the Pico |
