"""Configure the SMCX over USB serial, in a window.

The same settings as tools/smcx_config.py, with the same rule: only with the
Pico out of the Xbox, powered from USB alone (config mode, see
docs/USB_Config.md).

    pip install pyserial
    pip install sv-ttk      (optional: the Windows 11 style, light and dark)
    python tools/smcx_config_gui.py

"Apply" sends the changes to the Pico, "Save to flash" also writes them: only
saved settings are kept when the Pico restarts.
"""

import copy
import json
import queue
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from tkinter import font as tkfont

sys.path.insert(0, str(Path(__file__).resolve().parent))
import smcx_config as cli

try:
    import sv_ttk
except ImportError:
    sv_ttk = None

TITLE = "SMCX config"


AV_PACK_DETECT = "From the AV connector"
# By name, in code order (the position gives the code back). Code 5 has no
# known pack, so it keeps its number.
AV_PACK_NAMES = [AV_PACK_DETECT] + ["Unknown (5)" if code == 5 else name for code, name in cli.AV_PACKS.items()]
LOG_LEVEL_NAMES = [level.capitalize() for level in cli.LOG_LEVELS]
RAMP_UP_NAMES = [f"x{n} (+{n} warm, +{3 * n} hot)" for n in range(1, 6)]
RAMP_DOWN_NAMES = [f"{n} step{'s' if n > 1 else ''} when cool" for n in range(1, 6)]

# The P01/P11 fan thresholds (src/smc_fan_pic.cpp, threshold_entries) with the
# P11 defaults, shown before connecting. The rows stay and only take the
# Pico's values when it has the same list; a P2L firmware has its own.
PIC_THRESHOLDS = [
    {"index": i, "label": label, "value": default, "default": default, "min": 40}
    for i, (label, default) in enumerate(
        [
            ("CPU overheat (above)", 88),
            ("Board overheat (above)", 75),
            ("CPU hot, +3 speed (above)", 67),
            ("Predicted CPU hot, +3 speed (above)", 86),
            ("Predicted CPU warm, +1 speed (above)", 67),
            ("Board warm, +1 speed (above)", 60),
            ("Predicted CPU cool, -1 speed (below)", 66),
            ("Board cool, -1 speed (below)", 57),
            ("CPU still overheated, shut down (above)", 97),
            ("Board still overheated, shut down (above)", 84),
        ]
    )
]

RGB_MODE_NAMES = {
    "off": "Off, even while booting",
    "boot": "Boot phases, then dark",
    "mirror": "Boot phases, then the front panel",
}

# Status and log colours per theme
COLOURS = {
    "light": {
        "ok": "#0f7b0f",
        "warn": "#9d5d00",
        "error": "#c42b1c",
        "muted": "#6b6b6b",
        "log_bg": "#fbfbfb",
        "log_fg": "#1c1c1c",
        "log_pico": "#6b6b6b",
    },
    "dark": {
        "ok": "#6ccb5f",
        "warn": "#fce100",
        "error": "#ff99a4",
        "muted": "#9d9d9d",
        "log_bg": "#272727",
        "log_fg": "#e4e4e4",
        "log_pico": "#9d9d9d",
    },
}


def system_dark():
    """Whether the OS uses a dark theme (Windows, macOS; light elsewhere)."""
    try:
        if sys.platform == "win32":
            import winreg

            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER, r"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"
            ) as key:
                return winreg.QueryValueEx(key, "AppsUseLightTheme")[0] == 0
        if sys.platform == "darwin":
            result = subprocess.run(
                ["defaults", "read", "-g", "AppleInterfaceStyle"],
                capture_output=True,
                text=True,
                timeout=2,
                check=False,
            )
            return result.stdout.strip() == "Dark"
    except (OSError, subprocess.SubprocessError):
        pass
    return False


def dark_title_bar(root, dark):
    """Windows 10/11: matches the title bar to the theme."""
    if sys.platform != "win32":
        return
    try:
        import ctypes

        root.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(root.winfo_id())
        value = ctypes.c_int(1 if dark else 0)
        for attribute in (20, 19):  # DWMWA_USE_IMMERSIVE_DARK_MODE, before Windows 10 20H1
            if (
                ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attribute, ctypes.byref(value), ctypes.sizeof(value))
                == 0
            ):
                break
    except (OSError, AttributeError):
        pass


