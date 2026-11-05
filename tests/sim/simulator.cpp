#include "simulator.hpp"

#include <cstdarg>
#include <cstdio>

#include "fakes.hpp"
#include "hal.hpp"
#include "hardware/i2c.h"
#include "pico/i2c_slave.h"
#include "pico/stdlib.h"
#include "pin_assignments.hpp"
#include "smc.hpp"

namespace sim {

constexpr uint32_t tick_us = hal::TICK_US;

static Simulator* instance = nullptr;

static int ticks(uint32_t ms) {
  const int n = static_cast<int>((ms * 1000 + tick_us - 1) / tick_us);
  return n < 1 ? 1 : n;
}

// A line from the models, next to the firmware's own
__attribute__((format(printf, 1, 2))) static void simLog(const char* format, ...) {
  char    line[256];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  fake::log.push_back(std::string("(SIM  ) ") + line);
  if (fake::echo_log) {
    printf("[%012llu] %s\n", static_cast<unsigned long long>(fake::now_us()), fake::log.back().c_str());
  }
}

Simulator::Simulator() {
  fake::reset();
  fake::eraseFlash();
  fake::hal.video_mode = 0x02;  // HDTV AV pack
  fake::hal.tray_state = 0x70;  // Drive unpowered
  next_tick_us = fake::now_us();

  instance = this;
  fake::tick_hook = &Simulator::tickHook;
  thread = std::thread(&Simulator::firmwareThread, this);

  // Up to the first tick
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return pauses > 0 || finished; });
}

Simulator::~Simulator() {
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = true;
  }
  cv.notify_all();
  thread.join();
  fake::tick_hook = nullptr;
  instance = nullptr;
}

void Simulator::firmwareThread() {
  while (true) {
    try {
      SMC::main_loop();
    } catch (const fake::WatchdogReboot&) {
      // The Pico reboots: main() starts again (RAM isn't cleared here, but
      // main_loop() sets up every state machine again)
      watchdog_reboots++;
      simLog("Pico rebooted by its watchdog");
      continue;
    } catch (const Stop&) {
    }
    break;
  }

  std::lock_guard<std::mutex> lock(mutex);
  finished = true;
  cv.notify_all();
}

void Simulator::tickHook() {
  instance->tick();
}

// Called by hal::timer1_wait(): the end of a main loop tick
void Simulator::tick() {
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (fake::now_us() >= run_until_us) {
      pauses++;
      cv.notify_all();
      cv.wait(lock, [&] { return stopping || fake::now_us() < run_until_us; });
      if (stopping) {
        throw Stop{};
      }
    }
  }

  // Sleep to the start of the next tick, as the Pico does
  next_tick_us += tick_us;
  if (fake::now_us() < next_tick_us) {
    fake::advance_us(next_tick_us - fake::now_us());
  } else {
    next_tick_us = fake::now_us();  // Overran
  }
  fake::checkWatchdog();
  fake::hal.transfers.clear();

  psuModel();
  driveModel();
  osModel();
}

void Simulator::run(uint32_t ms) {
  std::unique_lock<std::mutex> lock(mutex);
  if (finished) {
    return;
  }
  run_until_us = fake::now_us() + uint64_t{ms} * 1000;
  const uint64_t paused = pauses;
  cv.notify_all();
  cv.wait(lock, [&] { return pauses > paused || finished; });
}

void Simulator::pressPower() {
  holdPower(100);
}

void Simulator::holdPower(uint32_t ms) {
  fake::hal.power_button = true;
  run(ms);
  fake::hal.power_button = false;
}

void Simulator::pressEject() {
  fake::hal.eject_button = true;
  run(100);
  fake::hal.eject_button = false;
}

void Simulator::pushTray() {
  if (mech == Mech::open && drive_powered) {
    simLog("Tray pushed closed by hand");
    mech = Mech::closing;
    mech_ticks = ticks(drive.move_ms);
  }
}

void Simulator::setDisc(bool disc) {
  drive.disc = disc;
}

void Simulator::setAvPins(uint8_t pins) {
  fake::hal.video_mode = pins;
}

void Simulator::psuDropout(uint32_t ms) {
  dropout_ticks = ticks(ms);
}

void Simulator::smbusWrite(uint8_t command, uint8_t data) {
  fake_i2c0.rx = command;
  SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_RECEIVE);
  fake_i2c0.rx = data;
  SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_RECEIVE);
  SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_FINISH);
}

uint8_t Simulator::smbusRead(uint8_t command) {
  fake_i2c0.rx = command;
  SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_RECEIVE);
  SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_FINISH);
  SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_REQUEST);
  SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_FINISH);
  return fake_i2c0.tx;
}

