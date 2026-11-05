"""Configure the SMCX over USB serial.

Only works with the Pico out of the Xbox, powered from USB alone: it then
starts in config mode instead of running the SMC (see src/usb_config.hpp).

    pip install pyserial
    python tools/smcx_config.py get
    python tools/smcx_config.py set-brightness 40
    python tools/smcx_config.py set-fade off save
    python tools/smcx_config.py set-no-dvd on set-av-pack 1 set-boot-challenge on save
    python tools/smcx_config.py set-ntc on set-ntc-beta 3950 set-log-level debug save
    python tools/smcx_config.py set-ntc on set-sensor-bypass on save   (a bad ADM1032)
    python tools/smcx_config.py set-bypass-cpu-offset 5 set-bypass-board-offset -8 save
    python tools/smcx_config.py set-power-button off save   (BOOTSEL powers the Xbox instead)
    python tools/smcx_config.py set-rgb-mode mirror set-rgb-brightness 30 save
    python tools/smcx_config.py set-threshold 2 65
    python tools/smcx_config.py set-fan-ramp-up 2 set-fan-glide-down 200 save
    python tools/smcx_config.py set-ssid "My Xbox" set-password secret123 save
    python tools/smcx_config.py set-password "" save   (back to the board ID one)
    python tools/smcx_config.py set-web off save
    python tools/smcx_config.py set-report-version P2L save   (auto: the build's)
    python tools/smcx_config.py defaults
    python tools/smcx_config.py save
    python tools/smcx_config.py reboot
    python tools/smcx_config.py bootloader   (USB bootloader, to copy a new SMCX.uf2)
    python tools/smcx_config.py export smcx.json   (the settings to a JSON file)
    python tools/smcx_config.py import smcx.json save   (and back: sends what differs)

Several commands can be chained: `set-brightness 40 save`. Changes are only
kept after `save`. tools/smcx_config_gui.py does the same in a window.
"""

import argparse
import copy
import json
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
LOG_LEVELS = ("silent", "critical", "warnings", "info", "debug")
# SMBus 0x04 AV pack codes (the AV pack setting)
AV_PACKS = {
    0: "SCART",
    1: "High Definition AV Pack",
    2: "VGA",
    3: "RF",
    4: "Advanced AV (S-Video)",
    5: "None known",
    6: "Standard AV",
}

