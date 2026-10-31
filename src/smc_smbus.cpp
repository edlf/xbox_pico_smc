#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"
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
constexpr uint8_t POWER_CYCLE_DELAY = 0x22; // P11 only
}; // namespace Command

// SMBus state. Like the PIC, the command byte is kept after the transaction
// ends: a read byte is "write command, repeated START, read", and the START
// must not make us forget the command.
static bool expecting_command = true; // Next received byte is a command byte
static uint8_t smbus_command = 0;
static uint8_t last_write_command = 0; // SMBus 0x16, only updated by writes
static uint8_t last_write_data = 0;    // SMBus 0x17, only updated by writes
// SMBus 0x01: the SMC_FIRMWARE version, or "P01" with SMC_REPORT_P01
#if SMC_FIRMWARE_P11 && !SMC_REPORT_P01
const char version_string[] = "P11";
#else
const char version_string[] = "P01";
#endif
static uint8_t smbus_version_counter = 1; // 1-3 = 'P', '0' or '1', '1'
static uint8_t scratch_register = 0;      // SMBus 0x1B, kernel boot flags kept across reboots

// Transactions are printed from the main loop (printSMBusLog): printing in
// the IRQ would stretch the SMBus clock for milliseconds per byte
struct SMBusLogEntry {
  bool write;
  uint8_t command;
  uint8_t data;
};
static constexpr uint8_t smbus_log_size = 32;
static SMBusLogEntry smbus_log[smbus_log_size];
static volatile uint8_t smbus_log_head = 0; // Written by the IRQ
static volatile uint8_t smbus_log_tail = 0; // Written by the main loop

static void logTransaction(bool write, uint8_t command, uint8_t data) {
  const uint8_t next = (smbus_log_head + 1) % smbus_log_size;
  if (next == smbus_log_tail) {
    return; // Full, drop
  }
  smbus_log[smbus_log_head] = {write, command, data};
  smbus_log_head = next;
}

// Same as the PIC's RAM_init (0xB58), called at boot and on power off
void resetSMBusRegisters() {
  smbus_command = 0;
  last_write_command = 0;
  last_write_data = 0;
  smbus_version_counter = 1;
  scratch_register = 0;
  bios_response_byte0 = 0; // Error code, SMBus 0x0E/0x0F
}

void printSMBusLog() {
  while (smbus_log_tail != smbus_log_head) {
    const SMBusLogEntry entry = smbus_log[smbus_log_tail];
    smbus_log_tail = (smbus_log_tail + 1) % smbus_log_size;
    if (debug::debug_enabled()) { // Debug level only, they're too frequent for Info
      debug::print_debug(std::string("SMBUS: ") + (entry.write ? "Write" : "Read ") + " cmd [" +
                         utils::toHex(entry.command) + "] data [" + utils::toHex(entry.data) + "]");
    }
  }
}

