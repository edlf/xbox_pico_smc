"""tools/smcx_config.py: the `get` reply parser, the commands it sends, and the
serial handshake (against a fake Pico, no hardware)."""

import json
import re
import sys
import types

import pytest
import smcx_config as cli
from conftest import GET_REPLY, ROOT


def settings(*changes):
    """GET_REPLY with some lines replaced (by their first word), parsed."""
    lines = list(GET_REPLY)
    for change in changes:
        key = change.split(" ", 1)[0]
        lines = [change if line.split(" ", 1)[0] == key else line for line in lines]
    return cli.parse_settings(lines)


# ---- The parser -------------------------------------------------------------


def test_parses_every_line():
    s = cli.parse_settings(GET_REPLY)
    assert s["firmware"] == "P01"
    assert s["git"] == "abc1234"
    assert s["report_version"] == "auto" and s["report_version_default"] == "P01"
    assert s["brightness"] == 100 and s["fade"] is True
    assert s["rgb_mode"] == "boot" and s["rgb_available"] and s["rgb_brightness"] == 100
    assert s["ssid"] == "SMCX" and s["password"] == ""
    assert s["board_password"] == "smcx-E6614103E7123456"
    assert s["web"] is False and s["web_available"]
    assert s["boot_challenge"] is False and s["no_dvd"] is False and s["sniffer"] is False
    assert s["av_pack"] is None
    assert s["log_level"] == "info"
    assert s["ntc"] == {"enabled": False, "beta": 3950, "r25": 10000, "series": 10000}
    assert s["fan_ramp"] == {"up": 1, "down": 1, "glide_up": 0, "glide_down": 0}
    assert s["sensor_bypass"] is False
    assert s["bypass_offsets"] == {"cpu": 10, "board": 10}
    assert s["power_button"] is True and s["eject_button"] is True and s["bootsel_power"] is False
    assert s["thresholds"][1] == {
        "index": 1,
        "value": 78,
        "default": 78,
        "min": 40,
        "label": "CPU hot, +3 speed (at least)",
    }


def test_parses_the_variants():
    s = settings(
        "av-pack 4",
        "rgb-mode mirror (no RGB LED in this build, ignored)",
        "web on (no web interface in this build, ignored)",
        "bypass-offsets cpu -7 board 12",
        "password correct horse",
    )
    assert s["av_pack"] == 4
    assert s["rgb_mode"] == "mirror" and not s["rgb_available"]
    assert s["web"] is True and not s["web_available"]
    assert s["bypass_offsets"] == {"cpu": -7, "board": 12}
    assert s["password"] == "correct horse"


def test_print_settings_handles_a_full_reply(capsys):
    cli.print_settings(settings("power-button off", "sensor-bypass on"))
    out = capsys.readouterr().out
    assert "BOOTSEL button:  power button (needed: the power button is off)" in out
    assert "on (CPU from the NTC +10 C, board from the Pico +10 C)" in out


# ---- The firmware's side ----------------------------------------------------


def firmware_keys():
    """The first word of each line the firmware's `get` sends."""
    describe = (ROOT / "src" / "smc_settings.cpp").read_text(encoding="utf-8")
    usb = (ROOT / "src" / "usb_config.cpp").read_text(encoding="utf-8")
    keys = set(re.findall(r'emitf?\((?:emit, )?"([a-z-]+)[ "]', describe))
    keys |= set(re.findall(r'printf\("(firmware|git) ', usb))
    return keys


def test_sample_matches_the_firmware():
    """GET_REPLY has a line for every key the firmware sends, and nothing else,
    so a renamed or new setting shows up here."""
    assert {line.split(" ", 1)[0] for line in GET_REPLY} == firmware_keys()


def test_cli_settings_exist_in_the_firmware():
    """Each `set-<x>` the CLI offers is a `set <name>` the firmware's help lists."""
    usb = (ROOT / "src" / "usb_config.cpp").read_text(encoding="utf-8")
    for name in cli.SIMPLE_SETTINGS.values():
        assert f'"set {name} ' in usb, name


# ---- The handshake ----------------------------------------------------------