// PSU: POWER_OK a few ticks after POWON, dropping at once when it's switched
// off or during psuDropout()
void Simulator::psuModel() {
  if (fake::hal.psu_on && !psu_was_on) {
    power_ons++;
    psu_ticks = 0;
    simLog("PSU on");
  } else if (!fake::hal.psu_on && psu_was_on) {
    simLog("PSU off");
  }
  psu_was_on = fake::hal.psu_on;

  if (!fake::hal.psu_on) {
    setPowerOk(false);
  } else if (dropout_ticks > 0) {
    if (fake::hal.power_ok) {
      simLog("PSU dropout: POWER_OK low");
    }
    dropout_ticks--;
    setPowerOk(false);
  } else if (++psu_ticks >= 3) {
    setPowerOk(true);
  }
}

void Simulator::setPowerOk(bool ok) {
  const bool was = fake::hal.power_ok;
  fake::hal.power_ok = ok;
  // The firmware's POWER_OK falling edge interrupt
  if (was && !ok && fake::power_ok_irq.enabled && fake::power_ok_irq.callback != nullptr) {
    fake::power_ok_irq.callback(pins::POWER_OK, GPIO_IRQ_EDGE_FALL);
  }
}

const char* Simulator::trayName(uint8_t pins) {
  switch (pins) {
    case 0x00:
      return "closed";
    case 0x10:
      return "open";
    case 0x20:
      return "unloading";
    case 0x30:
      return "opening";
    case 0x40:
      return "closed, no disc";
    case 0x50:
      return "closing";
    case 0x60:
      return "closed, disc";
    case 0x70:
      return "drive off";
    default:
      return "?";
  }
}

// DVD drive: the eject line toggles the tray, a moving tray reverses, and a
// closed tray looks for a disc
void Simulator::driveModel() {
  const uint8_t before = fake::hal.tray_state;
  const bool    eject_low = !fake::hal.dvd_eject;
  const bool    eject_pressed = eject_was_low && !eject_low;
  eject_was_low = eject_low;

  if (!drive.present || !fake::hal.power_ok) {
    drive_powered = false;
    fake::hal.tray_state = 0x70;
  } else {
    if (!drive_powered) {
      drive_powered = true;
      if (mech == Mech::closed) {
        detect_ticks = ticks(drive.detect_ms);
      }
    }

    if (eject_pressed) {
      switch (mech) {
        case Mech::closed:
          if (drive.disc && detect_ticks == 0) {
            mech = Mech::unloading;
            mech_ticks = ticks(drive.unload_ms);
          } else {
            mech = Mech::opening;
            mech_ticks = ticks(drive.move_ms);
          }
          break;
        case Mech::open:
          mech = Mech::closing;
          mech_ticks = ticks(drive.move_ms);
          break;
        case Mech::opening:  // Back the way it came
          mech = Mech::closing;
          mech_ticks = ticks(drive.move_ms) - mech_ticks + 1;
          break;
        case Mech::closing:
          mech = Mech::opening;
          mech_ticks = ticks(drive.move_ms) - mech_ticks + 1;
          break;
        case Mech::unloading:
          break;
      }
    }

    if (mech_ticks > 0 && --mech_ticks == 0) {
      switch (mech) {
        case Mech::unloading:
          mech = Mech::opening;
          mech_ticks = ticks(drive.move_ms);
          break;
        case Mech::opening:
          mech = Mech::open;
          break;
        case Mech::closing:
          mech = Mech::closed;
          detect_ticks = ticks(drive.detect_ms);
          break;
        default:
          break;
      }
    }
    if (mech == Mech::closed && detect_ticks > 0) {
      detect_ticks--;
    }

    switch (mech) {
      case Mech::closed:
        fake::hal.tray_state = detect_ticks > 0 ? 0x00 : (drive.disc ? 0x60 : 0x40);
        break;
      case Mech::unloading:
        fake::hal.tray_state = 0x20;
        break;
      case Mech::opening:
        fake::hal.tray_state = 0x30;
        break;
      case Mech::open:
        fake::hal.tray_state = 0x10;
        break;
      case Mech::closing:
        fake::hal.tray_state = 0x50;
        break;
    }
  }

  if (fake::hal.tray_state != before) {
    simLog("Drive: %s (0x%02X)", trayName(fake::hal.tray_state), fake::hal.tray_state);
  }
}

// What a stock BIOS does over SMBus while it boots (from a real log)
void Simulator::bootSequence() {
  smbusWrite(0x01, 0x00);  // Version string from the start
  for (int i = 0; i < 3; i++) {
    smbusRead(0x01);
  }
  if (os.ram_test_passes) {
    smbusWrite(0x13, 0x0F);  // RAM test passed
    smbusWrite(0x12, 0xF0);
  } else {
    smbusWrite(0x13, 0x01);  // Failed, bank 0
    smbusWrite(0x12, 0x01);
  }

  // Boot challenge: the BIOS's answer, the same formula as the SMC checks
  const uint8_t b0 = smbusRead(0x1C);
  const uint8_t b1 = smbusRead(0x1D);
  const uint8_t b2 = smbusRead(0x1E);
  const uint8_t b3 = smbusRead(0x1F);
  uint8_t       e0 = 0x33;
  uint8_t       e1 = 0xED;
  for (int i = 0; i < 4; i++) {
    e0 = static_cast<uint8_t>(
        (static_cast<uint8_t>(b0 << 2) ^ static_cast<uint8_t>(b1 + 0x39) ^ static_cast<uint8_t>(b2 >> 2) ^ static_cast<uint8_t>(b3 + 0x63) ^ e1) + e0);
    e1 = static_cast<uint8_t>((static_cast<uint8_t>(b0 + 0x0B) ^ static_cast<uint8_t>(b1 >> 2) ^ static_cast<uint8_t>(b2 + 0x1B) ^ e0) + e1);
  }
  smbusWrite(0x20, e0);
  smbusWrite(0x21, e1);
  smbusWrite(0x01, 0x00);
}