# CLI commands that are one `set <name> <value>` each: CLI name -> shell name
SIMPLE_SETTINGS = {
    "set-fade": "fade",
    "set-boot-challenge": "boot-challenge",
    "set-av-pack": "av-pack",
    "set-log-level": "log-level",
    "set-no-dvd": "no-dvd",
    "set-sniffer": "sniffer",
    "set-ntc": "ntc",
    "set-ntc-beta": "ntc-beta",
    "set-ntc-r25": "ntc-r25",
    "set-ntc-series": "ntc-series",
    "set-fan-ramp-up": "fan-ramp-up",
    "set-fan-ramp-down": "fan-ramp-down",
    "set-fan-glide-up": "fan-glide-up",
    "set-fan-glide-down": "fan-glide-down",
    "set-sensor-bypass": "sensor-bypass",
    "set-bypass-cpu-offset": "bypass-cpu-offset",
    "set-bypass-board-offset": "bypass-board-offset",
    "set-power-button": "power-button",
    "set-eject-button": "eject-button",
    "set-bootsel-power": "bootsel-power",
}


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
        self.first = True  # The first command gets one retry, see command()
        self.sync()

    def close(self):
        self.serial.close()

    def sync(self):
        """Starts the Pico on a fresh line: right after the port opens, bytes
        written before it (Windows probing a new port) can be left in its line,
        or the first command can arrive before the Pico sees the port open and
        its reply is dropped. A bare newline ends any half line, the Pico ignores
        an empty one, and whatever comes back is read until it goes quiet."""
        self.serial.write(b"\n")
        deadline = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            raw = self.serial.readline()  # Waits up to the 0.2 s timeout
            if not raw:
                break
            text = raw.decode(errors="replace").strip()
            if text.startswith("[") and self.on_log:
                self.on_log(text)

    def command(self, line, timeout=5.0):
        """Sends one command, returns its reply lines (without the final OK).
        A "get" as the first command after opening the port is tried again once
        if it gets no answer or isn't understood (see sync()). Nothing else is:
        a "save" or "reboot" whose reply was lost mustn't run twice."""
        retry = self.first and line == "get"
        self.first = False
        if not retry:
            return self.send(line, timeout)
        try:
            return self.send(line, timeout)
        except (TimeoutError, SmcError) as e:
            if isinstance(e, SmcError) and not str(e).startswith("unknown command"):
                raise
            self.sync()
            return self.send(line, timeout)

    def send(self, line, timeout):
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
        elif key == "fade":
            settings["fade"] = rest == "on"  # Not sent by older firmware
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
        elif key in (
            "boot-challenge",
            "no-dvd",
            "sniffer",
            "sensor-bypass",
            "power-button",
            "eject-button",
            "bootsel-power",
        ):
            settings[key.replace("-", "_")] = rest == "on"  # Not sent by older firmware, nor the ones below
        elif key == "av-pack":
            settings["av_pack"] = None if rest == "detect" else int(rest)  # None: read the connector
        elif key == "log-level":
            settings["log_level"] = rest.lower()
        elif key == "bypass-offsets":
            # bypass-offsets cpu <C> board <C>
            words = rest.split()
            settings["bypass_offsets"] = {"cpu": int(words[1]), "board": int(words[3])}
        elif key == "fan-ramp":
            # fan-ramp up <1-5> down <1-5> glide-up <ms> glide-down <ms>
            words = rest.split()
            settings["fan_ramp"] = {
                "up": int(words[1]),
                "down": int(words[3]),
                "glide_up": int(words[5]),
                "glide_down": int(words[7]),
            }
        elif key == "ntc":
            # ntc <on|off> beta <K> r25 <ohms> series <ohms>
            words = rest.split()
            settings["ntc"] = {
                "enabled": words[0] == "on",
                "beta": int(words[2]),
                "r25": int(words[4]),
                "series": int(words[6]),
            }
        elif key == "report-version":
            # report-version <auto|name> default <the build's>; not sent by older firmware
            words = rest.split()
            settings["report_version"] = words[0]
            settings["report_version_default"] = words[2]
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


# ---- What to send, export and import ----------------------------------------

REPORT_VERSIONS = ("P01", "P11", "D01", "DXF", "P2L")  # Besides "auto", the build's
NTC_RANGES = {"beta": (1000, 10000), "r25": (100, 1000000), "series": (100, 1000000)}  # As the firmware's help


def printable(text):
    """Only plain printable ASCII, as the firmware accepts."""
    return all(32 <= ord(c) < 127 for c in text)


def _number(value, what, low, high, unit=""):
    if isinstance(value, bool) or not isinstance(value, int):
        raise ValueError(f"{what} must be a number")  # noqa: TRY004 (a bad value, reported as the others)
    if not low <= value <= high:
        raise ValueError(f"{what} must be {low} to {high}{unit}")
    return value


def _switch(value, what):
    if not isinstance(value, bool):
        raise ValueError(f"{what} must be true or false")  # noqa: TRY004 (a bad value, reported as the others)
    return value


def _on(value):
    return "on" if value else "off"


