# SMBus commands

The SMC is an SMBus slave at address **0x10** (0x20 write / 0x21 read in 8-bit
form). Each command is one register byte: a write sends the command and one data
byte; a read sends the command, then a repeated START, then reads one byte.

Behavior follows the original firmware chosen with `SMC_FIRMWARE`: P11 (the
default), P01, the debug kit versions D01 and DXF, or P2L (v1.6). Unknown
commands are ignored on write and read as 0x00, and so are writes to read-only
commands and reads of write-only ones. Times are in SMC ticks of 20 ms.

## Overview

### Stock
Commands available on the stock PIC/Xyclops firmware.

| Command | Name | R/W | Summary |
|-----------|----------------------|-----|----------------------------------------|
| 0x01      | Firmware version     | R/W | Version string, one character per read |
| 0x02      | Reset / power        |  W  | Reset, power cycle or power off |
| 0x03      | Tray state           |  R  | DVD tray state |
| 0x04      | AV pack              |  R  | Connected AV cable |
| 0x05      | Fan override         |  W  | Use the speed from 0x06 |
| 0x06      | Fan speed            |  W  | Speed used by the fan override |
| 0x07      | LED override         |  W  | Show the pattern from 0x08 |
| 0x08      | LED pattern          |  W  | Red/green blink pattern |
| 0x09      | CPU temperature      |  R  | °C |
| 0x0A      | Board temperature    |  R  | °C |
| 0x0B      | Audio clamp          |  W  | Mute / unmute the audio |
| 0x0C      | Tray control         |  W  | Eject or load the DVD tray |
| 0x0D      | SMI answer           |  W  | OS answer to a power/eject interrupt |
| 0x0E      | Error code           |  W  | Error code from the BIOS/kernel |
| 0x0F      | Error code           |  R  | Error code (0x0E or boot challenge) |
| 0x10      | Fan speed            |  R  | Current fan speed |
| 0x11      | Interrupt reason     |  R  | Why the SMI was raised, cleared on read |
| 0x12      | RAM test result      |  W  | Result of the BIOS RAM test |
| 0x13      | RAM type             |  W  | RAM type / second result byte |
| 0x14      | RAM test result      |  R  | Value written to 0x12 |
| 0x15      | RAM type             |  R  | Value written to 0x13 |
| 0x16      | Last command written |  R  | Command of the last write |
| 0x17      | Last byte written    |  R  | Data of the last write |
| 0x18      | Software interrupt   |  W  | Ignored |
| 0x19      | Reset on eject       |  W  | Enable or keep off reset-on-eject |
| 0x1A      | OS ready             |  W  | Enable power off through the OS |
| 0x1B      | Scratch              | R/W | Kernel storage, kept across reboots |
| 0x1C-0x1F | Challenge            |  R  | Boot challenge bytes |
| 0x20-0x21 | Challenge response   |  W  | Answer to the boot challenge |
| 0x22      | Power cycle delay    |  W  | Off time of the next power cycle |

### Extra commands

These commands are exclusive to this SMC, so they aren't present on the original
PIC or Xcylops.

| Command | Name | R/W | Summary |
|-----------|----------------------|-----|----------------------------------------|
| 0x30      | Pico temperature     |  R  | °C |
| 0x31      | NTC temperature      |  R  | °C, 0xFF without one |
| 0x32      | LED brightness       |  R  | Front panel LED brightness, percent |
| 0x33      | LED brightness       |  W  | Set it, 1-100 percent |

## Video encoder writes (sniffer)

With `SMC_I2C_SNIFFER` (on by default) the Pico also listens to the rest of
the bus for the video encoder (Conexant 0x45, Focus 0x6A, Xcalibur 0x70).
The BIOS probes each address after power on and only the board's own encoder
acknowledges, so the first time one does it's logged at INFO level (once per
power on):

```
ENCODER: Found Conexant (0x45)
```

The video mode is worked out from the CPU's writes to the encoder
(`src/smc_encoder_mode.cpp`, decoding ported from X-HD Redux's passive SMBus
probe) and logged at INFO once a mode set has settled (200 ms without encoder
writes), whenever it changes:

