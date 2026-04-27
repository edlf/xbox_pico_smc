#include "smc.hpp"
#include "pico_hal.hpp"
#include "utils.hpp"

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
}; // namespace Command

// SMBus state
static bool command_received = false;
static uint8_t smbus_command = 0;
static uint8_t smbus_data = 0;
static uint8_t smbus_version_counter = 0;

uint8_t smbus_read_handler(uint8_t command) {
  uint8_t response = 0;

  switch (command) {
  case Command::FIRMWARE_REVISION: // 0x01
    // Multi-byte response: 'P', '0', '1'
    if (smbus_version_counter == 0) {
      response = 'P';
    } else if (smbus_version_counter == 1) {
      response = '0';
    } else if (smbus_version_counter == 2) {
      response = '1';
    }
    break;

  case Command::TRAY_STATE: // 0x03
    response = sensors.tray_status;
    // Set bit 0 if tray_insert flag is set
    if (flags.bitfield_DATA_6F & 0x20) {
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
    response = smbus_command;
    break;

  case Command::LAST_BYTE_WRITTEN: // 0x17
    response = smbus_data;
    break;

  case Command::SCRATCH: // 0x1B
    response = Fan::getCustomFanSpeed();
    break;

  case Command::READ_ERROR_CODE:             // 0x0F
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
  default:
    // Unsupported/write-only commands - return 0
    response = 0;
    break;
  }

  return response;
}

void smbus_write_handler(uint8_t command, uint8_t data) {
  switch (command) {
  case Command::RESET: // 0x02
    // data: 0x00 = warm reset, 0x01 = cold reset
    if (data == 0x00) {
      // Warm reset - keep power state
      PLL_Reset::setWarmReset1();
    } else if (data == 0x01) {
      // Cold reset - power cycle
      PLL_Reset::setColdReset();
    }
    break;

  case Command::VIDEO_MODE: // 0x04
    // Set video mode (0=NTSC, 1=PAL, etc)
    sensors.vmode = data;
    setStatusBit(Status::video_mode_changed);
    setInterruptReason(InterruptReason_av_mode_changed);
    break;

  case Command::FAN_OVERRIDE: // 0x05
    // data: bit 0 = enable override, bit 1-7 = unused
    if (data & 0x01) {
      Fan::enableCustomFanSpeed();
    } else {
      Fan::disableCustomFanSpeed();
    }
    break;

  case Command::REQUEST_FAN_SPEED: // 0x06
    // Set requested fan speed (0-50)
    Fan::setFanSpeed(data);
    break;

  case Command::LED_OVERRIDE: // 0x07
    // data: bit 0 = enable override
    if (data & 0x01) {
      Led::setManualControl(true);
    } else {
      Led::resetState();
    }
    break;

  case Command::LED_STATES: // 0x08
    // data: LED color/state bits
    Led::setGreenPhases((data >> 0) & 0x0F);
    Led::setRedPhases((data >> 4) & 0x0F);
    break;

  case Command::AUDIO_CLAMP: // 0x0B
    // data: 0 = unclamp, non-zero = clamp
    if (data == 0) {
      AudioClamp::unclamp();
    } else {
      AudioClamp::clamp();
    }
    break;

  case Command::DVD_TRAY_OPERATION: // 0x0C
    // data: 0x00 = close, 0x01 = eject
    if (data == 0x00) {
      // Close tray
      pico_hal::dvd_eject_off();
    } else if (data == 0x01) {
      // Eject tray
      pico_hal::dvd_eject_on();
      setInterruptReason(InterruptReason_dvd_tray0);
    }
    break;

  case Command::OS_RESUME: // 0x0D
    // Mark system as resuming from sleep/standby
    clearStatusBit(Status::prepare_for_shutdown);
    break;

  case Command::WRITE_ERROR_CODE: // 0x0E
    // Store error code from BIOS (not used in normal operation)
    bios_response_byte0 = data;
    break;

  case Command::WRITE_RAM_TEST_RESULTS: // 0x12
    // Store RAM test results (bit 0 = pass/fail)
    ram_test_response0 = data;
    break;

  case Command::WRITE_RAM_TYPE: // 0x13
    // Store RAM type information
    ram_test_response1 = data;
    break;

  case Command::SOFTWARE_INTERRUPT: // 0x18
    // Trigger SMI interrupt signal to BIOS
    fireSystemInterrupt();
    if (data == 0x00) {
      // Clear the interrupt after delivery
      pico_hal::smi_pin_off();
    }
    break;

  case Command::OVERRIDE_RESET_ON_TRAY_OPEN: // 0x19
    // data: 0 = reset on tray open, non-zero = don't reset
    if (data == 0x00) {
      utils::clearBitNo(flags.bitfield_DATA_71, 0); // Allow reset on tray open
    } else {
      utils::setBitNo(flags.bitfield_DATA_71, 0); // Override reset on tray open
    }
    break;

  case Command::OS_READY: // 0x1A
    // Mark OS as fully booted and ready
    clearStatusBit(Status::first_execution);
    setStatusBit(Status::prepare_for_shutdown); // Clear prepare_for_shutdown
    break;

  case Command::SCRATCH: // 0x1B
    // TODO: This looks broken AF
    // Write to scratch register (general purpose storage)
    Fan::setCustomSpeed(data);
    break;

  case Command::FIRMWARE_REVISION:     // 0x01 (read-only, ignore writes)
  case Command::TRAY_STATE:            // 0x03 (read-only, ignore writes)
  case Command::CPU_TEMPERATURE:       // 0x09 (read-only, ignore writes)
  case Command::AIR_TEMPERATURE:       // 0x0A (read-only, ignore writes)
  case Command::READ_FAN_SPEED:        // 0x10 (read-only, ignore writes)
  case Command::INTERRUPT_REASON:      // 0x11 (read-only, ignore writes)
  case Command::READ_RAM_TEST_RESULTS: // 0x14 (read-only, ignore writes)
  case Command::READ_RAM_TYPE:         // 0x15 (read-only, ignore writes)
  case Command::LAST_REGISTER_WRITTEN: // 0x16 (read-only, ignore writes)
  case Command::LAST_BYTE_WRITTEN:     // 0x17 (read-only, ignore writes)
    // Read-only commands - silently ignore writes
    break;

  default:
    // Unknown command - silently ignore
    break;
  }
}

void handle_SMBus_interrupt(i2c_inst_t* i2c, i2c_slave_event_t event) {
  uint8_t data;

  switch (event) {
  case I2C_SLAVE_RECEIVE: // master has written data
    data = i2c_read_byte_raw(i2c);

    if (!command_received) {
      // This is the command byte
      smbus_command = data;
      smbus_data = 0;
      command_received = true;
      smbus_version_counter = 0;
    } else {
      // This is a data byte (write operation)
      smbus_data = data;
      smbus_write_handler(smbus_command, smbus_data);
    }
    break;

  case I2C_SLAVE_REQUEST: // master is requesting data
    if (command_received) {
      uint8_t response = smbus_read_handler(smbus_command);

      // For FIRMWARE_REVISION, increment counter for multi-byte response
      if (smbus_command == Command::FIRMWARE_REVISION) {
        smbus_version_counter++;
        if (smbus_version_counter > 2) {
          smbus_version_counter = 0;
        }
      }

      i2c_write_byte_raw(i2c, response);
    }
    break;

  case I2C_SLAVE_FINISH: // master has sent Stop or Restart
    command_received = false;
    smbus_version_counter = 0;
    break;

  default:
    break;
  }
}

void read_temperatures() {
  // Function only valid for 1.0-1.4

  // TODO: CPU
  sensors.CPU_temperature = 55;
  sensors.CPU_temperature_previous = sensors.CPU_temperature;
  sensors.board_temperature = 55;
  // if (i2c_read(I2C_TEMP_SENSOR_ADDRESS, 0x01, &(sensors.CPU_temperature))) {
  //   sensors.CPU_temperature_previous = sensors.CPU_temperature;
  // }

  // Board, near MCPX for 1.0-1.4
  // i2c_read(I2C_TEMP_SENSOR_ADDRESS, 0x02, &(sensors.board_temperature));
}

} // namespace SMC
