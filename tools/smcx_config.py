#!/usr/bin/env python3
"""Configure the SMCX over USB serial.

Only works with the Pico out of the Xbox, powered from USB alone: it then
starts in config mode instead of running the SMC (see src/usb_config.hpp).

    pip install pyserial
    python tools/smcx_config.py get
    python tools/smcx_config.py set-brightness 40
    python tools/smcx_config.py set-rgb-mode mirror set-rgb-brightness 30 save
    python tools/smcx_config.py set-threshold 2 65
    python tools/smcx_config.py set-ssid "My Xbox" set-password secret123 save
    python tools/smcx_config.py set-password "" save   (back to the board ID one)
    python tools/smcx_config.py set-web off save
    python tools/smcx_config.py defaults
    python tools/smcx_config.py save
    python tools/smcx_config.py reboot
    python tools/smcx_config.py bootloader   (USB bootloader, to copy a new SMCX.uf2)

Several commands can be chained: `set-brightness 40 save`. Changes are only
kept after `save`. tools/smcx_config_gui.py does the same in a window.
"""

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    serial = None

PICO_VID = 0x2E8A  # Raspberry Pi
USB_PRODUCT = "SMCX"  # SMC_USB_PRODUCT in CMakeLists.txt
RGB_MODES = ("off", "boot", "mirror")


class SmcError(Exception):
    """No Pico, no pyserial, or an ERR reply."""


def require_pyserial():
    if serial is None:
        raise SmcError("pyserial is needed: pip install pyserial")


def windows_usb_product(vid, pid, serial_number):
    """The USB product name Windows has for a device, or None.

    pyserial leaves `product` empty on Windows. The port's parent USB device is
    USB\\VID_xxxx&PID_xxxx\\<serial>, and its bus-reported description is the
    product string.
    """
    import ctypes
    from ctypes import wintypes

    class GUID(ctypes.Structure):
        _fields_ = [
            ("Data1", ctypes.c_uint32),
            ("Data2", ctypes.c_uint16),
            ("Data3", ctypes.c_uint16),
            ("Data4", ctypes.c_ubyte * 8),
        ]

    class DEVPROPKEY(ctypes.Structure):
        _fields_ = [("fmtid", GUID), ("pid", ctypes.c_ulong)]

    # DEVPKEY_Device_BusReportedDeviceDesc
    key = DEVPROPKEY(
        GUID(0x540B947E, 0x8B40, 0x45BC, (ctypes.c_ubyte * 8)(0xA8, 0xA2, 0x6A, 0x0B, 0x89, 0x4C, 0xBD, 0xA2)), 4
    )
    try:
        cfgmgr = ctypes.WinDLL("cfgmgr32")
    except OSError:
        return None
    cfgmgr.CM_Locate_DevNodeW.argtypes = [ctypes.POINTER(wintypes.DWORD), wintypes.LPCWSTR, wintypes.ULONG]
    cfgmgr.CM_Get_DevNode_PropertyW.argtypes = [
        wintypes.DWORD,
        ctypes.POINTER(DEVPROPKEY),
        ctypes.POINTER(wintypes.ULONG),
        ctypes.c_void_p,
        ctypes.POINTER(wintypes.ULONG),
        wintypes.ULONG,
    ]

    node = wintypes.DWORD()
    if cfgmgr.CM_Locate_DevNodeW(ctypes.byref(node), f"USB\\VID_{vid:04X}&PID_{pid:04X}\\{serial_number}", 0) != 0:
        return None
    kind = wintypes.ULONG()
    text = ctypes.create_unicode_buffer(256)
    size = wintypes.ULONG(ctypes.sizeof(text))
    if cfgmgr.CM_Get_DevNode_PropertyW(node, ctypes.byref(key), ctypes.byref(kind), text, ctypes.byref(size), 0) != 0:
        return None
    return text.value


def usb_product(port):
    if port.product:
        return port.product  # Linux, macOS
    if sys.platform == "win32" and port.vid is not None and port.pid is not None and port.serial_number:
        return windows_usb_product(port.vid, port.pid, port.serial_number)
    return None


def is_smcx(port):
    return port.vid == PICO_VID and usb_product(port) == USB_PRODUCT


def pico_ports():
    """Serial ports of the SMCX boards plugged in, found by their USB name: other
    serial devices (other Picos included) are never opened."""
    require_pyserial()
    return [p.device for p in list_ports.comports() if is_smcx(p)]


def find_port():
    ports = pico_ports()
    if not ports:
        raise SmcError(
            "No SMCX found. Is the Pico out of the Xbox, plugged in over USB and running the SMCX firmware? "
            'Firmware older than the USB name "SMCX" is only found with --port.'
        )
    if len(ports) > 1:
        raise SmcError("Several SMCX boards found, pick one with --port: " + ", ".join(ports))
    return ports[0]


