# DVD tray

`src/smc_dvd.cpp`. Four small state machines handle the DVD drive. Builds with
`SMC_DVD_SUPPORT=OFF` replace them with stubs that report a closed, empty tray
(0x40).

Tray states (SMBus 0x03): 0x00 closed, 0x10 open, 0x20 unloading, 0x30 opening,
0x40 no disc, 0x50 closing, 0x60 disc detected, 0x70 drive off.

## Tray state debounce (`dvd_tray_state`, PIC 0x9D3)

Reads the drive's tray state pins and reports a new state after two equal
readings. Raises the tray SMIs.

```mermaid
stateDiagram-v2
    [*] --> initial
    initial --> startup_delay: 32-tick timer
    startup_delay --> watch: after 32 ticks (640 ms)
    watch --> confirm: pins differ from the reported state
    confirm --> watch: changed again
    confirm --> apply: same as the first reading
    apply --> watch: report it
```

| State | What it does |
|-------|--------------|
| `initial` | Starts the start-up delay |
| `startup_delay` | Ignores the tray after power on. P11 also comes back here on a warm reset, then for ~5 s |
| `watch` | Waits for the pins to differ from the reported state |
| `confirm` | Reads again |
| `apply` | Reports the state and, once tray events are enabled, raises an SMI: 0x30 opening, 0x00 closed, 0x40/0x60 media. Any other state enables tray events |

## Eject pulse (`update_eject_tray_state`, PIC 0xADE)

```mermaid
stateDiagram-v2
    [*] --> initial
    initial --> tick_timer: eject_pulse set
    tick_timer --> release_eject: eject line held for 5 ticks
    release_eject --> initial: release the line, clear eject_pulse
```

## Open / close decision (`updateDvdTrayEject`, PIC 0x4D0)

Two states, tracking whether the tray was last sent open or closed. It starts as
closed.

```mermaid
stateDiagram-v2
    [*] --> closed
    closed --> open: tray reports opening (0x30)
    closed --> open: SMBus 0x0C eject, pulse
    closed --> closed: eject button, SMI only (FRAG pulses)
    open --> closed: tray reports closing (0x50)
    open --> closed: eject button, SMBus 0x0C load, or shutdown, pulse
```

While the tray is open the SMC closes it on its own. While it's closed the eject
button only raises an SMI, and the OS opens the tray with SMBus 0x0C. Pulses
are skipped while the drive reports 0x70 or 0x20.

## Tray moving (`update_dvd_tray_three_state`, PIC 0x519)

Sets `tray_moving`, reported in SMBus 0x03 bit 0 and shown as a quick green blink.

```mermaid
stateDiagram-v2
    [*] --> initial
    initial --> eject: eject button pressed
    initial --> wait: tray in a moving state
    wait --> initial: tray at 0x10, 0x40 or 0x60 (DXF also 0x00)
    eject --> wait: tray opening (0x30), 0x20, closing (0x50, unless opening is expected), or 255-tick timeout
```

| State | What it does |
|-------|--------------|
| `initial` | Not moving |
| `wait` | Moving, until the tray settles |
| `eject` | Moving after an eject button press, until the drive reacts |
