"""tools/smcx_config_gui.py: the commands Apply sends for what was changed. The
window is never shown (withdrawn); without a display (Tk can't start) these
are skipped."""

import pytest
import smcx_config as cli
from conftest import GET_REPLY

tk = pytest.importorskip("tkinter")
gui = pytest.importorskip("smcx_config_gui")


@pytest.fixture
def app(monkeypatch):
    # No ports to look at, and no serial work
    monkeypatch.setattr(cli, "pico_ports", list, raising=False)
    monkeypatch.setattr(cli, "list_ports", type("NoPorts", (), {"comports": staticmethod(list)}), raising=False)
    try:
        root = tk.Tk()
    except tk.TclError as e:
        pytest.skip(f"No display for Tk: {e}")
    root.withdraw()
    window = gui.App(root, theme="light")
    yield window
    root.destroy()


def read(app, *changes):
    """Shows GET_REPLY (with some lines replaced) as if read from the Pico."""
    lines = list(GET_REPLY)
    for change in changes:
        key = change.split(" ", 1)[0]
        lines = [change if line.split(" ", 1)[0] == key else line for line in lines]
    app.show_settings(cli.parse_settings(lines))


def test_nothing_changed_sends_nothing(app):
    read(app)
    assert app.changes() == []


def test_each_change_is_one_command(app):
    read(app)
    app.brightness.set(40)
    app.no_dvd.set(True)
    app.eject_button.set(False)
    app.web.set(True)
    assert app.changes() == ["set brightness 40", "set no-dvd on", "set eject-button off", "set web on"]


def test_power_button_on_goes_before_bootsel_off(app):
    # BOOTSEL can only be turned off while the power button is on
    read(app, "power-button off", "bootsel-power on")
    app.power_button.set(True)
    app.bootsel_choice = False
    assert app.changes() == ["set power-button on", "set bootsel-power off"]


def test_bootsel_on_goes_before_power_button_off(app):
    read(app)
    app.power_button.set(False)
    app.bootsel_choice = True
    assert app.changes() == ["set bootsel-power on", "set power-button off"]


def test_bypass_offsets_are_sent_and_checked(app):
    read(app)
    app.cpu_offset.set(-5)
    assert "set bypass-cpu-offset -5" in app.changes()
    app.cpu_offset.set(31)
    with pytest.raises(ValueError, match="-30 to 30"):
        app.changes()


def test_sensor_bypass_needs_the_ntc(app):
    read(app)
    app.sensor_bypass.set(True)
    with pytest.raises(ValueError, match="needs the NTC"):
        app.changes()
    app.ntc.set(True)
    assert app.changes() == ["set sensor-bypass on", "set ntc on"]


def test_ntc_shutdown(app):
    read(app, "ntc on beta 3950 r25 10000 series 10000")
    app.ntc_shutdown.set(True)
    app.ntc_shutdown_temp.set(90)
    assert app.changes() == ["set ntc-shutdown-temp 90", "set ntc-shutdown on"]
    app.ntc.set(False)
    with pytest.raises(ValueError, match="needs the NTC on"):
        app.changes()


def test_reported_version_auto_and_fixed(app):
    read(app, "report-version D01 default P01")
    assert app.changes() == []
    app.report_version.set("Same as the build (P01)")
    assert app.changes() == ["set report-version auto"]


def test_export_is_what_the_window_shows(app):
    read(app)
    app.brightness.set(40)
    app.board_password.set(False)
    app.password.set("correct horse")
    data = cli.export_settings(app.form())
    assert data["settings"]["brightness"] == 40
    assert data["settings"]["password"] == "correct horse"


def test_import_fills_the_window_and_apply_sends_it(app):
    read(app)
    data = cli.export_settings(cli.parse_settings([*GET_REPLY[:3], "brightness 40", *GET_REPLY[4:]]))
    data["settings"]["web"] = True
    wanted, notes = cli.import_settings(app.settings, data)
    assert notes == []
    app.load_form(wanted)
    assert app.brightness.get() == 40
    assert app.settings["brightness"] == 100  # Still the Pico's: Apply sends the difference
    assert app.changes() == ["set brightness 40", "set web on"]
