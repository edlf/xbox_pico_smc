# Audio clamp

`src/smc_audio_clamp.cpp`, PIC 0x573. Mutes (clamps) the audio output.

The audio is always clamped without an AV cable or during shutdown. At power on
it unclamps after 44 ticks (~0.9 s). SMBus 0x0B clamps (1) or unclamps (0).

```mermaid
stateDiagram-v2
    [*] --> clamped
    clamped --> clamped: no AV cable
    clamped --> unclamped: unclamp request
    clamped --> tick_timer: power-on timer
    tick_timer --> clamped: must clamp
    tick_timer --> unclamped: unclamp request, or 44 ticks
    unclamped --> clamped: clamp request, no AV cable, or shutdown
```

| # | State | What it does |
|---|-------|--------------|
| 0 | `clamped` | Clamp on, clears the clamp request, restarts the 44-tick delay |
| 1 | `tick_timer` | Counts down the power-on delay |
| 2 | `unclamped` | Clamp off, clears the unclamp request |