```
ENCODER: Video mode 720x480p (Xcalibur, mode 0x08)
ENCODER: Video mode 640x480i, NTSC, 4:3, RGB (Conexant, 0x2E = 0x00, H_ACTIVE 648)
ENCODER: Video mode 1280x720p, 16:9, YCrCb (Conexant, 0x2E = 0xAA)
ENCODER: Video mode 720x576i, PAL, 16:9, YCrCb (Focus, 0x92 = 0x84, IHW 702, IHO 103)
```

- **Xcalibur (1.6):** the full mode, from its raster registers (0x50 input
  raster, 0x52 width, 0x54 output raster, 0x56 written last), plus the SD
  aspect (0x07). 0x0F/0x10/0x11 are register-identical to 0x02/0x04/0x08 and
  read as those.
- **Conexant (1.0-1.3):** the raster from 0x2E (RASTER_SEL/HDTV_EN: 0x00 SD
  interlaced, RASTER_SEL 1 = 480p (0xAD), as X-HD; 2 = 720p (0xAA, seen on a
  720p console); 3 = 1080i from the CX25871 field order, not yet confirmed on
  an Xbox), the SD active width
  (H_ACTIVE, 0x78/0x86), the line standard and aspect (WSS, 0x60) and RGB or
  YCrCb input (IN_MODE, 0xC6/0x32).
- **Focus (1.4-1.5):** interlaced or progressive and RGB or YCrCb (0x92), the
  SD width (IHO 0x00, or IHW 0x04/0x05 before IHO is seen) and the line
  standard and aspect (WSS, 0x83/0x85). It doesn't say which progressive mode
  (480p or 720p), so that shows as `progressive (480p or 720p)`.

- **X-HD mode packet (any encoder):** with an X-HD HDMI board fitted, an
  X-HD-aware BIOS or dashboard writes the exact mode to it at 0x69 (commands
  128-131, as in X-HD Redux). That's decoded too and logged as soon as it's
  applied, e.g. `1920x1080i, 16:9 (X-HD mode packet, mode 0x0D, encoder 0x8A,
  mode dword 0x000D0000)`. It's the only way to get 480p vs 720p on a Focus,
  or 1440 vs 1920 wide 1080i.

The line starts with the resolution (`?` or `480/576` for a part that isn't
known yet), then the standard, aspect and colour when known, and in brackets
the encoder and the raw register values it was decoded from. Conexant and
Focus don't hold the whole mode (X-HD measures the rest from the video
signal). The WSS and SD width are dropped on a switch to progressive, which
doesn't rewrite them.

At DEBUG level every write the CPU makes to it is logged too:

```
ENCODER: Conexant [0xBA] = 0x3F
```