def commands_for(current, wanted):
    """The `set` commands that turn the current settings (parse_settings(),
    from the Pico) into the wanted ones (the same shape), in an order the
    firmware takes. Only the settings this firmware has (those in current) are
    looked at. Raises ValueError for a value the firmware would refuse."""
    s, w = current, wanted
    commands = []

    if _number(w.get("brightness"), "The LED brightness", 1, 100, "%") != s.get("brightness"):
        commands.append(f"set brightness {w['brightness']}")
    if "fade" in s and _switch(w["fade"], "The LED fade") != s["fade"]:
        commands.append(f"set fade {_on(w['fade'])}")
    if w.get("rgb_mode") not in RGB_MODES:
        raise ValueError(f"The RGB LED mode must be one of {', '.join(RGB_MODES)}")
    if w["rgb_mode"] != s.get("rgb_mode"):
        commands.append(f"set rgb-mode {w['rgb_mode']}")
    if _number(w.get("rgb_brightness"), "The RGB LED brightness", 1, 100, "%") != s.get("rgb_brightness"):
        commands.append(f"set rgb-brightness {w['rgb_brightness']}")

    values = {t["index"]: t["value"] for t in w.get("thresholds", [])}
    for t in s["thresholds"]:
        value = _number(values.get(t["index"], t["value"]), f"Fan threshold {t['index']}", t["min"], t["default"])
        if value != t["value"]:
            commands.append(f"set threshold {t['index']} {value}")

    ssid = w.get("ssid")
    if ssid != s.get("ssid"):
        if not isinstance(ssid, str) or not 1 <= len(ssid) <= 32 or not printable(ssid):
            raise ValueError("The Wi-Fi name must be 1-32 printable characters")
        commands.append(f"set ssid {ssid}")
    password = w.get("password", "")
    if password != s.get("password"):
        if not isinstance(password, str) or password and (not 8 <= len(password) <= 63 or not printable(password)):
            raise ValueError("The Wi-Fi password must be 8-63 printable characters")
        commands.append(f"set password {password}" if password else "set password")

    if "fan_ramp" in s:
        ramp, wanted_ramp = s["fan_ramp"], w["fan_ramp"]
        for name, key, what, low, high in (
            ("fan-ramp-up", "up", "The fan ramp up", 1, 5),
            ("fan-ramp-down", "down", "The fan ramp down", 1, 5),
            ("fan-glide-up", "glide_up", "The fan glide", 0, 1000),
            ("fan-glide-down", "glide_down", "The fan glide", 0, 1000),
        ):
            value = _number(wanted_ramp.get(key), what, low, high, " ms" if "glide" in key else "")
            if value != ramp[key]:
                commands.append(f"set {name} {value}")
    if "av_pack" in s:
        code = w["av_pack"]
        if code is not None:
            _number(code, "The AV pack", 0, 6)
        if code != s["av_pack"]:
            commands.append(f"set av-pack {'detect' if code is None else code}")
    for key, name, what in (
        ("no_dvd", "no-dvd", "No DVD drive"),
        ("boot_challenge", "boot-challenge", "The boot challenge"),
        ("sniffer", "sniffer", "The sniffer"),
        ("sensor_bypass", "sensor-bypass", "The sensor bypass"),
        ("eject_button", "eject-button", "The eject button"),
    ):
        if key in s and _switch(w[key], what) != s[key]:
            commands.append(f"set {name} {_on(w[key])}")
    if "power_button" in s:
        # BOOTSEL as set (the firmware keeps it in use without the power button
        # anyway). It can only be turned off with the power button on, so that
        # one goes first when it's turned on.
        power = _switch(w["power_button"], "The power button")
        bootsel = _switch(w["bootsel_power"], "BOOTSEL as a power button")
        power_command = [f"set power-button {_on(power)}"] if power != s["power_button"] else []
        bootsel_command = [f"set bootsel-power {_on(bootsel)}"] if bootsel != s["bootsel_power"] else []
        commands += power_command + bootsel_command if power else bootsel_command + power_command
    if "log_level" in s:
        if w["log_level"] not in LOG_LEVELS:
            raise ValueError(f"The log level must be one of {', '.join(LOG_LEVELS)}")
        if w["log_level"] != s["log_level"]:
            commands.append(f"set log-level {w['log_level']}")
    if "ntc" in s:
        # The values first: they're checked even with the NTC off
        for key, (low, high) in NTC_RANGES.items():
            if _number(w["ntc"].get(key), f"The NTC {key}", low, high) != s["ntc"][key]:
                commands.append(f"set ntc-{key} {w['ntc'][key]}")
        if "bypass_offsets" in s:
            for key in ("cpu", "board"):
                offset = _number(w["bypass_offsets"].get(key), f"The {key} offset", -30, 30, " °C")
                if offset != s["bypass_offsets"][key]:
                    commands.append(f"set bypass-{key}-offset {offset}")
        ntc_on = _switch(w["ntc"].get("enabled"), "The NTC")
        if w.get("sensor_bypass") and not ntc_on:
            raise ValueError("The sensor bypass needs the NTC on: it takes the CPU temperature from it")
        if ntc_on != s["ntc"]["enabled"]:
            commands.append(f"set ntc {_on(ntc_on)}")
    if "report_version" in s:
        version = w["report_version"]
        if version != "auto" and version not in REPORT_VERSIONS:
            raise ValueError(f"The reported version must be auto or one of {', '.join(REPORT_VERSIONS)}")
        if version != s["report_version"]:
            commands.append(f"set report-version {version}")

    if _switch(w.get("web"), "The web interface") != s.get("web"):
        commands.append(f"set web {_on(w['web'])}")
    return commands