uint8_t smbus_read_handler(uint8_t command) {
  uint8_t response = 0;

  switch (command) {
  case Command::FIRMWARE_REVISION: // 0x01
    // One character per read, the index survives between transactions and
    // is only reset by writing 0x01 (same as the PIC, 0x07A)
    if (smbus_version_counter == 2) {
      response = version_string[1];
      smbus_version_counter = 3;
    } else if (smbus_version_counter == 3) {
      response = '1';
      smbus_version_counter = 1;
    } else {
      response = 'P';
      smbus_version_counter++;
    }
    break;

  case Command::TRAY_STATE: // 0x03
    response = sensors.tray_status;
    // Set bit 0 if tray_insert flag is set
    if (flags.tray_moving) {
      response |= 0x01;
    }
    break;

  case Command::VIDEO_MODE: // 0x04
    response = (sensors.vmode >> 1) & 0x07;
    break;

  case Command::CPU_TEMPERATURE: // 0x09
    response = sensors.CPU_temperature;
    break;

  case Command::AIR_TEMPERATURE: // 0x0A
    response = sensors.board_temperature;
    break;

  case Command::READ_FAN_SPEED: // 0x10
    response = Fan::getFanSpeed();
    break;

  case Command::INTERRUPT_REASON: // 0x11
    response = flags.interrupt_reason;
    flags.interrupt_reason = 0; // Clear after reading
    break;

  case Command::READ_RAM_TEST_RESULTS: // 0x14
    response = ram_test_response0;
    break;

  case Command::READ_RAM_TYPE: // 0x15
    response = ram_test_response1;
    break;

  case Command::LAST_REGISTER_WRITTEN: // 0x16
    response = last_write_command;
    break;

  case Command::LAST_BYTE_WRITTEN: // 0x17
    response = last_write_data;
    break;

  case Command::SCRATCH: // 0x1B
    response = scratch_register;
    break;

  case Command::READ_ERROR_CODE: // 0x0F
    // Written by 0x0E, or set by the boot challenge on failure
    response = bios_response_byte0;
    break;

  case Command::CHALLENGE_BYTE0: // 0x1C
    response = BootChallenge::getChallengeStructRef().input_byte0;
    break;

  case Command::CHALLENGE_BYTE1: // 0x1D
    response = BootChallenge::getChallengeStructRef().input_byte1;
    break;

  case Command::CHALLENGE_BYTE2: // 0x1E
    response = BootChallenge::getChallengeStructRef().input_byte2;
    break;

  case Command::CHALLENGE_BYTE3: // 0x1F
    response = BootChallenge::getChallengeStructRef().input_byte3;
    break;

  case Command::CHALLENGE_RESPONSE0: // 0x20
  case Command::CHALLENGE_RESPONSE1: // 0x21
  case Command::AUDIO_CLAMP:                 // 0x0B
  case Command::DVD_TRAY_OPERATION:          // 0x0C
  case Command::OS_RESUME:                   // 0x0D
  case Command::WRITE_ERROR_CODE:            // 0x0E
  case Command::RESET:                       // 0x02
  case Command::FAN_OVERRIDE:                // 0x05
  case Command::REQUEST_FAN_SPEED:           // 0x06
  case Command::LED_OVERRIDE:                // 0x07
  case Command::LED_STATES:                  // 0x08
  case Command::WRITE_RAM_TEST_RESULTS:      // 0x12
  case Command::WRITE_RAM_TYPE:              // 0x13
  case Command::SOFTWARE_INTERRUPT:          // 0x18
  case Command::OVERRIDE_RESET_ON_TRAY_OPEN: // 0x19
  case Command::OS_READY:                    // 0x1A
    // Unsupported/write-only commands - return 0
  default:
    break;
  }

  return response;
}