class Pico:
    """The firmware's USB config loop: one line buffer, answers per line."""

    def __init__(self, junk=b"", drop_replies=0):
        self.buffer = bytearray(junk)  # Left over in the line, as after a port probe
        self.drop_replies = drop_replies  # Replies lost (sent before the port was seen open)
        self.out = []
        self.commands = []

    def feed(self, data):
        for c in data:
            if c not in (10, 13):
                self.buffer.append(c)
                continue
            line = self.buffer.decode()
            self.buffer.clear()
            if not line.split():
                continue  # Empty lines are ignored
            self.commands.append(line)
            if line == "get":
                reply = [*GET_REPLY, "OK"]
            elif line == "save" or line.startswith("set "):
                reply = ["OK"]
            else:
                reply = ["ERR unknown command"]
            if self.drop_replies:
                self.drop_replies -= 1
                continue
            self.out += [(r + "\r\n").encode() for r in reply]


@pytest.fixture
def pico(monkeypatch):
    holder = types.SimpleNamespace(pico=Pico())

    class Serial:
        def __init__(self, port, baud, timeout):
            pass

        def reset_input_buffer(self):
            holder.pico.out.clear()

        def write(self, data):
            holder.pico.feed(data)

        def readline(self):
            return holder.pico.out.pop(0) if holder.pico.out else b""

        def close(self):
            pass

    monkeypatch.setattr(cli, "serial", types.SimpleNamespace(Serial=Serial))
    monkeypatch.setattr(cli.time, "sleep", lambda s: None)
    # The timeouts in seconds pass at once: monotonic() jumps 0.1 s per call
    clock = iter(range(10**6))
    monkeypatch.setattr(cli.time, "monotonic", lambda: next(clock) / 10)
    return holder


def test_get_on_a_clean_line(pico):
    smc = cli.Smc("COM9")
    assert smc.get()["firmware"] == "P01"
    assert pico.pico.commands == ["get"]


def test_a_half_line_left_in_the_pico_is_cleared(pico):
    pico.pico = Pico(junk=b"AT")
    smc = cli.Smc("COM9")
    assert smc.get()["brightness"] == 100
    assert pico.pico.commands == ["AT", "get"]  # The open's newline ended it


def test_a_lost_first_reply_is_tried_again(pico):
    pico.pico = Pico(drop_replies=1)
    smc = cli.Smc("COM9")
    assert smc.get()["brightness"] == 100
    assert pico.pico.commands == ["get", "get"]


def test_a_first_save_is_never_sent_twice(pico):
    pico.pico = Pico(drop_replies=1)
    smc = cli.Smc("COM9")
    with pytest.raises(TimeoutError):
        smc.command("save")
    assert pico.pico.commands == ["save"]


def test_later_errors_are_not_retried(pico):
    smc = cli.Smc("COM9")
    smc.get()
    with pytest.raises(cli.SmcError, match="unknown command"):
        smc.command("bogus")
    assert pico.pico.commands == ["get", "bogus"]


# ---- commands_for -----------------------------------------------------------


def test_the_same_settings_need_no_commands():
    s = settings()
    assert cli.commands_for(s, s) == []


def test_values_the_firmware_would_refuse_are_refused():
    s = settings()
    for key, value, message in (
        ("brightness", 0, "LED brightness must be 1 to 100"),
        ("brightness", "40", "LED brightness must be a number"),
        ("fade", "on", "LED fade must be true or false"),
        ("rgb_mode", "rainbow", "RGB LED mode must be one of"),
        ("ssid", "", "Wi-Fi name must be 1-32"),
        ("password", "short", "Wi-Fi password must be 8-63"),
        ("log_level", "loud", "log level must be one of"),
        ("report_version", "X99", "reported version must be auto or one of"),
        ("av_pack", 7, "AV pack must be 0 to 6"),
    ):
        wanted = dict(s, **{key: value})
        with pytest.raises(ValueError, match=message):
            cli.commands_for(s, wanted)


# ---- Export and import ------------------------------------------------------


