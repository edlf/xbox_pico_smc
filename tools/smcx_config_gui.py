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

import queue
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import font as tkfont
from tkinter import messagebox, ttk

sys.path.insert(0, str(Path(__file__).resolve().parent))
import smcx_config as cli

try:
    import sv_ttk
except ImportError:
    sv_ttk = None

TITLE = "SMCX config"

REPORT_VERSIONS = ("P01", "P11", "D01", "DXF", "P2L")

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


def printable(text):
    return all(0x20 <= ord(c) <= 0x7E for c in text)


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
        self.columnconfigure(0, weight=1, uniform="column")
        self.columnconfigure(1, weight=1, uniform="column")
        self.rowconfigure(5, weight=1)

        # Header: title, connection state, theme switch
        header = ttk.Frame(self)
        header.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 12))
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
        bar.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(0, 12))
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

        left = ttk.Frame(self)
        left.grid(row=2, column=0, sticky="nsew", padx=(0, 8))
        left.columnconfigure(0, weight=1)

        # LEDs
        leds = self.card(left, "LEDs", row=0, column=0, pady=(0, 12))
        self.brightness = tk.IntVar(value=100)
        self.slider(leds, 1, "Front panel", self.brightness)
        ttk.Separator(leds).grid(row=2, column=0, columnspan=3, sticky="ew", pady=10)
        ttk.Label(leds, text="RGB LED").grid(row=3, column=0, sticky="w", padx=(0, 12))
        self.rgb_mode = self.input(ttk.Combobox(leds, state="readonly", values=list(RGB_MODE_NAMES.values()), width=30))
        self.rgb_mode.grid(row=3, column=1, columnspan=2, sticky="ew", pady=4)
        self.rgb_brightness = tk.IntVar(value=100)
        self.slider(leds, 4, "RGB brightness", self.rgb_brightness)
        self.rgb_note = self.muted(leds, "")
        self.rgb_note.grid(row=5, column=0, columnspan=3, sticky="w")

        # Web interface
        wifi = self.card(left, "Web interface", row=1, column=0, pady=(0, 12))
        self.web = tk.BooleanVar(value=True)
        self.input(
            ttk.Checkbutton(
                wifi,
                text="Start at power-up (Pico W / Pico 2 W)",
                variable=self.web,
                style="Switch.TCheckbutton" if sv_ttk else "TCheckbutton",
            )
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

        # Version reported to the Xbox on SMBus 0x01
        xbox = self.card(left, "Reported version", row=2, column=0)
        ttk.Label(xbox, text="SMBus 0x01").grid(row=1, column=0, sticky="w", padx=(0, 12))
        self.report_version = self.input(ttk.Combobox(xbox, state="readonly", width=24))
        self.report_version.grid(row=1, column=1, columnspan=2, sticky="ew", pady=4)
        self.report_version_note = self.muted(xbox, "What the SMC tells the Xbox it is.", wraplength=360)
        self.report_version_note.grid(row=2, column=0, columnspan=3, sticky="w")

        # Fan thresholds: the P01/P11 ones until the Pico says otherwise
        self.fan = self.card(self, "Fan thresholds", row=2, column=1, padx=(8, 0))
        self.fan_rows = ttk.Frame(self.fan)
        self.fan_rows.grid(row=1, column=0, columnspan=3, sticky="nsew")
        self.fan_rows.columnconfigure(0, weight=1)
        self.fan_labels = []
        self.fan_wrap_job = None  # Pending re-wrap (after())
        self.fan_wrap_width = None  # Width the labels were last wrapped to
        self.fan_rows.bind("<Configure>", self.schedule_fan_wrap)
        self.thresholds = []  # (index, IntVar, min, default, value read)
        self.fan_rows_shown = []  # (label, Spinbox, range label, IntVar) per threshold
        self.build_fan_rows(PIC_THRESHOLDS)

        # Actions
        actions = ttk.Frame(self)
        actions.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(16, 0))
        self.read_button = ttk.Button(actions, text="Read again", command=self.read)
        self.defaults_button = ttk.Button(actions, text="Defaults", command=self.defaults)
        self.reboot_button = ttk.Button(actions, text="Reboot Pico", command=self.reboot)
        self.bootloader_button = ttk.Button(actions, text="Firmware update", command=self.bootloader)
        self.save_button = ttk.Button(
            actions,
            text="Save to flash",
            command=lambda: self.apply(save=True),
            style="Accent.TButton" if sv_ttk else "TButton",
        )
        self.apply_button = ttk.Button(actions, text="Apply", command=lambda: self.apply(save=False))
        for button in (self.read_button, self.defaults_button, self.reboot_button, self.bootloader_button):
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
        )

        # Status line
        self.status = ttk.Label(self, text="")
        self.status.grid(row=4, column=0, columnspan=2, sticky="w", pady=(10, 0))

        # Log: the commands sent and the Pico's log lines. Takes the height
        # left over when the window is made taller.
        log_frame = ttk.Frame(self)
        log_frame.grid(row=5, column=0, columnspan=2, sticky="nsew", pady=(8, 0))
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
        self.connect_button.configure(state="disabled" if self.busy else "normal")

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
        self.rgb_mode.set(RGB_MODE_NAMES.get(s.get("rgb_mode"), s.get("rgb_mode", "")))
        self.rgb_brightness.set(s.get("rgb_brightness", 100))
        self.rgb_note.configure(text="" if s["rgb_available"] else "No RGB LED in this build: kept, but ignored.")
        self.web.set(s.get("web", True))
        if "report_version" in s:
            auto = f"Same as the build ({s['report_version_default']})"
            self.report_version.configure(values=[auto, *REPORT_VERSIONS])
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
            self.thresholds.append((t["index"], var, t["min"], t["default"], t["value"]))

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
            self.thresholds.append((t["index"], var, t["min"], t["default"], t["value"]))
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

    def changes(self):
        """The commands for what differs from the last read, or raises ValueError."""
        s = self.settings
        commands = []
        if self.brightness.get() != s.get("brightness"):
            commands.append(f"set brightness {self.brightness.get()}")

        mode = next((key for key, name in RGB_MODE_NAMES.items() if name == self.rgb_mode.get()), None)
        if mode and mode != s.get("rgb_mode"):
            commands.append(f"set rgb-mode {mode}")
        if self.rgb_brightness.get() != s.get("rgb_brightness"):
            commands.append(f"set rgb-brightness {self.rgb_brightness.get()}")

        for index, var, low, high, old in self.thresholds:
            try:
                value = var.get()
            except tk.TclError:
                raise ValueError(f"Fan threshold {index} must be a number")
            if not low <= value <= high:
                raise ValueError(f"Fan threshold {index} must be {low}-{high}")
            if value != old:
                commands.append(f"set threshold {index} {value}")

        ssid = self.ssid.get()
        if ssid != s.get("ssid"):
            if not 1 <= len(ssid) <= 32 or not printable(ssid):
                raise ValueError("The Wi-Fi name must be 1-32 printable characters")
            commands.append(f"set ssid {ssid}")

        password = "" if self.board_password.get() else self.password.get()
        if password != s.get("password"):
            if password and (not 8 <= len(password) <= 63 or not printable(password)):
                raise ValueError("The Wi-Fi password must be 8-63 printable characters")
            commands.append(f"set password {password}" if password else "set password")

        if "report_version" in s:
            choice = self.report_version.get()
            version = "auto" if choice.startswith("Same as the build") else choice
            if version and version != s["report_version"]:
                commands.append(f"set report-version {version}")

        if self.web.get() != s.get("web"):
            commands.append(f"set web {'on' if self.web.get() else 'off'}")
        return commands

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
