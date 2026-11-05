#include "smc.hpp"
#include "hal.hpp"
#include "ring_buffer.hpp"
#include "pin_assignments.hpp"

namespace SMC {

namespace Command {
constexpr uint8_t FIRMWARE_REVISION = 0x01;
constexpr uint8_t RESET = 0x02;
constexpr uint8_t TRAY_STATE = 0x03;
constexpr uint8_t VIDEO_MODE = 0x04;
constexpr uint8_t FAN_OVERRIDE = 0x05;
constexpr uint8_t REQUEST_FAN_SPEED = 0x06;
constexpr uint8_t LED_OVERRIDE = 0x07;
constexpr uint8_t LED_STATES = 0x08;
constexpr uint8_t CPU_TEMPERATURE = 0x09;
constexpr uint8_t AIR_TEMPERATURE = 0x0A;
constexpr uint8_t AUDIO_CLAMP = 0x0B;
constexpr uint8_t DVD_TRAY_OPERATION = 0x0C;
constexpr uint8_t OS_RESUME = 0x0D;
constexpr uint8_t WRITE_ERROR_CODE = 0x0E;
constexpr uint8_t READ_ERROR_CODE = 0x0F;
constexpr uint8_t READ_FAN_SPEED = 0x10;
constexpr uint8_t INTERRUPT_REASON = 0x11;
constexpr uint8_t WRITE_RAM_TEST_RESULTS = 0x12;
constexpr uint8_t WRITE_RAM_TYPE = 0x13;
constexpr uint8_t READ_RAM_TEST_RESULTS = 0x14;
constexpr uint8_t READ_RAM_TYPE = 0x15;
constexpr uint8_t LAST_REGISTER_WRITTEN = 0x16;
constexpr uint8_t LAST_BYTE_WRITTEN = 0x17;
constexpr uint8_t SOFTWARE_INTERRUPT = 0x18;
constexpr uint8_t OVERRIDE_RESET_ON_TRAY_OPEN = 0x19;
constexpr uint8_t OS_READY = 0x1A;
constexpr uint8_t SCRATCH = 0x1B;
constexpr uint8_t CHALLENGE_BYTE0 = 0x1C;
constexpr uint8_t CHALLENGE_BYTE1 = 0x1D;
constexpr uint8_t CHALLENGE_BYTE2 = 0x1E;
constexpr uint8_t CHALLENGE_BYTE3 = 0x1F;
constexpr uint8_t CHALLENGE_RESPONSE0 = 0x20;
constexpr uint8_t CHALLENGE_RESPONSE1 = 0x21;
constexpr uint8_t POWER_CYCLE_DELAY = 0x22;  // P11/P2L, all firmwares here

// New commands, not stock
constexpr uint8_t CUSTOM_VERSION = 0x30;
constexpr uint8_t PICO_TEMPERATURE = 0x31;
constexpr uint8_t NTC_TEMPERATURE = 0x32;
constexpr uint8_t READ_LED_BRIGHTNESS = 0x33;
constexpr uint8_t WRITE_LED_BRIGHTNESS = 0x34;
constexpr uint8_t READ_RGB_BRIGHTNESS = 0x35;
constexpr uint8_t WRITE_RGB_BRIGHTNESS = 0x36;
constexpr uint8_t RGB_COLOUR_SHOWN = 0x37;  // Bit 0: show the colour below
constexpr uint8_t RGB_RED = 0x38;
constexpr uint8_t RGB_GREEN = 0x39;
constexpr uint8_t RGB_BLUE = 0x3A;
};  // namespace Command

// ExtraTemp's tenths of °C as a byte of whole °C, like 0x09/0x0A. 0xFF means
// no reading, so real values stop at 0xFE, and below 0 reads 0.
static uint8_t temperatureByte(const int16_t tenths) {
  const int whole = (tenths + 5) / 10;  // Rounded
  if (whole <= 0) {
    return 0;
  }
  return static_cast<uint8_t>(whole > 0xFE ? 0xFE : whole);
}

static bool expecting_command = true;  // Next received byte is a command byte

// The slave's registers, back to these at boot and power off (as the PIC's
// RAM_init). Like the PIC, the command byte is kept after the transaction
// ends: a read byte is "write command, repeated START, read", and the START
// must not make us forget the command.
struct Registers {
  uint8_t command = 0;
  uint8_t last_write_command = 0;  // SMBus 0x16, only updated by writes
  uint8_t last_write_data = 0;     // SMBus 0x17, only updated by writes
  uint8_t version_counter = 1;     // SMBus 0x01: 1-3, the character read next
  uint8_t scratch = 0;             // SMBus 0x1B, kernel boot flags kept across reboots
};
static Registers regs;

// SMBus 0x01: Settings::reportedVersion(), SMC_VERSION_STRING (CMake
// SMC_REPORT_VERSION) unless one is set over the USB config
static_assert(sizeof(SMC_VERSION_STRING) == 4, "The version is 3 characters");
// SMBus 0x30: version of this SMC firmware (the commands it adds), separate
// from the PIC version above. Bump it when the custom commands change.
constexpr uint8_t custom_version = 0x01;

// Transactions are printed from the main loop (printSMBusLog): printing in
// the IRQ would stretch the SMBus clock for milliseconds per byte
struct SMBusLogEntry {
  bool    write;
  uint8_t command;
  uint8_t data;
};
static RingBuffer<SMBusLogEntry, 32> smbus_log;  // Pushed by the IRQ, popped by the main loop

static void logTransaction(bool write, uint8_t command, uint8_t data) {
  smbus_log.push({write, command, data});  // Dropped when full
}

// Writes that act on the other modules wait here for the main loop
// (applySMBusWrites), so only the main loop touches their flags. The BIOS
// writes a handful per tick at most.
struct PendingWrite {
  uint8_t command;
  uint8_t data;
};
static RingBuffer<PendingWrite, 32> pending_writes;  // Pushed by the IRQ, popped by the main loop

// Main loop only (applyWrite())
static bool    bios_error_seen = false;
static uint8_t bios_error_code = 0;

bool lastBiosErrorCode(uint8_t& code) {
  code = bios_error_code;
  return bios_error_seen;
}

void resetSMBusRegisters() {
  regs = {};
  bios_response_byte0 = 0;  // Error code, SMBus 0x0E/0x0F
  // Nothing from before the reset: a power off request still waiting would
  // turn the Xbox back on
  pending_writes.clear();
  pending_writes.takeDropped();
}

void printSMBusLog() {
  SMBusLogEntry entry;
  while (smbus_log.pop(entry)) {
    if (debug::enabled(debug::Level::Debug)) {  // Debug level only, they're too frequent for Info
      debug::print(debug::Level::Debug, "SMBUS: %s cmd [0x%02X] data [0x%02X]", entry.write ? "Write" : "Read ", entry.command, entry.data);
    }
  }
}

// In the IRQ: the answer has to be on the bus at once
static uint8_t readRegister(uint8_t command) {
  uint8_t response = 0;

  switch (command) {
    case Command::FIRMWARE_REVISION:  // 0x01
      // One character per read, the index survives between transactions and
      // is only reset by writing 0x01 (same as the PIC)
      if (regs.version_counter < 1 || regs.version_counter > 3) {
        regs.version_counter = 1;
      }
      response = Settings::reportedVersion()[regs.version_counter - 1];  // SMC_VERSION_STRING unless set
      regs.version_counter = (regs.version_counter == 3) ? 1 : regs.version_counter + 1;
      break;

    case Command::TRAY_STATE:  // 0x03
      response = static_cast<uint8_t>(Dvd::sensors.tray_status);
      // Bit 0: the tray is moving
      if (Dvd::flags.tray_moving) {
        response |= 0x01;
      }
      break;

    case Command::VIDEO_MODE:  // 0x04
      response = static_cast<uint8_t>(Video::sensors.vmode) >> 1;
      break;

    case Command::CPU_TEMPERATURE:  // 0x09
      response = Fan::sensors.CPU_temperature;
      break;

    case Command::AIR_TEMPERATURE:  // 0x0A
      // P2L has no board sensor and reports the CPU temperature
      response = SMC_FIRMWARE_P2L ? Fan::sensors.CPU_temperature : Fan::sensors.board_temperature;
      break;

    case Command::CUSTOM_VERSION:  // 0x30
      response = custom_version;
      break;

    case Command::PICO_TEMPERATURE:  // 0x31
      response = temperatureByte(ExtraTemp::sensors.pico_temperature);
      break;

    case Command::NTC_TEMPERATURE:  // 0x32
      // 0xFF without an NTC (off in the settings), or when it reads open or shorted
      response = ExtraTemp::sensors.ntc_connected ? temperatureByte(ExtraTemp::sensors.ntc_temperature) : 0xFF;
      break;

    case Command::READ_LED_BRIGHTNESS:  // 0x33
      response = hal::get_led_brightness();
      break;

    case Command::READ_RGB_BRIGHTNESS:  // 0x35
      response = RgbLed::brightness();
      break;

    case Command::RGB_COLOUR_SHOWN:  // 0x37
      response = RgbLed::smbusColourShown() ? 0x01 : 0x00;
      break;

    case Command::RGB_RED:    // 0x38
    case Command::RGB_GREEN:  // 0x39
    case Command::RGB_BLUE:   // 0x3A
      response = RgbLed::smbusColour(static_cast<uint8_t>(command - Command::RGB_RED));
      break;

    case Command::READ_FAN_SPEED:  // 0x10
      response = Fan::getFanSpeed();
      break;

    case Command::INTERRUPT_REASON:  // 0x11
      response = SMI::flags.interrupt_reason;
      SMI::flags.interrupt_reason = 0;  // Clear after reading
      break;

    case Command::READ_RAM_TEST_RESULTS:  // 0x14
      response = ram_test_response0;
      break;

    case Command::READ_RAM_TYPE:  // 0x15
      response = ram_test_response1;
      break;

    case Command::LAST_REGISTER_WRITTEN:  // 0x16
      response = regs.last_write_command;
      break;

    case Command::LAST_BYTE_WRITTEN:  // 0x17
      response = regs.last_write_data;
      break;

    case Command::SCRATCH:  // 0x1B
      response = regs.scratch;
      break;

    case Command::READ_ERROR_CODE:  // 0x0F
      // Written by 0x0E, or set by the boot challenge on failure
      response = bios_response_byte0;
      break;

    case Command::CHALLENGE_BYTE0:  // 0x1C
      response = BootChallenge::getChallengeStructRef().input_byte0;
      break;

    case Command::CHALLENGE_BYTE1:  // 0x1D
      response = BootChallenge::getChallengeStructRef().input_byte1;
      break;

    case Command::CHALLENGE_BYTE2:  // 0x1E
      response = BootChallenge::getChallengeStructRef().input_byte2;
      break;

    case Command::CHALLENGE_BYTE3:  // 0x1F
      response = BootChallenge::getChallengeStructRef().input_byte3;
      break;

    // Write-only commands read as 0
    case Command::CHALLENGE_RESPONSE0:          // 0x20
    case Command::CHALLENGE_RESPONSE1:          // 0x21
    case Command::AUDIO_CLAMP:                  // 0x0B
    case Command::DVD_TRAY_OPERATION:           // 0x0C
    case Command::OS_RESUME:                    // 0x0D
    case Command::WRITE_ERROR_CODE:             // 0x0E
    case Command::RESET:                        // 0x02
    case Command::FAN_OVERRIDE:                 // 0x05
    case Command::REQUEST_FAN_SPEED:            // 0x06
    case Command::LED_OVERRIDE:                 // 0x07
    case Command::LED_STATES:                   // 0x08
    case Command::WRITE_RAM_TEST_RESULTS:       // 0x12
    case Command::WRITE_RAM_TYPE:               // 0x13
    case Command::SOFTWARE_INTERRUPT:           // 0x18
    case Command::OVERRIDE_RESET_ON_TRAY_OPEN:  // 0x19
    case Command::OS_READY:                     // 0x1A
    case Command::WRITE_LED_BRIGHTNESS:         // 0x34
    case Command::WRITE_RGB_BRIGHTNESS:         // 0x36
    default:
      break;
  }

  return response;
}

// In the IRQ: the writes to the slave's own registers, which a read straight
// after must see. Returns true when that's all the write does; the others are
// applied by applyWrite() from the main loop.
static bool storeRegister(uint8_t command, uint8_t data) {
  switch (command) {
    case Command::FIRMWARE_REVISION:  // 0x01
      // Restart the version string
      regs.version_counter = 1;
      return true;

    case Command::WRITE_ERROR_CODE:  // 0x0E
      // Error code from the BIOS (not used in normal operation), read by 0x0F.
      // Logged by applyWrite().
      bios_response_byte0 = data;
      return false;

    case Command::WRITE_RAM_TYPE:  // 0x13
      // RAM type, read by 0x15
      ram_test_response1 = data;
      return true;

    case Command::WRITE_RAM_TEST_RESULTS:  // 0x12
      // RAM test result, read by 0x14. Checked by applyWrite().
      ram_test_response0 = data;
      return false;

    case Command::SCRATCH:  // 0x1B
      // Plain storage for the kernel (bit 1 = display fatal error after the
      // reboot, bit 3 = boot to dashboard, ...)
      regs.scratch = data;
      return true;

    // Applied by applyWrite()
    case Command::RESET:                        // 0x02
    case Command::FAN_OVERRIDE:                 // 0x05
    case Command::REQUEST_FAN_SPEED:            // 0x06
    case Command::LED_OVERRIDE:                 // 0x07
    case Command::LED_STATES:                   // 0x08
    case Command::AUDIO_CLAMP:                  // 0x0B
    case Command::DVD_TRAY_OPERATION:           // 0x0C
    case Command::OS_RESUME:                    // 0x0D
    case Command::OVERRIDE_RESET_ON_TRAY_OPEN:  // 0x19
    case Command::OS_READY:                     // 0x1A
    case Command::CHALLENGE_RESPONSE0:          // 0x20
    case Command::CHALLENGE_RESPONSE1:          // 0x21
    case Command::POWER_CYCLE_DELAY:            // 0x22
    case Command::WRITE_LED_BRIGHTNESS:         // 0x34
    case Command::WRITE_RGB_BRIGHTNESS:         // 0x36
    case Command::RGB_COLOUR_SHOWN:             // 0x37
    case Command::RGB_RED:                      // 0x38
    case Command::RGB_GREEN:                    // 0x39
    case Command::RGB_BLUE:                     // 0x3A
      return false;

    // The rest do nothing: read-only commands, unknown ones, and 0x04 (video
    // mode) and 0x18 (software interrupt), no-ops on the PIC
    default:
      return true;
  }
}

// In the main loop: the writes that act on the other modules
static void applyWrite(uint8_t command, uint8_t data) {
  switch (command) {
    case Command::RESET:  // 0x02
      // Same as the PIC: bit 0 = reset, bit 6 = power cycle,
      // bit 7 = power off
      if (data & 0x01) {
        PLL_Reset::flags.warm_reset_request = true;
        SMI::flags.os_reset_request = true;
      }
      if (data & 0x40) {
        SMI::flags.power_change_requested = true;
        SMI::flags.power_cycle_request = true;
      }
      if (data & 0x80) {
        SMI::flags.power_change_requested = true;
        SMI::flags.power_off_request = true;
      }
      break;

    case Command::FAN_OVERRIDE:  // 0x05
                                 // Bit 0 = use the speed from 0x06 (fan_override)
#if SMC_FIRMWARE_P2L
                                 // P2L: always accepted (the speed starts at 50); turning it off raises
      // the fan to at least 30
      Fan::flags.fan_override = data & 0x01;
      if (!Fan::flags.fan_override) {
        Fan::raiseSpeedTo(30);
      }
#else
                                 // The PIC: only accepted when that speed is valid
      if (!(data & 0x01)) {
        Fan::flags.fan_override = false;
      } else if (Fan::getCustomFanSpeed() <= hal::FAN_MAX_SPEED) {
        Fan::flags.fan_override = true;
      }
#endif
      break;

    case Command::REQUEST_FAN_SPEED:  // 0x06
      // Speed used while 0x05 override is on, limited to 50 as P2L. The
      // PIC stores it as is, and 0x05 then refuses anything above 50
      Fan::setCustomSpeed(data > hal::FAN_MAX_SPEED ? hal::FAN_MAX_SPEED : data);
      break;

    case Command::LED_OVERRIDE:  // 0x07
      // Same as the PIC: bit 0 = show the 0x08 pattern
      Led::flags.led_override = data & 0x01;
      break;

    case Command::LED_STATES:  // 0x08
      // Green phases in bits 0-3, red in bits 4-7
      Led::setGreenPhases((data >> 0) & 0x0F);
      Led::setRedPhases((data >> 4) & 0x0F);
      break;

    case Command::WRITE_LED_BRIGHTNESS:  // 0x34
      // Percent, 1-100, anything else is ignored. Not saved (Settings).
      if (data >= 1 && data <= 100 && data != hal::get_led_brightness()) {
        hal::set_led_brightness(data);
        debug::print(debug::Level::Info, "LED: Brightness %u%% (SMBus 0x34)", data);
      }
      break;

    case Command::WRITE_RGB_BRIGHTNESS:  // 0x36
      // RGB LED brightness, percent, 1-100, anything else is ignored. Not saved.
      if (data != RgbLed::brightness() && RgbLed::setBrightness(data)) {
        debug::print(debug::Level::Info, "RGB: Brightness %u%% (SMBus 0x36)", data);
      }
      break;

    case Command::RGB_COLOUR_SHOWN:  // 0x37
      // Bit 0: show the colour from 0x38-0x3A instead of the automatic ones.
      // Back to automatic at a reset or power off.
      RgbLed::showSmbusColour((data & 0x01) != 0);
      break;

    case Command::RGB_RED:    // 0x38
    case Command::RGB_GREEN:  // 0x39
    case Command::RGB_BLUE:   // 0x3A
      RgbLed::setSmbusColour(static_cast<uint8_t>(command - Command::RGB_RED), data);
      break;

    case Command::AUDIO_CLAMP:  // 0x0B
      // Same as the PIC: bit 0 = 1 clamp, 0 unclamp,
      // carried out by AudioClamp::update
      if (data & 0x01) {
        AudioClamp::flags.audio_clamp_request = true;
      } else {
        AudioClamp::flags.audio_unclamp_request = true;
      }
      break;

    case Command::DVD_TRAY_OPERATION:  // 0x0C
      // Same as the PIC: bit 0 = 1 load, 0 eject, carried
      // out by Dvd::updateEjectRequests
      if (data & 0x01) {
        Dvd::flags.tray_load_request = true;
      } else {
        Dvd::flags.tray_eject_request = true;
      }
      break;

    case Command::OS_RESUME:  // 0x0D
      // Same as the PIC: the OS answering a power/eject SMI, only
      // while one is being handled (waiting_for_os). Without it the SMC resets the Xbox.
      if (!SMI::flags.waiting_for_os) {
        break;
      }
      if (data & 0x01) {
        SMI::flags.power_off_request = true;  // Power off
      }
      if (data & 0x08) {
        SMI::flags.os_reset_request = true;  // Reset
      }
      if (data & 0x02) {
        SMI::flags.os_busy = true;  // Busy, keep waiting
      }
      if (data & 0x04) {
        SMI::flags.os_event_handled = true;  // Handled, no reset
      }
      if (!(data & 0x02)) {
        SMI::flags.waiting_for_os = false;
      }
      break;

    case Command::WRITE_RAM_TEST_RESULTS:  // 0x12
      // As P2L: passed with 0x13 = 0x0F and 0x12 = 0xF0, or 0x13 = 0x00
      // or 0xFF and 0x12 = 0x00. The PIC passes 0x13 = 0x00/0xFF
      // whatever 0x12 holds. The bytes were stored by storeRegister().
      BootChallenge::flags.ram_test_submitted = true;
      BootChallenge::flags.ram_test_passed = (ram_test_response1 == 0x0F && ram_test_response0 == 0xF0) ||
                                             ((ram_test_response1 == 0x00 || ram_test_response1 == 0xFF) && ram_test_response0 == 0x00);
      break;

    case Command::CHALLENGE_RESPONSE0:  // 0x20
      BootChallenge::getChallengeStructRef().response0 = data;
      break;

    case Command::CHALLENGE_RESPONSE1:  // 0x21
      BootChallenge::getChallengeStructRef().response1 = data;
      BootChallenge::flags.challenge_response_received = true;  // Response complete
      break;

    case Command::POWER_CYCLE_DELAY:  // 0x22
      // Same as P11/P2L: ticks to stay off during the next power cycle
      SMI::setPowerCycleDelay(data);
      break;

    case Command::OVERRIDE_RESET_ON_TRAY_OPEN:  // 0x19
      // Same as the PIC: bit 0 disables reset-on-eject, allowed
      // once per boot (reset_on_eject_writable is re-armed by the PLL/reset state machine)
      if (data & 0x01) {
        if (SMI::flags.reset_on_eject_writable) {
          SMI::flags.reset_on_eject_disabled = true;
        }
      } else {
        SMI::flags.reset_on_eject_disabled = false;
      }
      SMI::flags.reset_on_eject_writable = false;
      break;

    case Command::OS_READY:  // 0x1A
      // Bit 0 enables interrupts (os_ready), which lets a power off go through the
      // OS instead of cutting power right away. Writing 0 clears it, as P2L;
      // the PIC only ever sets it
      SMI::flags.os_ready = data & 0x01;
      break;

    case Command::WRITE_ERROR_CODE:  // 0x0E
      // Stored for 0x0F by storeRegister(), kept here for the web interface
      bios_error_seen = true;
      bios_error_code = data;
      debug::print(debug::Level::Info, "BIOS: Error code 0x%02X (%u) written (SMBus 0x0E)", data, data);
      break;

    default:  // storeRegister() did all there was
      break;
  }
}

void applySMBusWrites() {
  PendingWrite write;
  while (pending_writes.pop(write)) {
    applyWrite(write.command, write.data);
  }
  if (const uint16_t dropped = pending_writes.takeDropped(); dropped != 0) {
    debug::print(debug::Level::Warnings, "SMBUS: %u writes lost (queue full)", dropped);
  }
}

void handle_SMBus_interrupt(i2c_inst_t* i2c, i2c_slave_event_t event) {
  switch (event) {
    case I2C_SLAVE_RECEIVE: {  // master has written data
      const uint8_t data = i2c_read_byte_raw(i2c);

      if (expecting_command) {
        regs.command = data;
        expecting_command = false;
      } else {
        // Data byte of a write
        regs.last_write_command = regs.command;
        regs.last_write_data = data;
        if (!storeRegister(regs.command, data)) {
          pending_writes.push({regs.command, data});  // Counted when dropped
        }
        logTransaction(true, regs.command, data);
      }
      break;
    }

    case I2C_SLAVE_REQUEST: {  // master is requesting data
      // Always answer: with an empty TX FIFO the slave stretches the clock and
      // holds the bus
      const uint8_t response = readRegister(regs.command);
      i2c_write_byte_raw(i2c, response);
      logTransaction(false, regs.command, response);
      break;
    }

    case I2C_SLAVE_FINISH:  // master has sent Stop or Restart
      // The first byte written after a (repeated) START is a command byte
      expecting_command = true;
      break;

    default:
      break;
  }
}

// Like the PIC, we master the bus we're a slave on (video encoder,
// temperature sensor), see hal::i2c_master_transfer
static bool masterTransfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
  I2CSniffer::beginOwnTransfer();
  const hal::I2CMasterResult result = hal::i2c_master_transfer(address, tx, tx_len, rx, rx_len);
  I2CSniffer::endOwnTransfer();
  if (result == hal::I2CMasterResult::ok) {
    return true;
  }