EXPORT_FORMAT = "smcx-settings"
EXPORT_VERSION = 1
# The settings an export has, as parse_settings() names them (thresholds aside)
EXPORT_KEYS = (
    "brightness",
    "fade",
    "rgb_mode",
    "rgb_brightness",
    "ssid",
    "password",
    "web",
    "report_version",
    "boot_challenge",
    "av_pack",
    "log_level",
    "no_dvd",
    "sniffer",
    "ntc",
    "fan_ramp",
    "sensor_bypass",
    "bypass_offsets",
    "power_button",
    "eject_button",
    "bootsel_power",
)


def export_settings(s):
    """The settings as a JSON-ready dict. The thresholds are kept with the
    firmware they're for: they only import into the same one. The Wi-Fi
    password is in it as it is (empty: the board ID one)."""
    settings = {key: s[key] for key in EXPORT_KEYS if key in s}
    settings["thresholds"] = [{"index": t["index"], "value": t["value"], "label": t["label"]} for t in s["thresholds"]]
    return {
        "format": EXPORT_FORMAT,
        "version": EXPORT_VERSION,
        "firmware": s.get("firmware"),
        "git": s.get("git"),
        "settings": settings,
    }


def import_settings(current, data):
    """The current settings with an export's on top, and notes on what was
    left out. commands_for(current, result) then sends them. Raises
    ValueError for something that isn't an export."""
    if not isinstance(data, dict) or data.get("format") != EXPORT_FORMAT:
        raise ValueError("Not an SMCX settings file")
    if data.get("version") != EXPORT_VERSION:
        raise ValueError(f"Settings file version {data.get('version')}, this tool reads version {EXPORT_VERSION}")
    imported = data.get("settings")
    if not isinstance(imported, dict):
        raise ValueError("The settings file has no settings")  # noqa: TRY004 (a bad value, reported as the others)
    wanted = copy.deepcopy(current)
    notes = []
    for key, value in imported.items():
        if key == "thresholds":
            if data.get("firmware") != current.get("firmware"):
                notes.append(
                    f"Fan thresholds left out: saved from {data.get('firmware')} firmware, this is {current.get('firmware')}"
                )
                continue
            values = {t.get("index"): t.get("value") for t in value if isinstance(t, dict)}
            for t in wanted["thresholds"]:
                if t["index"] in values:
                    t["value"] = values[t["index"]]
        elif key not in EXPORT_KEYS:
            notes.append(f"Unknown setting left out: {key}")
        elif key not in current:
            notes.append(f"Left out, this firmware doesn't have it: {key}")
        elif isinstance(current[key], dict):
            if not isinstance(value, dict):
                raise ValueError(f"{key} must be an object")
            wanted[key] = {**current[key], **{k: v for k, v in value.items() if k in current[key]}}
        else:
            wanted[key] = value
    return wanted, notes


