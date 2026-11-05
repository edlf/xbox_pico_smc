"""Generates docs/images/wiring.svg: Pico and PIC footprints with net labels.

Run from the project root: python tools/wiring_svg.py docs/images/wiring.svg
Keep the nets in step with src/pin_assignments.hpp.
"""

import sys

if len(sys.argv) != 2:
    sys.exit("usage: wiring_svg.py out.svg")
out = sys.argv[1]

# Pico physical pins
pico = [None] + [
    "GP0",
    "GP1",
    "GND",
    "GP2",
    "GP3",
    "GP4",
    "GP5",
    "GND",
    "GP6",
    "GP7",
    "GP8",
    "GP9",
    "GND",
    "GP10",
    "GP11",
    "GP12",
    "GP13",
    "GND",
    "GP14",
    "GP15",
    "GP16",
    "GP17",
    "GND",
    "GP18",
    "GP19",
    "GP20",
    "GP21",
    "GND",
    "GP22",
    "RUN",
    "GP26",
    "GP27",
    "GND",
    "GP28",
    "ADC_VREF",
    "3V3",
    "3V3_EN",
    "GND",
    "VSYS",
    "VBUS",
]
# PIC16LC63A pins
pic = [
    None,
    "MCLR#",
    "RA0",
    "RA1",
    "RA2",
    "RA3",
    "RA4",
    "RA5",
    "VSS",
    "OSC1",
    "OSC2",
    "RC0",
    "RC1",
    "RC2",
    "RC3",
    "RC4",
    "RC5",
    "RC6",
    "RC7",
    "VSS",
    "VDD",
    "RB0",
    "RB1",
    "RB2",
    "RB3",
    "RB4",
    "RB5",
    "RB6",
    "RB7",
]

# (net, pico pin, pic pin, type); type: out (Pico drives), in, io, pwr, gnd
nets = [
    ("RED_LED", 4, 2, "out"),
    ("GREEN_LED", 5, 3, "out"),
    ("SYSRESET#", 6, 4, "out"),
    ("EJECT_SW", 7, 5, "in"),
    ("SMI#", 9, 6, "out"),
    ("DVD_EJECT", 10, 7, "out"),
    ("AUDIO_CLAMP", 11, 11, "out"),
    ("POWOK", 12, 12, "in"),
    ("FAN_PWM", 14, 13, "out"),
    ("POWON", 15, 16, "out"),
    ("SMB_SDA", 16, 15, "io"),
    ("SMB_SCL", 17, 14, "io"),
    ("RTC_DUMP", 19, 17, "out"),
    ("POWER_SW", 20, 18, "in"),
    ("PLL_EN", 21, 21, "out"),
    ("AV_MODE0", 22, 22, "in"),
    ("AV_MODE1", 24, 23, "in"),
    ("AV_MODE2", 25, 24, "in"),
    ("TRAY0", 26, 25, "in"),
    ("TRAY1", 27, 26, "in"),
    ("TRAY2", 29, 27, "in"),
    ("SMC_RESET#", 30, 1, "in"),
    ("DVD_ACTIVE", 31, 28, "in"),
    ("3V3_STBY", 36, 20, "pwr"),
    ("GND", 38, 8, "gnd"),
]
# Pico-only extras: (net, type, where the other end goes)
extra_pico = {
    1: ("UART_TX", "dbg", "debug only"),
    2: ("UART_RX", "dbg", "debug only"),
    32: ("RGB_LED", "opt", "WS2812 data in"),
    33: ("NTC_GND", "opt", "NTC, other leg"),
    34: ("NTC", "opt", "NTC + 10k to 3V3"),
}

color = {
    "out": "#c0392b",
    "in": "#2471a3",
    "io": "#7d3c98",
    "pwr": "#d68910",
    "gnd": "#424949",
    "dbg": "#1e8449",
    "opt": "#138d90",
}

pico_net, pic_net = {}, {}
for name, pp, cp, kind in nets:
    pico_net[pp] = (name, kind, f"PIC {cp}")
    pic_net[cp] = (name, kind, f"Pico {pp} · {pico[pp]}")
pic_net[19] = ("GND", "gnd", "Pico GND")  # Second PIC ground
pico_net.update(extra_pico)

pitch = 25
svg = []


def e(*lines):
    svg.extend(lines)


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def tag(x, y, align, net):
    """A net tag: rounded box with the net name and where it goes. align:
    'left' = tag ends at x, 'right' = tag starts at x"""
    name, kind, to = net
    w = 190
    x0 = x - w if align == "left" else x
    c = color[kind]
    e(f'<rect x="{x0}" y="{y - 10}" width="{w}" height="20" rx="4" fill="{c}" fill-opacity="0.12" stroke="{c}"/>')
    e(f'<text x="{x0 + 8}" y="{y + 4}" class="net" fill="{c}">{esc(name)}</text>')
    e(f'<text x="{x0 + w - 7}" y="{y + 4}" class="to" text-anchor="end">{esc(to)}</text>')


