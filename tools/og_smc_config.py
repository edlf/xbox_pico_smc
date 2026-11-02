#!/usr/bin/env python3
"""Configure the OG SMC over USB serial.

Only works with the Pico out of the Xbox, powered from USB alone: it then
starts in config mode instead of running the SMC (see src/usb_config.hpp).

    pip install pyserial
    python tools/og_smc_config.py get
    python tools/og_smc_config.py set-brightness 40
    python tools/og_smc_config.py set-threshold 2 65
    python tools/og_smc_config.py set-ssid "My Xbox" set-password secret123 save
    python tools/og_smc_config.py set-password "" save   (back to the board ID one)
    python tools/og_smc_config.py set-web off save
    python tools/og_smc_config.py defaults
    python tools/og_smc_config.py save
    python tools/og_smc_config.py reboot

Several commands can be chained: `set-brightness 40 save`. Changes are only
kept after `save`.
"""

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is needed: pip install pyserial")

PICO_VID = 0x2E8A  # Raspberry Pi


def find_port():
    ports = [p for p in list_ports.comports() if p.vid == PICO_VID]
    if not ports:
        sys.exit("No Pico found. Is it out of the Xbox, plugged in over USB and running the OG SMC firmware?")
    if len(ports) > 1:
        sys.exit("Several Picos found, pick one with --port: " + ", ".join(p.device for p in ports))
    return ports[0].device


class Smc:
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(0.2)
        self.serial.reset_input_buffer()

    def command(self, line, timeout=5.0):
        """Sends one command, returns its reply lines (without the final OK)."""
        self.serial.write((line + "\n").encode())
        reply = []
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self.serial.readline()
            if not raw:
                continue
            text = raw.decode(errors="replace").strip()
            if not text or text.startswith("["):
                continue  # Blank, or a log line ("[timestamp] (INFO ) ...")
            if text == "OK":
                return reply
            if text.startswith("ERR"):
                raise RuntimeError(text[4:] or "error")
            reply.append(text)
        raise TimeoutError(f"No answer to '{line}'")


def print_settings(lines):
    for line in lines:
        words = line.split()
        if words[0] == "firmware":
            print(f"Firmware:        {words[1]}")
        elif words[0] == "brightness":
            print(f"LED brightness:  {words[1]}%")
        elif words[0] == "ssid":
            print(f"Wi-Fi name:      {line[5:]}")
        elif words[0] == "password":
            password = line[9:]
            print(f"Wi-Fi password:  {password or '(made from the board ID, shown in the boot log)'}")
        elif words[0] == "web":
            print(f"Web interface:   {line[4:]}")
        elif words[0] == "threshold":
            # threshold <index> <value> default <d> min <m> label <text>
            label = line.split(" label ", 1)[1]
            print(f"Threshold {words[1]}:     {words[2]} C (default {words[4]}, min {words[6]})  {label}")
        else:
            print(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: the one Pico found)")
    parser.add_argument("commands", nargs="+", help="get | set-brightness N | set-threshold I C | set-ssid NAME | set-password PW | set-web on|off | defaults | save | reboot")
    args = parser.parse_args()

    smc = Smc(args.port or find_port())
    words = list(args.commands)
    try:
        while words:
            name = words.pop(0)
            if name == "get":
                print_settings(smc.command("get"))
            elif name == "set-brightness":
                smc.command(f"set brightness {words.pop(0)}")
                print("LED brightness set")
            elif name == "set-threshold":
                index, value = words.pop(0), words.pop(0)
                smc.command(f"set threshold {index} {value}")
                print(f"Threshold {index} set")
            elif name == "set-ssid":
                smc.command(f"set ssid {words.pop(0)}")
                print("Wi-Fi name set")
            elif name == "set-password":
                # "" for the one made from the board ID
                password = words.pop(0)
                smc.command(f"set password {password}" if password else "set password")
                print("Wi-Fi password set")
            elif name == "set-web":
                state = words.pop(0)
                smc.command(f"set web {state}")
                print(f"Web interface {state}")
            elif name == "defaults":
                smc.command("defaults")
                print("Back to the defaults (not saved yet)")
            elif name == "save":
                smc.command("save")
                print("Saved")
            elif name == "reboot":
                smc.command("reboot")
                print("Rebooting")
            else:
                sys.exit(f"Unknown command '{name}'")
    except IndexError:
        sys.exit("Missing value for a command")
    except (RuntimeError, TimeoutError) as e:
        sys.exit(f"Error: {e}")


if __name__ == "__main__":
    main()