// The OS answers each SMI: reads the reason (0x11) and answers with 0x0D. On
// an eject press it also asks for the tray to open or close (0x0C).
void Simulator::answerSmi() {
  const uint8_t reasons = smbusRead(0x11);

  if (reasons & 0x01) {  // Power button
    switch (os.power_answer) {
      case OsOptions::PowerAnswer::power_off:
        simLog("OS: SMI 0x%02X, power off (0x0D = 0x01)", reasons);
        smbusWrite(0x0D, 0x01);
        break;
      case OsOptions::PowerAnswer::busy:
        simLog("OS: SMI 0x%02X, busy (0x0D = 0x02)", reasons);
        smbusWrite(0x0D, 0x02);
        break;
      case OsOptions::PowerAnswer::none:
        simLog("OS: SMI 0x%02X, not answered", reasons);
        break;
    }
    return;
  }

  smbusWrite(0x0D, 0x04);  // Handled
  if (reasons & 0x20) {    // Eject button
    const bool open = (smbusRead(0x03) & 0x70) == 0x10;
    simLog("OS: SMI 0x%02X, %s the tray (0x0C = 0x%02X)", reasons, open ? "close" : "open", open ? 0x01 : 0x00);
    smbusWrite(0x0C, open ? 0x01 : 0x00);
    if (os.write_19_after_eject) {
      smbusWrite(0x19, 0x01);
    }
  } else {
    simLog("OS: SMI 0x%02X, handled (0x0D = 0x04)", reasons);
  }
}

// The CPU runs while it has power, its PLL and no reset: BIOS, then the
// kernel, then the dashboard answering SMIs
void Simulator::osModel() {
  const bool cpu = fake::hal.power_ok && fake::hal.pll_on && !fake::hal.system_reset;
  if (!cpu) {
    if (os_state != OsState::off) {
      simLog("CPU stopped (%s)", fake::hal.power_ok ? "reset" : "no power");
    }
    os_state = OsState::off;
    smi_ticks = -1;
    smi_seen = fake::hal.smi_pulses;
    return;
  }

  if (os_state == OsState::off) {
    boots++;
    simLog("CPU running, BIOS");
    os_state = OsState::bios;
    os_ticks = 5;
  }

  if (os_ticks > 0 && --os_ticks == 0) {
    if (os_state == OsState::bios) {
      bootSequence();
      os_state = OsState::kernel;
      os_ticks = 50;
    } else if (os_state == OsState::kernel) {
      smbusWrite(0x1A, 0x01);  // OS ready: power off goes through the OS
      os_state = OsState::running;
      simLog("Dashboard running");
    }
  }

  if (fake::hal.smi_pulses != smi_seen) {
    smi_seen = fake::hal.smi_pulses;
    if (os_state == OsState::running && os.answers && smi_ticks < 0) {
      smi_ticks = ticks(os.answer_delay_ms);
    }
  }
  if (smi_ticks > 0 && --smi_ticks == 0) {
    smi_ticks = -1;
    answerSmi();
  }
}

bool Simulator::xboxOn() const {
  return fake::hal.psu_on;
}

bool Simulator::osRunning() const {
  return os_state == OsState::running;
}

uint8_t Simulator::trayPins() const {
  return fake::hal.tray_state;
}

double Simulator::seconds() const {
  return static_cast<double>(fake::now_us()) / 1e6;
}

std::string Simulator::status() const {
  char text[256];
  snprintf(text, sizeof(text), "%.2f s: Xbox %s, OS %s, tray %s, LED%s%s%s, %d power on(s), %d reset(s), %d watchdog reboot(s)", seconds(),
           xboxOn() ? "on" : "off",
           os_state == OsState::running ? "running"
           : os_state == OsState::off   ? "off"
           : os_state == OsState::bios  ? "in the BIOS"
                                        : "booting",
           trayName(trayPins()), fake::hal.green_led ? " green" : "", fake::hal.red_led ? " red" : "",
           !fake::hal.green_led && !fake::hal.red_led ? "s off" : "", power_ons, resets(), watchdog_reboots);
  return text;
}

}  // namespace sim