void smbus_write_handler(uint8_t command, uint8_t data) {
  switch (command) {
  case Command::RESET: // 0x02
    // Same as the PIC (0x13B): bit 0 = reset, bit 6 = power cycle,
    // bit 7 = power off
    if (data & 0x01) {
      flags.warm_reset_request = true;
      flags.os_reset_request = true;
    }
    if (data & 0x40) {
      flags.power_change_requested = true;
      flags.power_cycle_request = true;
    }
    if (data & 0x80) {
      flags.power_change_requested = true;
      flags.power_off_request = true;
    }
    break;

  case Command::VIDEO_MODE: // 0x04
    // No-op on the PIC, the mode comes from the AV connector
    break;

  case Command::FAN_OVERRIDE: // 0x05
    // Same as the PIC (0x14A): bit 0 = use the speed from 0x06 (fan_override), only
    // accepted when that speed is valid
    if (!(data & 0x01)) {
      flags.fan_override = false;
    } else if (Fan::getCustomFanSpeed() <= hal::FAN_MAX_SPEED) {
      flags.fan_override = true;
    }
    break;

  case Command::REQUEST_FAN_SPEED: // 0x06
    // Speed used while 0x05 override is on (0-50)
    Fan::setCustomSpeed(data);
    break;

  case Command::LED_OVERRIDE: // 0x07
    // Same as the PIC (0x159): bit 0 = show the 0x08 pattern (led_override)
    if (data & 0x01) {
      flags.led_override = true;
    } else {
      flags.led_override = false;
    }
    break;

  case Command::LED_STATES: // 0x08
    // data: LED color/state bits
    Led::setGreenPhases((data >> 0) & 0x0F);
    Led::setRedPhases((data >> 4) & 0x0F);
    break;

  case Command::AUDIO_CLAMP: // 0x0B
    // Same as the PIC (0x16B): bit 0 = 1 clamp, 0 unclamp,
    // carried out by AudioClamp::update
    if (data & 0x01) {
      flags.audio_clamp_request = true;
    } else {
      flags.audio_unclamp_request = true;
    }
    break;

  case Command::DVD_TRAY_OPERATION: // 0x0C
    // Same as the PIC (0x170): bit 0 = 1 load, 0 eject, carried
    // out by Dvd::updateDvdTrayEject
    if (data & 0x01) {
      flags.tray_load_request = true;
    } else {
      flags.tray_eject_request = true;
    }
    break;

  case Command::OS_RESUME: // 0x0D
    // Same as the PIC (0x175): the OS answering a power/eject SMI, only
    // while one is being handled (waiting_for_os). Without it the SMC resets the Xbox.
    if (!flags.waiting_for_os) {
      break;
    }
    if (data & 0x01) {
      flags.power_off_request = true; // Power off
    }
    if (data & 0x08) {
      flags.os_reset_request = true; // Reset
    }
    if (data & 0x02) {
      flags.os_busy = true; // Busy, keep waiting
    }
    if (data & 0x04) {
      flags.os_event_handled = true; // Handled, no reset
    }
    if (!(data & 0x02)) {
      flags.waiting_for_os = false;
    }
    break;

  case Command::WRITE_ERROR_CODE: // 0x0E
    // Store error code from BIOS (not used in normal operation)
    bios_response_byte0 = data;
    break;

  case Command::WRITE_RAM_TEST_RESULTS: // 0x12
    // Same as the PIC (0x188)
    flags.ram_test_submitted = true;
    flags.ram_test_passed = false;
    ram_test_response0 = data;
    if (ram_test_response1 == 0xFF || ram_test_response1 == 0x00 ||
        (ram_test_response1 == 0x0F && ram_test_response0 == 0xF0)) {
      flags.ram_test_passed = true;
    }
    break;

  case Command::CHALLENGE_RESPONSE0: // 0x20
    BootChallenge::getChallengeStructRef().response0 = data;
    break;

  case Command::CHALLENGE_RESPONSE1: // 0x21
    BootChallenge::getChallengeStructRef().response1 = data;
    flags.challenge_response_received = true; // Response complete
    break;

#if SMC_FIRMWARE_P11
  case Command::POWER_CYCLE_DELAY: // 0x22
    // Same as P11 (0x1CA): ticks to stay off during the next power cycle
    SMI::setPowerCycleDelay(data);
    break;
#endif

  case Command::WRITE_RAM_TYPE: // 0x13
    // Store RAM type information
    ram_test_response1 = data;
    break;

  case Command::SOFTWARE_INTERRUPT: // 0x18
    // No-op on the PIC
    break;

  case Command::OVERRIDE_RESET_ON_TRAY_OPEN: // 0x19
    // Same as the PIC (0x1A8): bit 0 disables reset-on-eject, allowed
    // once per boot (reset_on_eject_writable is re-armed by the PLL/reset state machine)
    if (data & 0x01) {
      if (flags.reset_on_eject_writable) {
        flags.reset_on_eject_disabled = true;
      }
    } else {
      flags.reset_on_eject_disabled = false;
    }
    flags.reset_on_eject_writable = false;
    break;

  case Command::OS_READY: // 0x1A
    // Same as the PIC (0x1B1): bit 0 enables interrupts (os_ready), which lets a
    // power off go through the OS instead of cutting power right away
    if (data & 0x01) {
      flags.os_ready = true;
    }
    break;

  case Command::SCRATCH: // 0x1B
    // Plain storage for the kernel (bit 1 = display fatal error after the
    // reboot, bit 3 = boot to dashboard, ...)
    scratch_register = data;
    break;

  case Command::FIRMWARE_REVISION: // 0x01
    // Restart the version string
    smbus_version_counter = 1;
    break;

  case Command::TRAY_STATE:            // 0x03 (read-only, ignore writes)
  case Command::CPU_TEMPERATURE:       // 0x09 (read-only, ignore writes)
  case Command::AIR_TEMPERATURE:       // 0x0A (read-only, ignore writes)
  case Command::READ_FAN_SPEED:        // 0x10 (read-only, ignore writes)
  case Command::INTERRUPT_REASON:      // 0x11 (read-only, ignore writes)
  case Command::READ_RAM_TEST_RESULTS: // 0x14 (read-only, ignore writes)
  case Command::READ_RAM_TYPE:         // 0x15 (read-only, ignore writes)
  case Command::LAST_REGISTER_WRITTEN: // 0x16 (read-only, ignore writes)
  case Command::LAST_BYTE_WRITTEN:     // 0x17 (read-only, ignore writes)
  case Command::CHALLENGE_BYTE0:       // 0x1C (read-only, ignore writes)
  case Command::CHALLENGE_BYTE1:       // 0x1D (read-only, ignore writes)
  case Command::CHALLENGE_BYTE2:       // 0x1E (read-only, ignore writes)
  case Command::CHALLENGE_BYTE3:       // 0x1F (read-only, ignore writes)
    // Read-only commands - silently ignore writes
    break;

  default:
    break;
  }
}