  static const char* const reasons[] = {"ok", "bus busy", "nack", "arbitration lost", "timeout"};
  debug::print(debug::Level::Warnings, "I2C: Transfer to address 0x%02X failed: %s", address, reasons[static_cast<int>(result)]);
  return false;
}

bool smbusMasterWrite(uint8_t address, const uint8_t* data, size_t len) {
  return masterTransfer(address, data, len, nullptr, 0);
}

bool smbusMasterReadByte(uint8_t address, uint8_t reg, uint8_t& value) {
  return masterTransfer(address, &reg, 1, &value, 1);
}

// Same as the PIC: CPU from register 0x01 and board from 0x00 of the
// ADM1032 at 0x4C (1.0-1.4). temperature_read_failed is set when a read fails, and the values
// that couldn't be read keep their previous value.
// With the sensor bypass (Fan::temperatureBypass()) the ADM1032 isn't read:
// the CPU temperature is the NTC's and the board's the Pico's, in whole °C. No
// NTC reading (off, open or shorted) fails like a dead ADM1032.
bool read_temperatures() {
  uint8_t value;
  Fan::flags.temperature_read_failed = false;

  if (Fan::temperatureBypass()) {
    if (!ExtraTemp::ntcConfig().enabled || !ExtraTemp::sensors.ntc_connected) {
      Fan::flags.temperature_read_failed = true;
      return false;
    }
    // Plus the offsets (whole °C, in tenths here), then rounded
    const Fan::BypassOffsets& offsets = Fan::bypassOffsets();
    Fan::sensors.CPU_temperature = temperatureByte(static_cast<int16_t>(ExtraTemp::sensors.ntc_temperature + offsets.cpu * 10));
    Fan::sensors.board_temperature = temperatureByte(static_cast<int16_t>(ExtraTemp::sensors.pico_temperature + offsets.board * 10));
    return true;
  }

  if (!smbusMasterReadByte(I2C_TEMP_SENSOR_ADDRESS, 0x01, value)) {
    Fan::flags.temperature_read_failed = true;
    return false;
  }
  Fan::sensors.CPU_temperature = value;

  if (!smbusMasterReadByte(I2C_TEMP_SENSOR_ADDRESS, 0x00, value)) {
    Fan::flags.temperature_read_failed = true;
    return false;
  }
  Fan::sensors.board_temperature = value;
  return true;
}

}  // namespace SMC
