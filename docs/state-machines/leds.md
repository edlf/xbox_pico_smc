# Front panel LEDs

`src/smc_led.cpp`, PIC 0xA28. Picks the LED pattern and steps through it.

A pattern is a 4-bit value per colour; each bit is one phase of about 180 ms,
shown bit 3 first. Both LEDs on make orange.

```mermaid
stateDiagram-v2
    [*] --> initial
    initial --> overheat: overheated
    initial --> manual_control: LED override (P11 checks this second)
    initial --> quick_green_orange: no AV cable
    initial --> off: system not running
    initial --> manual_control: LED override (P01 checks this fourth)
    initial --> quick_green_blink: tray moving
    initial --> solid_green: otherwise

    overheat --> tick_update
    manual_control --> tick_update
    quick_green_orange --> tick_update
    off --> tick_update
    quick_green_blink --> tick_update
    solid_green --> tick_update

    tick_update --> set_gpios: after 6 ticks
    set_gpios --> initial: next phase
    set_gpios --> resetPhaseCounter: after the last phase
    resetPhaseCounter --> initial
```

| # | State | Red | Green | Result |
|---|-------|-----|-------|--------|
| 1 | `overheat` | 0x3 | 0x3 | Slow orange blink |
| 2 | `manual_control` | SMBus 0x08 | SMBus 0x08 | Pattern from the OS or the boot challenge |
| 3 | `quick_green_blink` | 0x0 | 0x5 | Quick green blink (tray moving) |
| 4 | `solid_green` | 0x0 | 0xF | Green |
| 5 | `off` | 0x0 | 0x0 | Off |
| 9 | `quick_green_orange` | 0x5 | 0xF | Alternating green / orange (no AV cable) |

| # | State | What it does |
|---|-------|--------------|
| 0 | `initial` | Picks the pattern, highest priority first |
| 6 | `tick_update` | Holds the current phase for 6 ticks |
| 7 | `set_gpios` | Sets both LEDs for the next phase |
| 8 | `resetPhaseCounter` | Starts again at phase 3 |

The LEDs are PWM driven: `SMC_LED_BRIGHTNESS` sets the brightness and
`SMC_LED_RED_BREATHE` makes the red LED breathe.