class App(ttk.Frame):
    def __init__(self, root, theme=None):
        super().__init__(root, padding=16)
        self.root = root
        self.smc = None
        self.settings = None  # Last `get`
        self.unsaved = False  # Applied to the Pico but not saved
        self.busy = False
        self.status_kind = "info"
        self.inputs = []  # Widgets that are off while not connected

        # Serial work runs on one thread, so the window never waits on the Pico
        self.jobs = queue.Queue()
        self.events = queue.Queue()
        threading.Thread(target=self.worker, daemon=True).start()

        self.dark = tk.BooleanVar(value=system_dark() if theme is None else theme == "dark")
        self.apply_theme()
        self.make_fonts()
        self.build()
        self.apply_theme()  # Again, for the widgets built since
        self.set_connected(False)
        self.refresh_ports()
        self.root.after(50, self.poll_events)

    # ---- Look -----------------------------------------------------------

    @property
    def colours(self):
        return COLOURS["dark" if self.dark.get() and sv_ttk else "light"]

    def apply_theme(self):
        if sv_ttk:
            sv_ttk.set_theme("dark" if self.dark.get() else "light", self.root)
            dark_title_bar(self.root, self.dark.get())
        if hasattr(self, "log"):
            c = self.colours
            self.log.configure(background=c["log_bg"], foreground=c["log_fg"], insertbackground=c["log_fg"])
            self.log.tag_configure("pico", foreground=c["log_pico"])
            self.log.tag_configure("error", foreground=c["error"])
            self.style_status()
            for label in self.muted_labels:
                label.configure(foreground=c["muted"])

    def make_fonts(self):
        if sv_ttk:
            self.font_title, self.font_heading, self.font_caption = (
                "SunValleySubtitleFont",
                "SunValleyBodyStrongFont",
                "SunValleyCaptionFont",
            )
            self.font_log = ("Cascadia Mono", 9) if "Cascadia Mono" in tkfont.families() else ("Consolas", 9)
            return
        base = tkfont.nametofont("TkDefaultFont").actual()
        self.font_title = tkfont.Font(family=base["family"], size=base["size"] + 6, weight="bold")
        self.font_heading = tkfont.Font(family=base["family"], size=base["size"], weight="bold")
        self.font_caption = tkfont.Font(family=base["family"], size=max(base["size"] - 1, 8))
        self.font_log = tkfont.nametofont("TkFixedFont")

    def card(self, parent, title, **grid):
        """A titled card (sv-ttk Card.TFrame, a plain frame without sv-ttk)."""
        frame = ttk.Frame(parent, style="Card.TFrame" if sv_ttk else "TFrame", padding=(16, 12))
        frame.grid(sticky="nsew", **grid)
        frame.columnconfigure(1, weight=1)
        ttk.Label(frame, text=title, font=self.font_heading).grid(
            row=0, column=0, columnspan=3, sticky="w", pady=(0, 8)
        )
        return frame

    def muted(self, parent, text, **kw):
        label = ttk.Label(parent, text=text, font=self.font_caption, **kw)
        self.muted_labels.append(label)
        label.configure(foreground=self.colours["muted"])
        return label

    def input(self, widget):
        self.inputs.append(widget)
        return widget

    # ---- Layout ---------------------------------------------------------

    def build(self):
        self.muted_labels = []
        self.grid(sticky="nsew")
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        for column in range(3):
            self.columnconfigure(column, weight=1, uniform="column")
        self.rowconfigure(5, weight=1)

        # Header: title, connection state, theme switch
        header = ttk.Frame(self)
        header.grid(row=0, column=0, columnspan=3, sticky="ew", pady=(0, 12))
        header.columnconfigure(1, weight=1)
        ttk.Label(header, text="SMCX", font=self.font_title).grid(row=0, column=0, rowspan=2, sticky="w", padx=(0, 16))
        self.connection = ttk.Label(header, text="", font=self.font_heading)
        self.connection.grid(row=0, column=1, sticky="w")
        self.firmware = self.muted(header, "")
        self.firmware.grid(row=1, column=1, sticky="w")
        if sv_ttk:
            ttk.Checkbutton(
                header, text="Dark", style="Switch.TCheckbutton", variable=self.dark, command=self.apply_theme
            ).grid(row=0, column=2, rowspan=2, sticky="e")

        # Connection
        bar = ttk.Frame(self)
        bar.grid(row=1, column=0, columnspan=3, sticky="ew", pady=(0, 12))
        ttk.Label(bar, text="Port").pack(side="left", padx=(0, 6))
        self.port = ttk.Combobox(bar, width=12, state="readonly")
        self.port.pack(side="left")
        self.refresh_button = ttk.Button(bar, text="Refresh", command=self.refresh_ports)
        self.refresh_button.pack(side="left", padx=6)
        self.connect_button = ttk.Button(bar, text="Connect", command=self.toggle_connection)
        self.connect_button.pack(side="left")
        # Off: only ports named "SMCX" over USB. On: every port, for firmware
        # from before the USB name
        self.all_ports = tk.BooleanVar(value=False)
        self.all_ports_check = ttk.Checkbutton(
            bar, text="Show all ports", variable=self.all_ports, command=self.refresh_ports
        )
        self.all_ports_check.pack(side="left", padx=12)

        switch = "Switch.TCheckbutton" if sv_ttk else "TCheckbutton"

        # Left column: the front panel, the RGB LED, the web interface
        left = ttk.Frame(self)
        left.grid(row=2, column=0, sticky="nsew", padx=(0, 8))
        left.columnconfigure(0, weight=1)

        # Front panel: its LEDs and buttons. Without the power button BOOTSEL
        # has to be one: the switch then shows on and can't be changed
        # (bootsel_choice keeps what was picked, for when the power button is back).
        front = self.card(left, "Front panel", row=0, column=0, pady=(0, 12))
        self.brightness = tk.IntVar(value=100)
        self.slider(front, 1, "LED brightness", self.brightness)
        self.fade = tk.BooleanVar(value=True)
        self.fade_check = self.input(
            ttk.Checkbutton(front, text="LEDs fade in at power on, out at power off", variable=self.fade, style=switch)
        )
        self.fade_check.grid(row=2, column=0, columnspan=3, sticky="w", pady=(4, 0))
        ttk.Separator(front).grid(row=3, column=0, columnspan=3, sticky="ew", pady=10)
        self.power_button = tk.BooleanVar(value=True)
        self.eject_button = tk.BooleanVar(value=True)
        self.bootsel = tk.BooleanVar(value=False)
        self.bootsel_choice = False
        self.input(
            ttk.Checkbutton(
                front, text="Power button", variable=self.power_button, style=switch, command=self.update_bootsel
            )
        ).grid(row=4, column=0, columnspan=3, sticky="w")
        self.input(ttk.Checkbutton(front, text="Eject button", variable=self.eject_button, style=switch)).grid(
            row=5, column=0, columnspan=3, sticky="w", pady=(6, 0)
        )
        self.bootsel_check = ttk.Checkbutton(
            front,
            text="Pico BOOTSEL button as a power button",
            variable=self.bootsel,
            style=switch,
            # Only clickable with the power button on
            command=lambda: setattr(self, "bootsel_choice", self.bootsel.get()),
        )
        self.bootsel_check.grid(row=6, column=0, columnspan=3, sticky="w", pady=(6, 0))
        self.bootsel_note = self.muted(front, "", wraplength=380)
        self.bootsel_note.grid(row=7, column=0, columnspan=3, sticky="w")

        # RGB LED
        rgb = self.card(left, "RGB LED", row=1, column=0, pady=(0, 12))
        ttk.Label(rgb, text="Shows").grid(row=1, column=0, sticky="w", padx=(0, 12))
        self.rgb_mode = self.input(ttk.Combobox(rgb, state="readonly", values=list(RGB_MODE_NAMES.values()), width=30))
        self.rgb_mode.grid(row=1, column=1, columnspan=2, sticky="ew", pady=4)
        self.rgb_brightness = tk.IntVar(value=100)
        self.slider(rgb, 2, "Brightness", self.rgb_brightness)
        self.rgb_note = self.muted(rgb, "")
        self.rgb_note.grid(row=3, column=0, columnspan=3, sticky="w")

        # Web interface
        wifi = self.card(left, "Web interface", row=2, column=0)
        self.web = tk.BooleanVar(value=False)
        self.input(
            ttk.Checkbutton(wifi, text="Start at power-up (Pico W / Pico 2 W)", variable=self.web, style=switch)
        ).grid(row=1, column=0, columnspan=3, sticky="w", pady=(0, 6))
        ttk.Label(wifi, text="Wi-Fi name").grid(row=2, column=0, sticky="w", padx=(0, 12))
        self.ssid = tk.StringVar()
        self.input(ttk.Entry(wifi, textvariable=self.ssid)).grid(row=2, column=1, columnspan=2, sticky="ew", pady=4)
        ttk.Label(wifi, text="Password").grid(row=3, column=0, sticky="w", padx=(0, 12))
        self.password = tk.StringVar()
        self.password_entry = ttk.Entry(wifi, textvariable=self.password, show="•")
        self.password_entry.grid(row=3, column=1, sticky="ew", pady=4)
        self.show_password = tk.BooleanVar(value=False)
        self.input(
            ttk.Checkbutton(
                wifi,
                text="Show",
                variable=self.show_password,
                command=lambda: self.password_entry.configure(show="" if self.show_password.get() else "•"),
            )
        ).grid(row=3, column=2, padx=(8, 0))
        self.board_password = tk.BooleanVar(value=True)
        self.input(
            ttk.Checkbutton(
                wifi,
                text="Use the password made from the board ID",
                variable=self.board_password,
                command=self.board_password_changed,
            )
        ).grid(row=4, column=0, columnspan=3, sticky="w", pady=(4, 0))
        self.board_password_value = ""  # From `get`, empty with firmware that doesn't send it
        self.typed_password = ""  # The other password, kept while the board ID one is shown
        self.board_password_note = self.muted(wifi, "Different for every Pico.")
        self.board_password_note.grid(row=5, column=0, columnspan=3, sticky="w", padx=(28, 0))
        self.web_note = self.muted(wifi, "")
        self.web_note.grid(row=6, column=0, columnspan=3, sticky="w")

        # Middle column: what the SMC tells the Xbox it is, how it treats the
        # Xbox, the NTC thermistor (and the sensor bypass that uses it), logging
        middle = ttk.Frame(self)
        middle.grid(row=2, column=1, sticky="nsew", padx=8)
        middle.columnconfigure(0, weight=1)
        smc = self.card(middle, "SMC", row=0, column=0, pady=(0, 12))
        ttk.Label(smc, text="Reported version").grid(row=1, column=0, sticky="w", padx=(0, 12))
        self.report_version = self.input(ttk.Combobox(smc, state="readonly", width=24))
        self.report_version.grid(row=1, column=1, columnspan=2, sticky="ew", pady=4)
        self.report_version_note = self.muted(smc, "What the SMC tells the Xbox it is (SMBus 0x01).", wraplength=360)
        self.report_version_note.grid(row=2, column=0, columnspan=3, sticky="w")

        xbox = self.card(middle, "Enhancements", row=1, column=0, pady=(0, 12))
        ttk.Label(xbox, text="AV pack").grid(row=1, column=0, sticky="w", padx=(0, 12))
        self.av_pack = self.input(ttk.Combobox(xbox, state="readonly", values=AV_PACK_NAMES, width=24))
        self.av_pack.grid(row=1, column=1, columnspan=2, sticky="ew", pady=4)
        self.av_pack.set(AV_PACK_DETECT)
        self.no_dvd = tk.BooleanVar(value=False)
        self.input(
            ttk.Checkbutton(
                xbox, text="No DVD drive (tray reported closed and empty)", variable=self.no_dvd, style=switch
            )
        ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(6, 0))
        self.boot_challenge = tk.BooleanVar(value=False)
        self.input(
            ttk.Checkbutton(
                xbox, text="Enforce the boot challenge (reset on a failure)", variable=self.boot_challenge, style=switch
            )
        ).grid(row=3, column=0, columnspan=3, sticky="w", pady=(6, 0))

        # The optional NTC thermistor, and the sensor bypass that reads it
        ntc = self.card(middle, "NTC thermistor", row=2, column=0, pady=(0, 12))
        self.ntc = tk.BooleanVar(value=False)
        self.input(ttk.Checkbutton(ntc, text="Fitted, on GP28", variable=self.ntc, style=switch)).grid(
            row=1, column=0, columnspan=3, sticky="w", pady=(0, 4)
        )
        self.ntc_values = {}  # "beta", "r25", "series" -> IntVar
        for row, (key, text, step, unit) in enumerate(
            [
                ("beta", "Beta", 50, "K"),
                ("r25", "R at 25 °C", 100, "ohms"),
                ("series", "Series resistor", 100, "ohms, from 3.3 V"),
            ],
            start=2,
        ):
            low, high = cli.NTC_RANGES[key]
            ttk.Label(ntc, text=text).grid(row=row, column=0, sticky="w", padx=(0, 12))
            var = tk.IntVar(value={"beta": 3950}.get(key, 10000))
            self.input(ttk.Spinbox(ntc, from_=low, to=high, increment=step, textvariable=var, width=9)).grid(
                row=row, column=1, sticky="w", pady=2
            )
            self.muted(ntc, unit).grid(row=row, column=2, sticky="w", padx=(8, 0))
            self.ntc_values[key] = var
        # Shut down when the NTC gets too hot (an NTC on something the ADM1032 doesn't see)
        shutdown = ttk.Frame(ntc)
        shutdown.grid(row=5, column=0, columnspan=3, sticky="w", pady=(6, 0))
        self.ntc_shutdown = tk.BooleanVar(value=False)
        self.ntc_shutdown_temp = tk.IntVar(value=80)  # The firmware's default
        self.input(ttk.Checkbutton(shutdown, text="Shut down at", variable=self.ntc_shutdown, style=switch)).grid(
            row=0, column=0, sticky="w"
        )
        low, high = cli.NTC_SHUTDOWN_RANGE
        self.input(
            ttk.Spinbox(shutdown, from_=low, to=high, increment=1, textvariable=self.ntc_shutdown_temp, width=4)
        ).grid(row=0, column=1, padx=(8, 0))
        self.muted(shutdown, "°C (not with the sensor bypass)").grid(row=0, column=2, padx=(6, 0))
        ttk.Separator(ntc).grid(row=6, column=0, columnspan=3, sticky="ew", pady=10)
        self.sensor_bypass = tk.BooleanVar(value=False)
        self.input(
            ttk.Checkbutton(
                ntc,
                text="Sensor bypass: CPU from the NTC, board from the Pico",
                variable=self.sensor_bypass,
                style=switch,
            )
        ).grid(row=7, column=0, columnspan=3, sticky="w")
        # Added to the bypass's readings, both on one row
        offsets = ttk.Frame(ntc)
        offsets.grid(row=8, column=0, columnspan=3, sticky="w", pady=(4, 0))
        self.cpu_offset = tk.IntVar(value=10)  # The firmware's default
        self.board_offset = tk.IntVar(value=10)
        for column, (text, var) in enumerate([("Add: CPU", self.cpu_offset), ("Board", self.board_offset)]):
            ttk.Label(offsets, text=text).grid(row=0, column=column * 3, sticky="w", padx=(0 if column == 0 else 16, 6))
            self.input(
                ttk.Spinbox(
                    offsets,
                    from_=-cli.MAX_BYPASS_OFFSET,
                    to=cli.MAX_BYPASS_OFFSET,
                    increment=1,
                    textvariable=var,
                    width=4,
                )
            ).grid(row=0, column=column * 3 + 1)
            self.muted(offsets, "°C").grid(row=0, column=column * 3 + 2, padx=(4, 0))
        self.muted(
            ntc,
            "For a bad ADM1032, which isn't read then. Needs the NTC on and fitted: without a reading the fan "
            "control sees a dead sensor and shuts the Xbox down.",
            wraplength=380,
        ).grid(row=9, column=0, columnspan=3, sticky="w")

        # Logging
        diag = self.card(middle, "Diagnostics", row=3, column=0)
        ttk.Label(diag, text="Log level").grid(row=1, column=0, sticky="w", padx=(0, 12))
        self.log_level = self.input(ttk.Combobox(diag, state="readonly", values=LOG_LEVEL_NAMES, width=12))
        self.log_level.grid(row=1, column=1, sticky="w", pady=4)
        self.log_level.set("Info")
        self.sniffer = tk.BooleanVar(value=True)
        self.input(
            ttk.Checkbutton(
                diag, text="Log the video encoder the CPU talks to (sniffer)", variable=self.sniffer, style=switch
            )
        ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(6, 0))

        # Fan thresholds: the P01/P11 ones until the Pico says otherwise
        right = ttk.Frame(self)
        right.grid(row=2, column=2, sticky="nsew", padx=(8, 0))
        right.columnconfigure(0, weight=1)
        right.rowconfigure(0, weight=1)
        self.fan = self.card(right, "Fan", row=0, column=0)
        self.fan_rows = ttk.Frame(self.fan)
        self.fan_rows.grid(row=1, column=0, columnspan=3, sticky="nsew")
        self.fan_rows.columnconfigure(0, weight=1)
        self.fan_labels = []
        self.fan_wrap_job = None  # Pending re-wrap (after())
        self.fan_wrap_width = None  # Width the labels were last wrapped to
        self.fan_rows.bind("<Configure>", self.schedule_fan_wrap)
        self.thresholds = []  # (index, IntVar) per row
        self.fan_rows_shown = []  # (label, Spinbox, range label, IntVar) per threshold
        self.build_fan_rows(PIC_THRESHOLDS)

        # How quickly the speed follows the fan control (every 20 s)
        ttk.Separator(self.fan).grid(row=2, column=0, columnspan=3, sticky="ew", pady=10)
        self.ramp_up = self.input(ttk.Combobox(self.fan, state="readonly", values=RAMP_UP_NAMES, width=22))
        self.ramp_down = self.input(ttk.Combobox(self.fan, state="readonly", values=RAMP_DOWN_NAMES, width=22))
        self.glide_up = tk.IntVar(value=0)
        self.glide_down = tk.IntVar(value=0)
        for row, (text, box) in enumerate([("Ramp up", self.ramp_up), ("Ramp down", self.ramp_down)], start=3):
            ttk.Label(self.fan, text=text).grid(row=row, column=0, sticky="w", padx=(0, 12))
            box.grid(row=row, column=1, columnspan=2, sticky="ew", pady=3)
            box.current(0)
        for row, (text, var) in enumerate([("Glide up", self.glide_up), ("Glide down", self.glide_down)], start=5):
            ttk.Label(self.fan, text=text).grid(row=row, column=0, sticky="w", padx=(0, 12))
            self.input(ttk.Spinbox(self.fan, from_=0, to=1000, increment=20, textvariable=var, width=6)).grid(
                row=row, column=1, sticky="w", pady=3
            )
            self.muted(self.fan, "ms per speed step, 0: at once").grid(row=row, column=2, sticky="w", padx=(8, 0))
        self.muted(
            self.fan,
            "Ramp: how far each 20 s decision moves the speed. Glide: how quickly the fan gets there.",
            wraplength=360,
        ).grid(row=7, column=0, columnspan=3, sticky="w", pady=(6, 0))

        # Actions
        actions = ttk.Frame(self)
        actions.grid(row=3, column=0, columnspan=3, sticky="ew", pady=(16, 0))
        self.read_button = ttk.Button(actions, text="Read again", command=self.read)
        self.defaults_button = ttk.Button(actions, text="Defaults", command=self.defaults)
        self.reboot_button = ttk.Button(actions, text="Reboot Pico", command=self.reboot)
        self.bootloader_button = ttk.Button(actions, text="Firmware update", command=self.bootloader)
        self.export_button = ttk.Button(actions, text="Export...", command=self.export)
        self.import_button = ttk.Button(actions, text="Import...", command=self.import_)
        self.save_button = ttk.Button(
            actions,
            text="Save to flash",
            command=lambda: self.apply(save=True),
            style="Accent.TButton" if sv_ttk else "TButton",
        )
        self.apply_button = ttk.Button(actions, text="Apply", command=lambda: self.apply(save=False))
        for button in (
            self.read_button,
            self.defaults_button,
            self.reboot_button,
            self.bootloader_button,
            self.export_button,
            self.import_button,
        ):
            button.pack(side="left", padx=(0, 6))
        self.save_button.pack(side="right")
        self.apply_button.pack(side="right", padx=6)
        self.action_buttons = (
            self.read_button,
            self.apply_button,
            self.save_button,
            self.defaults_button,
            self.reboot_button,
            self.bootloader_button,
            self.export_button,
            self.import_button,
        )

        # Status line
        self.status = ttk.Label(self, text="")
        self.status.grid(row=4, column=0, columnspan=3, sticky="w", pady=(10, 0))

        # Log: the commands sent and the Pico's log lines. Takes the height
        # left over when the window is made taller.
        log_frame = ttk.Frame(self)
        log_frame.grid(row=5, column=0, columnspan=3, sticky="nsew", pady=(8, 0))
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(1, weight=1)
        ttk.Label(log_frame, text="Log", font=self.font_heading).grid(row=0, column=0, sticky="w", pady=(0, 4))
        self.log = tk.Text(
            log_frame,
            height=9,
            state="disabled",
            font=self.font_log,
            relief="flat",
            borderwidth=0,
            highlightthickness=0,
            padx=8,
            pady=6,
            wrap="word",
        )
        self.log.grid(row=1, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(log_frame, orient="vertical", command=self.log.yview)
        scroll.grid(row=1, column=1, sticky="ns")
        self.log.configure(yscrollcommand=scroll.set)

    def slider(self, parent, row, text, variable):
        ttk.Label(parent, text=text).grid(row=row, column=0, sticky="w", padx=(0, 12))
        value = ttk.Label(parent, width=5, anchor="e")
        scale = self.input(ttk.Scale(parent, from_=1, to=100, command=lambda v: variable.set(round(float(v)))))
        scale.grid(row=row, column=1, sticky="ew", pady=4)
        value.grid(row=row, column=2, sticky="e", padx=(8, 0))

        def changed(*_):
            # Moves the slider when the value is set from elsewhere; setting it
            # from the slider lands here too, then the values already match
            if round(scale.get()) != variable.get():
                scale.set(variable.get())
            value.configure(text=f"{variable.get()}%")

        variable.trace_add("write", changed)
        changed()

    def board_password_changed(self):
        """Shows the board ID password while it's ticked (read-only), the
        typed one otherwise."""
        if self.board_password.get():
            self.typed_password = self.password.get()
            self.password.set(self.board_password_value)
        else:
            self.password.set(self.typed_password)
        self.update_password_state()

    def update_password_state(self):
        if self.smc is None or self.busy:
            state = "disabled"
        else:
            state = "readonly" if self.board_password.get() else "normal"  # The board ID one can be copied, not changed
        self.password_entry.configure(state=state)

    def write_log(self, text, tag=None):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n", tag)
        self.log.see("end")
        self.log.configure(state="disabled")

    def set_status(self, text, kind="info"):
        """kind: info, ok (green), warn (unsaved, orange) or error (red)."""
        self.status_kind = kind
        self.status.configure(text=text)
        self.style_status()

    def style_status(self):
        colour = self.colours.get(self.status_kind)
        self.status.configure(foreground=colour or "")

    def set_connected(self, connected):
        self.connect_button.configure(text="Disconnect" if connected else "Connect")
        if sv_ttk:
            self.connect_button.configure(style="TButton" if connected else "Accent.TButton")
        self.port.configure(state="disabled" if connected else "readonly")
        self.refresh_button.configure(state="disabled" if connected else "normal")
        self.all_ports_check.configure(state="disabled" if connected else "normal")
        if not connected:
            self.connection.configure(text="Not connected")
            self.firmware.configure(text="Plug the Pico in over USB, out of the Xbox, then Connect.")
        self.update_buttons()

    def update_buttons(self):
        enabled = self.smc is not None and not self.busy
        for button in self.action_buttons:
            button.configure(state="normal" if enabled else "disabled")
        spinboxes = [w for w in self.fan_rows.winfo_children() if isinstance(w, ttk.Spinbox)]
        for widget in self.inputs + spinboxes:
            widget.state(["!disabled"] if enabled else ["disabled"])
        self.update_password_state()
        self.update_bootsel()
        self.connect_button.configure(state="disabled" if self.busy else "normal")

    def update_bootsel(self):
        """BOOTSEL is the power button while the front panel's is off: shown on,
        and fixed. Otherwise the switch is what was picked."""
        enabled = self.smc is not None and not self.busy
        if self.power_button.get():
            self.bootsel.set(self.bootsel_choice)
            self.bootsel_check.state(["!disabled"] if enabled else ["disabled"])
            self.bootsel_note.configure(text="")
        else:
            self.bootsel.set(True)
            self.bootsel_check.state(["disabled"])
            self.bootsel_note.configure(
                text="Needed while the power button is off: BOOTSEL powers the Xbox on and off."
            )

    # ---- Serial work ----------------------------------------------------

    def worker(self):
        while True:
            work, done = self.jobs.get()
            try:
                self.events.put((done, work(), None))
            except Exception as e:  # noqa: BLE001 - shown in the window, the thread carries on
                self.events.put((done, None, e))

    def run(self, work, done, status):
        """work() on the serial thread, then done(result) here."""
        self.busy = True
        self.update_buttons()
        self.set_status(status)
        self.root.configure(cursor="watch")
        self.jobs.put((work, done))

    def poll_events(self):
        try:
            while True:
                done, result, error = self.events.get_nowait()
                if done == "log":
                    self.write_log(result, "pico")
                    continue
                self.busy = False
                self.root.configure(cursor="")
                self.update_buttons()
                if error is not None:
                    self.write_log(f"Error: {error}", "error")
                    self.set_status(f"Error: {error}", "error")
                    if isinstance(error, OSError) and not isinstance(error, TimeoutError):
                        self.drop_connection()  # Unplugged
                        self.set_status(f"Error: {error}", "error")
                else:
                    done(result)
        except queue.Empty:
            pass
        self.root.after(50, self.poll_events)

    def send(self, line):
        """On the serial thread: one command, logged."""
        shown = "set password ***" if line.startswith("set password ") else line
        self.events.put(("log", f"> {shown}", None))
        return self.smc.command(line)

    # ---- Actions --------------------------------------------------------

    def refresh_ports(self):
        try:
            smcs = cli.pico_ports()
            others = (
                [p.device for p in cli.list_ports.comports() if p.device not in smcs] if self.all_ports.get() else []
            )
        except cli.SmcError as e:
            messagebox.showerror(TITLE, str(e))
            return
        self.port.configure(values=smcs + others)
        if smcs:
            self.port.set(smcs[0])
            self.set_status(f"SMCX found on {smcs[0]}" + (f" ({len(smcs)} found)" if len(smcs) > 1 else ""))
        elif others:
            self.port.set(others[0])
            self.set_status("No SMCX found. Pick its port only if it runs older firmware.")
        else:
            self.port.set("")
            self.set_status("No SMCX found. Plug the Pico in over USB, out of the Xbox.")

    def toggle_connection(self):
        if self.smc:
            self.drop_connection()
            return
        port = self.port.get()
        if not port:
            self.set_status("Pick a port first", "warn")
            return

        def work():
            smc = cli.Smc(port, on_log=lambda line: self.events.put(("log", line, None)))
            self.smc = smc
            try:
                return self.send("get")
            except Exception:
                self.smc = None
                smc.close()
                raise

        def done(lines):
            self.write_log(f"Connected to {port}")
            self.set_connected(True)
            self.connection.configure(text=f"Connected to {port}")
            self.show_settings(cli.parse_settings(lines))
            self.unsaved = False
            self.set_status("Settings read from the Pico")

        self.run(work, done, f"Connecting to {port}...")

    def drop_connection(self):
        if self.smc:
            try:
                self.smc.close()
            except OSError:
                pass
            self.smc = None
            self.write_log("Disconnected")
        self.set_connected(False)
        self.set_status("")

    def read(self):
        def done(lines):
            self.show_settings(cli.parse_settings(lines))
            if self.unsaved:
                self.set_status("Settings read. Applied changes aren't saved to flash yet.", "warn")
            else:
                self.set_status("Settings read from the Pico")

        self.run(lambda: self.send("get"), done, "Reading...")

    def show_settings(self, s):
        self.settings = s
        build = f" · build {s['git']}" if s.get("git") else ""
        self.firmware.configure(text=f"Firmware {s.get('firmware', '?')}{build}")
        self.brightness.set(s.get("brightness", 100))
        self.fade.set(s.get("fade", True))
        self.rgb_mode.set(RGB_MODE_NAMES.get(s.get("rgb_mode"), s.get("rgb_mode", "")))
        self.rgb_brightness.set(s.get("rgb_brightness", 100))
        self.rgb_note.configure(text="" if s["rgb_available"] else "No RGB LED in this build: kept, but ignored.")
        self.web.set(s.get("web", False))
        if "fan_ramp" in s:
            ramp = s["fan_ramp"]
            self.ramp_up.current(ramp["up"] - 1)
            self.ramp_down.current(ramp["down"] - 1)
            self.glide_up.set(ramp["glide_up"])
            self.glide_down.set(ramp["glide_down"])
        if "av_pack" in s:
            self.av_pack.set(AV_PACK_DETECT if s["av_pack"] is None else AV_PACK_NAMES[s["av_pack"] + 1])
        self.no_dvd.set(s.get("no_dvd", False))
        self.boot_challenge.set(s.get("boot_challenge", False))
        self.power_button.set(s.get("power_button", True))
        self.eject_button.set(s.get("eject_button", True))
        self.bootsel_choice = s.get("bootsel_power", False)  # Older firmware: no BOOTSEL setting
        self.bootsel.set(self.bootsel_choice)
        if "log_level" in s:
            self.log_level.set(s["log_level"].capitalize())
        self.sniffer.set(s.get("sniffer", True))
        self.sensor_bypass.set(s.get("sensor_bypass", False))
        if "ntc_shutdown" in s:
            self.ntc_shutdown.set(s["ntc_shutdown"]["enabled"])
            self.ntc_shutdown_temp.set(s["ntc_shutdown"]["temp"])
        if "bypass_offsets" in s:
            self.cpu_offset.set(s["bypass_offsets"]["cpu"])
            self.board_offset.set(s["bypass_offsets"]["board"])
        if "ntc" in s:
            self.ntc.set(s["ntc"]["enabled"])
            for key in ("beta", "r25", "series"):
                self.ntc_values[key].set(s["ntc"][key])
        if "report_version" in s:
            auto = f"Same as the build ({s['report_version_default']})"
            self.report_version.configure(values=[auto, *cli.REPORT_VERSIONS])
            self.report_version.set(auto if s["report_version"] == "auto" else s["report_version"])
            self.report_version_note.configure(
                text=f"What the SMC tells the Xbox it is. It still behaves as {s.get('firmware', '?')} firmware."
            )
        else:
            self.report_version.configure(values=[])
            self.report_version.set("")
            self.report_version_note.configure(text="This firmware can't change it.")
        self.ssid.set(s.get("ssid", ""))
        self.board_password_value = s.get("board_password", "")
        self.board_password_note.configure(
            text="Different for every Pico."
            if self.board_password_value
            else "Shown in the Pico's boot log (this firmware doesn't report it)."
        )
        self.board_password.set(not s.get("password"))
        self.typed_password = s.get("password", "")
        self.password.set(self.typed_password or self.board_password_value)
        self.web_note.configure(text="" if s["web_available"] else "No web interface in this build: kept, but ignored.")

        self.show_thresholds(s["thresholds"])
        self.update_buttons()

    def show_thresholds(self, thresholds):
        """The Pico's thresholds: into the rows already there when it has the
        same ones (P01, P11 and the debug kits), new rows otherwise (P2L)."""
        if [t["label"] for t in thresholds] != [row[0]["text"] for row in self.fan_rows_shown]:
            self.build_fan_rows(thresholds)
            return
        self.thresholds = []
        for t, (_, spinbox, limits, var) in zip(thresholds, self.fan_rows_shown):
            spinbox.configure(from_=t["min"], to=t["default"])
            var.set(t["value"])
            limits.configure(text=f"°C, {t['min']}-{t['default']}")
            self.thresholds.append((t["index"], var))

    def build_fan_rows(self, thresholds):
        """One row per threshold: its name, a box for the value and its range"""
        for child in self.fan_rows.winfo_children():
            if child in self.muted_labels:
                self.muted_labels.remove(child)
            child.destroy()
        self.thresholds = []
        self.fan_rows_shown = []
        self.fan_labels = []  # (label, whole row width) wrapped to the card's width
        for row, t in enumerate(thresholds):
            # Every row as tall as the tallest, so the rows are evenly spaced
            self.fan_rows.rowconfigure(row, uniform="fan")
            label = ttk.Label(self.fan_rows, text=t["label"])
            label.grid(row=row, column=0, sticky="w", pady=4)
            self.fan_labels.append((label, False))
            var = tk.IntVar(value=t["value"])
            spinbox = ttk.Spinbox(self.fan_rows, from_=t["min"], to=t["default"], textvariable=var, width=4)
            spinbox.grid(row=row, column=1, padx=8, pady=4)
            limits = self.muted(self.fan_rows, f"°C, {t['min']}-{t['default']}")
            limits.grid(row=row, column=2, sticky="w", pady=4)
            self.fan_rows_shown.append((label, spinbox, limits, var))
            self.thresholds.append((t["index"], var))
        if self.thresholds:
            note = self.muted(self.fan_rows, "The firmware's default is the highest you can set each one.")
            note.grid(row=len(self.thresholds), column=0, columnspan=3, sticky="w", pady=(10, 0))
            self.fan_labels.append((note, True))
        self.fan_wrap_width = None
        self.wrap_fan_labels()

    def schedule_fan_wrap(self, event):
        """Re-wraps the threshold names once the resizing pauses. Re-wrapping
        on every resize event made Tk lay the whole window out again for each
        one, which lagged while dragging."""
        if event.width == self.fan_wrap_width:
            return  # Only the height changed, or our own re-wrap
        if self.fan_wrap_job is not None:
            self.root.after_cancel(self.fan_wrap_job)
        self.fan_wrap_job = self.root.after(100, self.wrap_fan_labels)

    def wrap_fan_labels(self):
        """Wraps the threshold names to the space the card has, so they're on
        one line when the window is wide enough."""
        self.fan_wrap_job = None
        width = self.fan_rows.winfo_width()
        if width <= 1 or not self.fan_labels:
            return
        self.fan_wrap_width = width
        others = sum(self.fan_rows.grid_bbox(column, 0)[2] for column in (1, 2))
        for label, whole_row in self.fan_labels:
            label.configure(wraplength=max(width if whole_row else width - others - 4, 80))

    def form(self):
        """The settings as the window shows them, in parse_settings()'s shape
        (the last read, with the boxes' values). Raises ValueError for a box
        that isn't a number; cli.commands_for() checks the rest."""

        def number(var, what):
            try:
                return var.get()
            except tk.TclError:
                raise ValueError(f"{what} must be a number")

        s = copy.deepcopy(self.settings)
        s["brightness"] = self.brightness.get()
        if "fade" in s:
            s["fade"] = self.fade.get()
        s["rgb_mode"] = next(
            (key for key, name in RGB_MODE_NAMES.items() if name == self.rgb_mode.get()), s.get("rgb_mode")
        )
        s["rgb_brightness"] = self.rgb_brightness.get()
        values = {index: number(var, f"Fan threshold {index}") for index, var in self.thresholds}
        for t in s["thresholds"]:
            t["value"] = values.get(t["index"], t["value"])
        s["ssid"] = self.ssid.get()
        s["password"] = "" if self.board_password.get() else self.password.get()
        if "fan_ramp" in s:
            s["fan_ramp"] = {
                "up": RAMP_UP_NAMES.index(self.ramp_up.get()) + 1,
                "down": RAMP_DOWN_NAMES.index(self.ramp_down.get()) + 1,
                "glide_up": number(self.glide_up, "The fan glide"),
                "glide_down": number(self.glide_down, "The fan glide"),
            }
        if "av_pack" in s:
            choice = self.av_pack.get()
            s["av_pack"] = None if choice == AV_PACK_DETECT else AV_PACK_NAMES.index(choice) - 1
        for key, var in (
            ("no_dvd", self.no_dvd),
            ("boot_challenge", self.boot_challenge),
            ("sniffer", self.sniffer),
            ("sensor_bypass", self.sensor_bypass),
            ("eject_button", self.eject_button),
            ("power_button", self.power_button),
        ):
            if key in s:
                s[key] = var.get()
        if "bootsel_power" in s:
            s["bootsel_power"] = self.bootsel_choice  # As picked: shown on anyway while the power button is off
        if "log_level" in s:
            s["log_level"] = self.log_level.get().lower()
        if "ntc" in s:
            s["ntc"] = {"enabled": self.ntc.get()}
            for key in ("beta", "r25", "series"):
                s["ntc"][key] = number(self.ntc_values[key], f"The NTC {key}")
        if "ntc_shutdown" in s:
            s["ntc_shutdown"] = {
                "enabled": self.ntc_shutdown.get(),
                "temp": number(self.ntc_shutdown_temp, "The NTC shutdown temperature"),
            }
        if "bypass_offsets" in s:
            s["bypass_offsets"] = {
                "cpu": number(self.cpu_offset, "The cpu offset"),
                "board": number(self.board_offset, "The board offset"),
            }
        if "report_version" in s:
            choice = self.report_version.get()
            s["report_version"] = "auto" if choice.startswith("Same as the build") else choice
        s["web"] = self.web.get()
        return s

    def changes(self):
        """The commands for what differs from the last read, or raises ValueError."""
        return cli.commands_for(self.settings, self.form())

    def export(self):
        """The settings as shown (applied or not) to a JSON file."""
        try:
            form = self.form()
            cli.commands_for(self.settings, form)  # Refuses what the firmware would
        except ValueError as e:
            messagebox.showerror(TITLE, str(e))
            return
        path = filedialog.asksaveasfilename(
            parent=self.root,
            title="Export the settings",
            defaultextension=".json",
            filetypes=[("SMCX settings", "*.json"), ("All files", "*.*")],
            initialfile=f"smcx-{self.settings.get('firmware', 'settings')}.json",
        )
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(cli.export_settings(form), f, indent=2)
                f.write("\n")
        except OSError as e:
            messagebox.showerror(TITLE, f"Couldn't write {path}: {e}")
            return
        self.write_log(f"Settings exported to {path}")
        self.set_status(f"Exported to {Path(path).name}")

    def import_(self):
        """A JSON file's settings into the window, to Apply or Save from there."""
        path = filedialog.askopenfilename(
            parent=self.root,
            title="Import settings",
            filetypes=[("SMCX settings", "*.json"), ("All files", "*.*")],
        )
        if not path:
            return
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            wanted, notes = cli.import_settings(self.settings, data)
            cli.commands_for(self.settings, wanted)  # Refuses what the firmware would
        except (OSError, ValueError) as e:  # ValueError: json too
            messagebox.showerror(TITLE, f"Couldn't import {Path(path).name}: {e}")
            return
        self.load_form(wanted)
        self.write_log(f"Settings imported from {path}")
        for note in notes:
            self.write_log(note, "error")
        self.set_status(f"Imported {Path(path).name}: Apply or Save to flash to send it to the Pico", "warn")

    def load_form(self, s):
        """Shows settings in the window without taking them as the Pico's:
        Apply then sends what differs from the last read."""
        read = self.settings
        self.show_settings(s)
        self.settings = read

    def apply(self, save):
        try:
            commands = self.changes()
        except ValueError as e:
            messagebox.showerror(TITLE, str(e))
            return
        if not commands and not save:
            self.set_status("Nothing changed")
            return

        def work():
            for line in commands:
                self.send(line)
            if save:
                self.send("save")
            return self.send("get")

        def done(lines):
            if save:
                self.unsaved = False
            elif commands:
                self.unsaved = True
            self.show_settings(cli.parse_settings(lines))
            if save:
                self.set_status("Saved to flash", "ok")
            else:
                self.set_status("Applied, not saved yet", "warn")

        self.run(work, done, "Saving..." if save else "Applying...")

    def defaults(self):
        if not messagebox.askyesno(
            TITLE, "Set everything back to the firmware's defaults?\n\nNothing is written to flash until you save."
        ):
            return

        def done(lines):
            self.unsaved = True
            self.show_settings(cli.parse_settings(lines))
            self.set_status("Back to the defaults, not saved yet", "warn")

        self.run(lambda: (self.send("defaults"), self.send("get"))[1], done, "Resetting...")

    def reboot(self):
        if self.unsaved and not messagebox.askyesno(
            TITLE, "Some changes aren't saved and will be lost. Reboot anyway?"
        ):
            return

        def done(_):
            self.unsaved = False
            self.drop_connection()
            self.set_status("Rebooted. Refresh and connect again.")

        self.run(lambda: self.send("reboot"), done, "Rebooting...")

    def bootloader(self):
        """Restarts the Pico into its USB bootloader (BOOTSEL), where it shows up
        as a drive to copy a new firmware file to. Only USB config mode has the
        command, so only a PC with the Pico plugged in can do this."""
        question = (
            "Restart the Pico into the bootloader for a firmware update?\n\n"
            "It shows up as a drive (RPI-RP2 or RP2350): copy SMCX.uf2 onto it, "
            "and the Pico restarts with the new firmware."
        )
        if self.unsaved:
            question += "\n\nSome changes aren't saved and will be lost."
        if not messagebox.askyesno(TITLE, question):
            return

        def done(_):
            self.unsaved = False
            self.drop_connection()
            self.set_status("In the bootloader: copy SMCX.uf2 to the RPI-RP2 / RP2350 drive.", "ok")

        self.run(lambda: self.send("bootloader"), done, "Restarting into the bootloader...")

    def close(self):
        if self.unsaved and not messagebox.askyesno(TITLE, "Some changes aren't saved to flash. Quit anyway?"):
            return
        if self.smc:
            try:
                self.smc.close()
            except OSError:
                pass
        if self.fan_wrap_job is not None:
            self.root.after_cancel(self.fan_wrap_job)
        self.root.destroy()


def main():
    root = tk.Tk()
    # Hidden until it's built: shown at once, the empty window would pop up
    # small and then grow, once per theme change and layout pass
    root.withdraw()
    root.title(TITLE)
    try:
        cli.require_pyserial()
    except cli.SmcError as e:
        messagebox.showerror(TITLE, str(e))
        return
    app = App(root)
    root.protocol("WM_DELETE_WINDOW", app.close)
    root.minsize(860, 600)
    # A fixed size from the start: left to follow its contents, the window
    # would change size again as the threshold names re-wrap, on connecting
    # and whenever the status line changes. It can still be resized by hand.
    # Kept on the screen (the log shrinks first), with room for the taskbar.
    root.update_idletasks()
    width = min(max(root.winfo_reqwidth(), 860), root.winfo_screenwidth() - 40)
    height = min(max(root.winfo_reqheight(), 600), root.winfo_screenheight() - 100)
    root.geometry(f"{width}x{height}")
    root.deiconify()
    root.mainloop()


if __name__ == "__main__":
    main()