def part(x, y, w, title, sub, names, part_nets, count):
    """A part: body with pins down the left (top to bottom) and up the right.
    All coordinates are whole numbers (the widths and pin counts are even)."""
    half = count // 2
    h = half * pitch + 20
    mid = x + w // 2
    e(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" class="body"/>')
    e(f'<path d="M {mid - 12} {y} a 12 12 0 0 0 24 0" class="notch"/>')
    e(f'<text x="{mid}" y="{y + h // 2 - 6}" class="title" text-anchor="middle">{esc(title)}</text>')
    e(f'<text x="{mid}" y="{y + h // 2 + 14}" class="sub" text-anchor="middle">{esc(sub)}</text>')
    for i in range(1, count + 1):
        left = i <= half
        row = i - 1 if left else count - i
        py = y + 20 + row * pitch
        px = x if left else x + w
        used = i in part_nets
        c = color[part_nets[i][1]] if used else "#b3b6b7"
        stub = 40 if used else 14
        x2 = px - stub if left else px + stub
        e(f'<line x1="{px}" y1="{py}" x2="{x2}" y2="{py}" stroke="{c}" stroke-width="{2 if used else 1}"/>')
        if used:
            e(f'<circle cx="{x2}" cy="{py}" r="2.5" fill="{c}"/>')
        # Pin number outside the body, name inside
        num_x, num_anchor = (px - 4, "end") if left else (px + 4, "start")
        e(f'<text x="{num_x}" y="{py - 4}" class="num" text-anchor="{num_anchor}">{i}</text>')
        name_x, name_anchor = (px + 8, "start") if left else (px - 8, "end")
        unused = "" if used else " unused"
        e(f'<text x="{name_x}" y="{py + 4}" class="pin{unused}" text-anchor="{name_anchor}">{esc(names[i])}</text>')
        if used:
            tag(x2 - 4 if left else x2 + 4, py, "left" if left else "right", part_nets[i])


W, H = 1620, 650
e(
    f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
    'font-family="Segoe UI, Helvetica, Arial, sans-serif">'
)
e(
    "<style>.body{fill:#f4f6f6;stroke:#2c3e50;stroke-width:2}.notch{fill:none;stroke:#2c3e50}",
    ".title{font-size:18px;font-weight:600;fill:#2c3e50}.sub{font-size:12px;fill:#566573}",
    ".num{font-size:9px;fill:#7f8c8d}.pin{font-size:11px;fill:#2c3e50;font-family:Consolas,Menlo,monospace}",
    ".unused{fill:#b3b6b7}.net{font-size:11px;font-weight:600;font-family:Consolas,Menlo,monospace}",
    ".to{font-size:10px;fill:#566573}.h{font-size:20px;font-weight:600;fill:#2c3e50}.leg{font-size:12px;fill:#2c3e50}</style>",
)
e(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
e('<text x="24" y="34" class="h">SMCX wiring: Raspberry Pi Pico to the Xbox PIC footprint (U7)</text>')
e(
    '<text x="24" y="54" class="sub">Same net name = connect together. Top view, notch at the top, pin 1 top left. '
    "Pico, Pico W, Pico 2 and Pico 2 W share this pinout.</text>"
)

part(290, 90, 280, "Raspberry Pi Pico", "top view", pico, pico_net, 40)
part(1100, 90, 260, "PIC16LC63A", "U7 footprint, top view", pic, pic_net, 28)

# Legend
legend = [
    ("out", "Pico output"),
    ("in", "Pico input"),
    ("io", "SMBus (I2C, both ways)"),
    ("pwr", "3.3 V standby"),
    ("gnd", "Ground"),
    ("dbg", "Debug UART only"),
    ("opt", "Optional extras (RGB LED, NTC)"),
]
lx, ly = 880, 515
e(f'<text x="{lx}" y="{ly - 14}" class="leg" font-weight="600">Legend</text>')
for i, (kind, label) in enumerate(legend):
    yy = ly + i * 20
    c = color[kind]
    e(f'<rect x="{lx}" y="{yy - 10}" width="26" height="14" rx="3" fill="{c}" fill-opacity="0.12" stroke="{c}"/>')
    e(f'<text x="{lx + 36}" y="{yy + 1}" class="leg">{esc(label)}</text>')
e("</svg>")

with open(out, "w", encoding="utf-8", newline="\n") as fh:
    fh.write("\n".join(svg) + "\n")