void handle_SMBus_interrupt(i2c_inst_t* i2c, i2c_slave_event_t event) {
  switch (event) {
  case I2C_SLAVE_RECEIVE: { // master has written data
    const uint8_t data = i2c_read_byte_raw(i2c);

    if (expecting_command) {
      smbus_command = data;
      expecting_command = false;
    } else {
      // Data byte of a write
      last_write_command = smbus_command;
      last_write_data = data;
      smbus_write_handler(smbus_command, data);
      logTransaction(true, smbus_command, data);
    }
    break;
  }

  case I2C_SLAVE_REQUEST: { // master is requesting data
    // Always answer: with an empty TX FIFO the slave stretches the clock and
    // holds the bus
    const uint8_t response = smbus_read_handler(smbus_command);
    i2c_write_byte_raw(i2c, response);
    logTransaction(false, smbus_command, response);
    break;
  }

  case I2C_SLAVE_FINISH: // master has sent Stop or Restart
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
  const hal::I2CMasterResult result = hal::i2c_master_transfer(address, tx, tx_len, rx, rx_len);
  if (result == hal::I2CMasterResult::ok) {
    return true;
  }

  static const char* const reasons[] = {"ok", "bus busy", "nack", "arbitration lost", "timeout"};
  debug::print_warn("I2C: Transfer to address " + utils::toHex(address) + " failed: " +
                    reasons[static_cast<int>(result)]);
  return false;
}

bool smbusMasterWrite(uint8_t address, const uint8_t* data, size_t len) {
  return masterTransfer(address, data, len, nullptr, 0);
}

bool smbusMasterReadByte(uint8_t address, uint8_t reg, uint8_t& value) {
  return masterTransfer(address, &reg, 1, &value, 1);
}

// Same as the PIC (0xE71): CPU from register 0x01 and board from 0x00 of the
// ADM1032 at 0x4C (1.0-1.4). temperature_read_failed is set when a read fails, and the values
// that couldn't be read keep their previous value.
bool read_temperatures() {
  uint8_t value;
  flags.temperature_read_failed = false;

  if (!smbusMasterReadByte(I2C_TEMP_SENSOR_ADDRESS, 0x01, value)) {
    flags.temperature_read_failed = true;
    return false;
  }
  sensors.CPU_temperature = value;

  if (!smbusMasterReadByte(I2C_TEMP_SENSOR_ADDRESS, 0x00, value)) {
    flags.temperature_read_failed = true;
    return false;
  }
  sensors.board_temperature = value;
  return true;
}

} // namespace SMC
