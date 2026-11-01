# SMBus commands

The SMC is an SMBus slave at address **0x10** (0x20 write / 0x21 read in 8-bit
form). Each command is one register byte: a write sends the command and one data
byte; a read sends the command, then a repeated START, then reads one byte.

Behavior follows the original PIC firmware (P01, or P11 with
`SMC_FIRMWARE=P11`, the default). Unknown commands are ignored on write and
read as 0x00, and so are writes to read-only commands and reads of write-only
ones. Times are in SMC ticks of 20 ms.

## Overview

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
| 0x22      | Power cycle delay    |  W  | Off time of the next power cycle (P11 only) |

## Commands

### 0x01 Firmware version (read/write)

Each read returns the next character of the version string, wrapping after three:
`'P'`, `'1'`, `'1'` for P11 or `'P'`, `'0'`, `'1'` for P01. The position is kept
between transactions. Writing any value restarts at `'P'`.

`SMC_REPORT_P01=ON` makes a P11 build report "P01". The boot log shows the
reported version.

Debug kits report other versions like D01 or DXF.

### 0x02 Reset / power (write)

| Bit | Action |
|-----|--------|
|  0  | Reset the Xbox (warm reset through SYSRESET) |
|  6  | Power cycle: power off, then back on. P01: about 1.5 s off. P11: the 0x22 delay, 0.5 s by default |
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
tray is ignored for 32 ticks after power on. Builds without `SMC_DVD_SUPPORT`
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
control. Only accepted once 0x06 holds a valid speed (≤ 50), so send 0x06
first. Bit 0 = 0: back to automatic.

### 0x06 Fan speed (write)

Speed for the fan override, 0-50 (the scale of 0x10). The fan never runs below
10 while the Xbox is on, so 0-9 run at 10.

### 0x07 LED override (write)

Bit 0 = 1: show the pattern from 0x08. Bit 0 = 0: back to the automatic
patterns. An overheat always shows its own pattern. On P01 the "no AV cable"
pattern and the off state also win over the override; on P11 they don't.
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
when the 0x13 value is 0x00 or 0xFF, or when 0x13 is 0x0F and 0x12 is 0xF0.
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
right away. Cleared at every reset.

### 0x1B Scratch (read/write)

One byte of storage for the kernel that survives reboots (cleared at power
off). The kernel uses it for boot flags, such as showing a fatal error or
booting to the dashboard after the reboot.

### 0x1C-0x1F Challenge (read) / 0x20-0x21 Challenge response (write)

At boot the BIOS reads the four challenge bytes, computes the answer and writes
it to 0x20 and 0x21 (0x21 last). By default the result is only logged; with
`SMC_BOOT_CHALLENGE=ON` a wrong or missing answer resets the Xbox and after
repeated failures shows the FRAG pattern, like the original.

### 0x22 Power cycle delay (write, P11 only)

Off time of the next power cycle (0x02 bit 6), in ticks. Reset to 25 (0.5 s)
at every power off. Values 0-2 wrap to about 5 s. Ignored by P01 builds.
