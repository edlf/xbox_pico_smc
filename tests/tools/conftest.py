"""Tests for the Python config tools (tools/smcx_config.py and
tools/smcx_config_gui.py), run with pytest: python -m pytest tests/tools"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

# A `get` reply as the firmware sends it (Settings::describe() in
# src/smc_settings.cpp, firmware and git from src/usb_config.cpp). Checked
# against those sources by test_sample_matches_the_firmware.
GET_REPLY = [
    "firmware P01",
    "git abc1234",
    "report-version auto default P01",
    "brightness 100",
    "fade on",
    "rgb-mode boot",
    "rgb-brightness 100",
    "ssid SMCX",
    "password ",
    "board-password smcx-E6614103E7123456",
    "web off",
    "boot-challenge off",
    "av-pack detect",
    "log-level INFO",
    "no-dvd off",
    "sniffer off",
    "ntc off beta 3950 r25 10000 series 10000",
    "fan-ramp up 1 down 1 glide-up 0 glide-down 0",
    "sensor-bypass off",
    "bypass-offsets cpu 10 board 10",
    "power-button on",
    "eject-button on",
    "bootsel-power off",
    "threshold 0 89 default 89 min 40 label CPU overheat (at least)",
    "threshold 1 78 default 78 min 40 label CPU hot, +3 speed (at least)",
]