def test_export_has_the_settings_and_not_the_read_only_lines():
    data = cli.export_settings(settings("brightness 40", "password correct horse"))
    assert data["format"] == "smcx-settings" and data["version"] == 1
    assert data["firmware"] == "P01" and data["git"] == "abc1234"
    assert data["settings"]["brightness"] == 40
    assert data["settings"]["password"] == "correct horse"
    assert data["settings"]["thresholds"][0] == {"index": 0, "value": 89, "label": "CPU overheat (at least)"}
    for read_only in ("firmware", "git", "board_password", "rgb_available", "web_available", "report_version_default"):
        assert read_only not in data["settings"]


def test_export_then_import_sends_the_differences():
    exported = settings(
        "brightness 40",
        "no-dvd on",
        "power-button off",
        "bootsel-power on",
        "ntc on beta 3435 r25 4700 series 22000",
        "sensor-bypass on",
        "bypass-offsets cpu 5 board -3",
        "threshold 0 80 default 89 min 40 label CPU overheat (at least)",
    )
    data = json.loads(json.dumps(cli.export_settings(exported)))  # Through a file's worth of JSON
    current = settings()
    wanted, notes = cli.import_settings(current, data)
    assert notes == []
    assert cli.commands_for(current, wanted) == [
        "set brightness 40",
        "set threshold 0 80",
        "set no-dvd on",
        "set sensor-bypass on",
        "set bootsel-power on",
        "set power-button off",
        "set ntc-beta 3435",
        "set ntc-r25 4700",
        "set ntc-series 22000",
        "set bypass-cpu-offset 5",
        "set bypass-board-offset -3",
        "set ntc on",
    ]
    # Imported again on top of the result: nothing left to send
    assert cli.commands_for(wanted, cli.import_settings(wanted, data)[0]) == []


def test_thresholds_only_import_into_the_same_firmware():
    data = cli.export_settings(settings("threshold 0 80 default 89 min 40 label CPU overheat (at least)"))
    data["firmware"] = "P2L"
    current = settings()
    wanted, notes = cli.import_settings(current, data)
    assert notes == ["Fan thresholds left out: saved from P2L firmware, this is P01"]
    assert cli.commands_for(current, wanted) == []


def test_settings_this_firmware_lacks_are_left_out():
    data = cli.export_settings(settings())
    data["settings"]["brightness"] = 40
    data["settings"]["warp_drive"] = True
    current = settings()
    del current["sensor_bypass"]  # Older firmware
    wanted, notes = cli.import_settings(current, data)
    assert "Unknown setting left out: warp_drive" in notes
    assert "Left out, this firmware doesn't have it: sensor_bypass" in notes
    assert cli.commands_for(current, wanted) == ["set brightness 40"]


def test_a_partial_export_changes_only_what_it_has():
    current = settings()
    data = {"format": "smcx-settings", "version": 1, "firmware": "P01", "settings": {"ntc": {"beta": 3435}}}
    wanted, _ = cli.import_settings(current, data)
    assert cli.commands_for(current, wanted) == ["set ntc-beta 3435"]


@pytest.mark.parametrize(
    ("data", "message"),
    [
        ([], "Not an SMCX settings file"),
        ({"format": "other"}, "Not an SMCX settings file"),
        ({"format": "smcx-settings", "version": 2, "settings": {}}, "version 2"),
        ({"format": "smcx-settings", "version": 1}, "has no settings"),
    ],
)
def test_other_files_are_refused(data, message):
    with pytest.raises(ValueError, match=message):
        cli.import_settings(settings(), data)


def test_cli_export_and_import(pico, monkeypatch, tmp_path, capsys):
    path = tmp_path / "smcx.json"
    monkeypatch.setattr(cli, "find_port", lambda: "COM9")
    monkeypatch.setattr(sys, "argv", ["smcx_config.py", "export", str(path)])
    cli.main()
    assert json.loads(path.read_text(encoding="utf-8"))["settings"]["ssid"] == "SMCX"

    data = json.loads(path.read_text(encoding="utf-8"))
    data["settings"]["ssid"] = "Living room"
    path.write_text(json.dumps(data), encoding="utf-8")
    pico.pico = Pico()
    monkeypatch.setattr(sys, "argv", ["smcx_config.py", "import", str(path), "save"])
    cli.main()
    assert pico.pico.commands == ["get", "set ssid Living room", "save"]
    assert "1 changed" in capsys.readouterr().out