The register comes first, then the data byte(s); `(NACK)` means the encoder
didn't acknowledge a byte. Reads (a register write followed by a repeated
START) aren't logged. The SMC's own transfers (P01's 0xBA write) are left out
of both.

- A PIO state machine reads SDA and SCL (GP12/GP13) without taking them from
  the I2C slave, and records each change. DMA copies the changes into an 8 KB
  ring buffer, decoded once per tick from the main loop.
- Printing takes ~4 ms a line, so at most 2 writes are printed per tick (DEBUG
  level), from a queue of 128. The BIOS writes the encoder in bursts, so the log lags behind
  and a long burst can overflow the queue (`writes not logged` warning).
- Changes are only recorded in batches of 16; the last few of a transfer are
  pushed out once the bus is idle, so a write shows up within a tick or two.

## Commands

### 0x01 Firmware version (read/write)

Each read returns the next character of the version string, wrapping after
three (for example `'P'`, `'1'`, `'1'`). The position is kept between
transactions. Writing any value restarts at the first character.

The string is set by `SMC_REPORT_VERSION`: `AUTO` (the default) reports the
`SMC_FIRMWARE` version. `P01`, `P11`, `D01`, `DXF` and `P2L` report that version
whatever the behavior is: only the reported string changes, the behavior
still follows `SMC_FIRMWARE`. The boot log shows the reported version.

### 0x02 Reset / power (write)

| Bit | Action |
|-----|--------|
|  0  | Reset the Xbox (warm reset through SYSRESET) |
|  6  | Power cycle: power off, then back on after the 0x22 delay, 0.5 s by default (as P11/P2L) |
|  7  | Power off |

Bits 6 and 7 go through the power button logic (`power_change_requested`): if
the OS has sent 0x1A it gets a chance to shut down first, see 0x0D.

### 0x03 Tray state (read)

The drive's tray state pins in bits 4-6, plus bit 0 while the tray is moving.

| Value | State |
|-------|-------|
| 0x00  | Closed |
| 0x10  | Open |
| 0x20  | Unloading |
| 0x30  | Opening |
| 0x40  | No disc |
| 0x50  | Closing |
| 0x60  | Disc detected |
| 0x70  | Drive off / reset |

A new state is only reported after reading the same twice in a row, and the
tray is ignored for 39 ticks after power on. Builds without `SMC_DVD_SUPPORT`
always report 0x40.

### 0x04 AV pack (read)

| Value | AV pack |
|-------|---------|
|   0   | SCART |
|   1   | High Definition AV Pack |
|   2   | VGA |
|   3   | RF adapter |
|   4   | Advanced AV (S-Video) |
|   5   | Unknown |
|   6   | Standard AV (composite) |
|   7   | No cable |

`SMC_AV_PACK_OVERRIDE=ON` reports `SMC_AV_PACK_MODE` instead of the detected
pack. Writes are ignored, as on the PIC.

### 0x05 Fan override (write)

Bit 0 = 1: run the fan at the speed from 0x06 instead of the temperature
control. Only accepted once 0x06 has been written, so send 0x06 first. Bit 0
= 0: back to automatic.

P2L: always accepted (the 0x06 speed starts at 50), and turning it off raises
the fan to at least 30.

### 0x06 Fan speed (write)

Speed for the fan override, 0-50 (the scale of 0x10). The fan never runs below
10 while the Xbox is on, so 0-9 run at 10. Values above 50 are limited to 50,
as P2L (the original P01/P11 store them, and 0x05 then refuses to turn on).

### 0x07 LED override (write)

Bit 0 = 1: show the pattern from 0x08. Bit 0 = 0: back to the automatic
patterns. A boot failure error code, an overheat and the "no AV cable" pattern
always win over the override (the original P11 lets the override hide the AV
one). The override wins over the off state, as P11/P2L (P01 shows off first).
Cleared by every reset.

### 0x08 LED pattern (write)

Bits 0-3 green, bits 4-7 red. Each nibble is four phases, shown bit 3 first,
about 180 ms each: 0xF = on, 0x0 = off, 0x5 or 0xA = blinking.

| Value | Pattern |
|-------|---------|
|  0x0F | Green |
|  0xF0 | Red |
|  0xFF | Orange |
|  0x0A | Blinking green |
|  0xA0 | Blinking red |
|  0xAA | Blinking orange |
|  0xA5 | Alternating red / green |
|  0x00 | Off |

### 0x09 CPU temperature (read)

In °C, read from the ADM1032 temperature sensor (address 0x4C, remote channel).

### 0x0A Board temperature (read)

In °C, from the temperature sensor's internal probe.
P2L has no board sensor and returns the CPU temperature.

### 0x0B Audio clamp (write)

Bit 0 = 1: clamp (mute) the audio. Bit 0 = 0: unclamp. The audio is always
clamped without an AV cable and during shutdown.

### 0x0C Tray control (write)

Bit 0 = 0: eject (open the tray). Bit 0 = 1: load (close the tray). The SMC
pulses the drive's eject line when the tray is in the matching state.

### 0x0D SMI answer (write)

The OS answer to a power button, eject or tray interrupt. Only accepted while
the SMC is waiting for one; without an answer within 25 ticks the SMC resets
the Xbox (eject/tray) or powers it off (power button).

| Bit | Meaning |
|-----|---------|
|  0  | Power off |
|  1  | Busy: wait longer (up to 255 ticks, at most 3 times) |
|  2  | Handled, no reset |
|  3  | Reset |

### 0x0E Error code (write) / 0x0F Error code (read)

Error code from the BIOS or kernel, read back with 0x0F. The boot challenge
also sets it: 0x01 challenge failed, 0x04 RAM test failed. Cleared at power off.

### 0x10 Fan speed (read)

Current fan speed, 10-50 while on, 0 while off.

### 0x11 Interrupt reason (read)

Why the SMI line was pulsed. Cleared by reading it.

| Bit | Reason |
|-----|--------|
|  0  | Power button (also power off/cycle over SMBus, or the special AV mode) |
|  1  | Tray state 0x40 / 0x60 (no disc / disc detected) |
|  2  | Tray opening (0x30) |
|  3  | AV pack changed |
|  4  | AV cable unplugged |
|  5  | Eject button |
|  6  | Tray closed (0x00) |

### 0x12 RAM test result (write) / 0x13 RAM type (write)

Written by the BIOS after its RAM test, 0x13 first. The test counts as passed
when 0x13 is 0x0F and 0x12 is 0xF0, or when 0x13 is 0x00 or 0xFF and 0x12 is
0x00 (as P2L; the original P01/P11 pass 0x13 = 0x00 or 0xFF whatever 0x12
holds).
Without a passed test the SMC doesn't wait for the OS at power off.
Read back with 0x14 and 0x15.

### 0x16 Last command written / 0x17 Last byte written (read)

The command and data byte of the most recent write.

### 0x18 Software interrupt (write)

Ignored, as on the PIC.

### 0x19 Reset on eject (write)

Whether opening the tray resets the Xbox. Every reset turns it off, and the
first 0x19 write after a reset decides:

- Bit 0 = 0: enable reset-on-eject. The eject button then raises an SMI and the
  Xbox resets unless the OS answers with 0x0D.
- Bit 0 = 1: keep it off. Only accepted as the first write; once it's enabled
  it stays on until the next reset.

### 0x1A OS ready (write)

Bit 0 = 1: the OS is running and handles power off. From then on the power
button sends an SMI and waits for the 0x0D answer instead of powering off
right away. Bit 0 = 0 clears it again, as P2L (the original P01/P11 ignore
it). Also cleared at every reset.

### 0x1B Scratch (read/write)

One byte of storage for the kernel that survives reboots (cleared at power
off). The kernel uses it for boot flags, such as showing a fatal error or
booting to the dashboard after the reboot.

### 0x1C-0x1F Challenge (read) / 0x20-0x21 Challenge response (write)

At boot the BIOS reads the four challenge bytes, computes the answer and writes
it to 0x20 and 0x21 (0x21 last). By default the result is only logged; with
`SMC_BOOT_CHALLENGE=ON` a wrong or missing answer resets the Xbox and after
repeated failures shows the FRAG pattern, like the original.

D01 and DXF (debug kits) have no challenge: the bytes stay 0x00 and the answer
is ignored. Only the RAM test result is checked.

### 0x22 Power cycle delay (write)

Off time of the next power cycle (0x02 bit 6), in ticks. Reset to 25 (0.5 s)
at every power off. Values 0-2 wrap to about 5 s. Accepted by every build (the original P01 doesn't have it).

### 0x30 Pico temperature (read)

Not on the PIC. The Pico's own chip temperature (RP2040/RP2350 sensor), in
whole °C, rounded, read once a second. 0 below 0 °C, at most 0xFE. It reads a
few degrees above the air around it.

### 0x31 NTC temperature (read)

Not on the PIC. The optional NTC thermistor on GP28 (`SMC_NTC_SENSOR`, see
`docs/Wiring.md`), in whole °C, rounded, read once a second. 0xFF when there
is no reading: the option is off, or the NTC reads open or shorted.

### 0x32 LED brightness (read)

Not on the PIC. The front panel LED brightness in percent (1-100), as set by
`SMC_LED_BRIGHTNESS`, the saved settings, 0x33 or the web interface.

### 0x33 LED brightness (write)

Not on the PIC. Sets the front panel LED brightness, 1-100 percent; other
values are ignored. Applied on the next tick (logged as `LED: Brightness n%
(SMBus 0x33)`), and kept until the Pico restarts: it isn't saved to flash.