def print_settings(s):
    print(f"Firmware:        {s.get('firmware', '?')}")
    print(f"Build:           {s.get('git', '(not reported by this firmware)')}")
    print(f"LED brightness:  {s.get('brightness', '?')}%")
    if "fade" in s:
        print(f"LED fade:        {'on' if s['fade'] else 'off'}")
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
    if "boot_challenge" in s:
        print(f"Boot challenge:  {'enforced' if s['boot_challenge'] else 'not enforced (logged only)'}")
    if "av_pack" in s:
        code = s["av_pack"]
        shown = "from the AV connector" if code is None else f"{code} {AV_PACKS.get(code, '?')}"
        print(f"AV pack:         {shown}")
    if "no_dvd" in s:
        print(f"No DVD drive:    {'on (tray reported closed and empty)' if s['no_dvd'] else 'off (the drive is used)'}")
    if "log_level" in s:
        print(f"Log level:       {s['log_level']}")
    if "sniffer" in s:
        print(f"Encoder sniffer: {'on' if s['sniffer'] else 'off'}")
    if "power_button" in s:
        print(f"Power button:    {'on' if s['power_button'] else 'off'}")
        print(f"Eject button:    {'on' if s['eject_button'] else 'off'}")
        if not s["power_button"]:
            bootsel = "power button (needed: the power button is off)"
        else:
            bootsel = "power button too" if s["bootsel_power"] else "unused"
        print(f"BOOTSEL button:  {bootsel}")
    if "sensor_bypass" in s:
        offsets = s.get("bypass_offsets", {"cpu": 0, "board": 0})
        on = f"on (CPU from the NTC {offsets['cpu']:+d} C, board from the Pico {offsets['board']:+d} C)"
        print(f"Sensor bypass:   {on if s['sensor_bypass'] else 'off (ADM1032)'}")
    if "ntc" in s:
        ntc = s["ntc"]
        state = "on" if ntc["enabled"] else "off"
        print(f"NTC:             {state}, beta {ntc['beta']} K, R25 {ntc['r25']} ohms, series {ntc['series']} ohms")
    if "report_version" in s:
        version = s["report_version"]
        shown = f"{s['report_version_default']} (the build's)" if version == "auto" else version
        print(f"Reported version: {shown}")
    if "fan_ramp" in s:
        r = s["fan_ramp"]

        def glide(ms):
            return f"{ms} ms per step" if ms else "at once"

        print(f"Fan ramp up:     x{r['up']} (+{r['up']} warm, +{3 * r['up']} hot), glide {glide(r['glide_up'])}")
        print(f"Fan ramp down:   -{r['down']} when cool, glide {glide(r['glide_down'])}")
    for t in s["thresholds"]:
        print(f"Threshold {t['index']}:     {t['value']} C (default {t['default']}, min {t['min']})  {t['label']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (default: the SMCX found by its USB name)")
    parser.add_argument(
        "commands",
        nargs="+",
        help="get | export FILE | import FILE | set-brightness N | set-fade on|off | set-boot-challenge on|off | set-av-pack detect|0-6 | set-log-level LEVEL | set-no-dvd on|off | set-sniffer on|off | set-ntc on|off | set-ntc-beta K | set-ntc-r25 OHMS | set-ntc-series OHMS | set-fan-ramp-up 1-5 | set-fan-ramp-down 1-5 | set-fan-glide-up MS | set-fan-glide-down MS | set-sensor-bypass on|off | set-bypass-cpu-offset C | set-bypass-board-offset C | set-power-button on|off | set-eject-button on|off | set-bootsel-power on|off | set-rgb-mode off|boot|mirror | set-rgb-brightness N | set-threshold I C | set-ssid NAME | set-password PW | set-web on|off | set-report-version auto|P01|P11|D01|DXF|P2L | defaults | save | reboot | bootloader",
    )
    args = parser.parse_args()

    words = list(args.commands)
    try:
        smc = Smc(args.port or find_port())
        while words:
            name = words.pop(0)
            if name == "get":
                print_settings(smc.get())
            elif name == "export":
                path = words.pop(0)
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(export_settings(smc.get()), f, indent=2)
                    f.write("\n")
                print(f"Settings exported to {path}")
            elif name == "import":
                path = words.pop(0)
                with open(path, encoding="utf-8") as f:
                    data = json.load(f)
                current = smc.get()
                wanted, notes = import_settings(current, data)
                for note in notes:
                    print(note)
                commands = commands_for(current, wanted)
                for line in commands:
                    smc.command(line)
                print(f"Imported {path}: {len(commands)} changed" + ("" if commands else ", nothing to change"))
            elif name == "set-brightness":
                smc.command(f"set brightness {words.pop(0)}")
                print("LED brightness set")
            elif name in SIMPLE_SETTINGS:
                value = words.pop(0)
                smc.command(f"set {SIMPLE_SETTINGS[name]} {value}")
                print(f"{SIMPLE_SETTINGS[name]} {value}")
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
            elif name == "set-report-version":
                version = words.pop(0)
                smc.command(f"set report-version {version}")
                print(f"Reported version {version}")
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
    except (SmcError, OSError) as e:  # OSError: timeouts, port busy or gone, files
        sys.exit(f"Error: {e}")
    except ValueError as e:  # A settings file that doesn't fit (json errors too)
        sys.exit(f"Import failed: {e}")


if __name__ == "__main__":
    main()