class Smc:
    def __init__(self, port, on_log=None):
        """on_log(line) gets the Pico's log lines ("[timestamp] (INFO ) ...")."""
        require_pyserial()
        self.on_log = on_log
        self.serial = serial.Serial(port, 115200, timeout=0.2)
        time.sleep(0.2)
        self.serial.reset_input_buffer()

    def close(self):
        self.serial.close()

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
            if not text:
                continue
            if text.startswith("["):
                if self.on_log:
                    self.on_log(text)
                continue
            if text == "OK":
                return reply
            if text.startswith("ERR"):
                raise SmcError(text[4:] or "error")
            reply.append(text)
        raise TimeoutError(f"No answer to '{line}'")

    def get(self):
        return parse_settings(self.command("get"))


def parse_settings(lines):
    """The `get` reply as a dict."""
    settings = {"thresholds": [], "rgb_available": True, "web_available": True}
    for line in lines:
        key, _, rest = line.partition(" ")
        if key == "firmware":
            settings["firmware"] = rest
        elif key == "git":
            settings["git"] = rest  # Short hash of the build (not sent by older firmware)
        elif key == "brightness":
            settings["brightness"] = int(rest)
        elif key == "rgb-mode":
            settings["rgb_mode"] = rest.split()[0]
            settings["rgb_available"] = "(" not in rest  # "(no RGB LED in this build, ignored)"
        elif key == "rgb-brightness":
            settings["rgb_brightness"] = int(rest)
        elif key == "ssid":
            settings["ssid"] = rest
        elif key == "password":
            settings["password"] = rest  # Empty: made from the board ID
        elif key == "board-password":
            settings["board_password"] = rest  # The board ID one (not sent by older firmware)
        elif key == "web":
            settings["web"] = rest.split()[0] == "on"
            settings["web_available"] = "(" not in rest  # "(no web interface in this build, ignored)"
        elif key == "threshold":
            # threshold <index> <value> default <d> min <m> label <text>
            words = rest.split()
            settings["thresholds"].append(
                {
                    "index": int(words[0]),
                    "value": int(words[1]),
                    "default": int(words[3]),
                    "min": int(words[5]),
                    "label": rest.split(" label ", 1)[1],
                }
            )
    return settings


def print_settings(s):
    print(f"Firmware:        {s.get('firmware', '?')}")
    print(f"Build:           {s.get('git', '(not reported by this firmware)')}")
    print(f"LED brightness:  {s.get('brightness', '?')}%")
    print(
        f"RGB LED:         {s.get('rgb_mode', '?')}{'' if s['rgb_available'] else ' (no RGB LED in this build, ignored)'}"
    )
    print(f"RGB brightness:  {s.get('rgb_brightness', '?')}%")
    print(f"Wi-Fi name:      {s.get('ssid', '')}")
    board = s.get("board_password")
    board_text = f"{board} (made from the board ID)" if board else "(made from the board ID, shown in the boot log)"
    print(f"Wi-Fi password:  {s.get('password') or board_text}")
    print(
        f"Web interface:   {'on' if s.get('web') else 'off'}{'' if s['web_available'] else ' (no web interface in this build, ignored)'}"
    )
    for t in s["thresholds"]:
        print(f"Threshold {t['index']}:     {t['value']} C (default {t['default']}, min {t['min']})  {t['label']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: the SMCX found by its USB name)")
    parser.add_argument(
        "commands",
        nargs="+",
        help="get | set-brightness N | set-rgb-mode off|boot|mirror | set-rgb-brightness N | set-threshold I C | set-ssid NAME | set-password PW | set-web on|off | defaults | save | reboot | bootloader",
    )
    args = parser.parse_args()

    words = list(args.commands)
    try:
        smc = Smc(args.port or find_port())
        while words:
            name = words.pop(0)
            if name == "get":
                print_settings(smc.get())
            elif name == "set-brightness":
                smc.command(f"set brightness {words.pop(0)}")
                print("LED brightness set")
            elif name == "set-rgb-mode":
                mode = words.pop(0)
                smc.command(f"set rgb-mode {mode}")
                print(f"RGB LED mode {mode}")
            elif name == "set-rgb-brightness":
                smc.command(f"set rgb-brightness {words.pop(0)}")
                print("RGB LED brightness set")
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
            elif name == "bootloader":
                smc.command("bootloader")
                print("Restarting into the bootloader: copy SMCX.uf2 to the RPI-RP2 / RP2350 drive that appears")
            else:
                sys.exit(f"Unknown command '{name}'")
    except IndexError:
        sys.exit("Missing value for a command")
    except (SmcError, OSError) as e:  # OSError: timeouts, port busy or gone
        sys.exit(f"Error: {e}")


if __name__ == "__main__":
    main()
