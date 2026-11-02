#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <pico/sync.h>

#include "hal.hpp"
#include "pin_assignments.hpp"
#include "debug.hpp"

#if SMC_RGB_LED
#include "hardware/pio.h"
#include "ws2812.pio.h"
#endif

namespace hal {

// FAN PWM
static pwm_config pwm_cfg;
static uint       pwm_slice;
static uint       pwm_channel;

// Front panel LEDs are active low, like on the PIC (RA0/RA1 low = on). They
// run as PWM so they can be dimmed: ~1 kHz, output inverted so the level is the
// on-time. The brightness starts at SMC_LED_BRIGHTNESS and the web interface
// can change it.
constexpr uint16_t LED_PWM_WRAP = 999;
constexpr uint32_t LED_PWM_HZ = 1000;

static constexpr uint16_t ledLevel(uint8_t percent) {
  return static_cast<uint16_t>((uint32_t)(LED_PWM_WRAP + 1) * percent / 100);
}

static volatile uint8_t  led_brightness = SMC_LED_BRIGHTNESS;
static volatile uint16_t led_on_level = ledLevel(SMC_LED_BRIGHTNESS);
static uint8_t           led_fade = 255;  // Power up/down fade, 255 = full brightness
static bool              green_lit = false;
static bool              red_lit = false;

// PWM level of a lit LED: brightness scaled by the fade
static uint16_t litLevel() {
  return static_cast<uint16_t>((uint32_t)led_on_level * led_fade / 255);
}

// Lit LEDs follow a brightness or fade change right away
static void updateLitLeds() {
  if (green_lit) {
    pwm_set_gpio_level(pins::LED_GREEN, litLevel());
  }
  if (red_lit) {
    pwm_set_gpio_level(pins::LED_RED, litLevel());
  }
}

static void initLeds() {
  for (const uint8_t pin : {pins::LED_RED, pins::LED_GREEN}) {
    const uint slice = pwm_gpio_to_slice_num(pin);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, (float)clock_get_hz(clk_sys) / (LED_PWM_HZ * (LED_PWM_WRAP + 1)));
    pwm_config_set_wrap(&cfg, LED_PWM_WRAP);
    pwm_config_set_output_polarity(&cfg, true, true);
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(pin, 0);  // Off
  }
  // Only now hand the pins to the PWM, so they never flash on
  gpio_set_function(pins::LED_RED, GPIO_FUNC_PWM);
  gpio_set_function(pins::LED_GREEN, GPIO_FUNC_PWM);
}

void led_green_on() {
  green_lit = true;
  pwm_set_gpio_level(pins::LED_GREEN, litLevel());
}

void led_green_off() {
  green_lit = false;
  pwm_set_gpio_level(pins::LED_GREEN, 0);
}

void led_red_on() {
  red_lit = true;
  pwm_set_gpio_level(pins::LED_RED, litLevel());
}

void led_red_off() {
  red_lit = false;
  pwm_set_gpio_level(pins::LED_RED, 0);
}

void set_led_brightness(uint8_t percent) {
  if (percent < 1) {
    percent = 1;
  } else if (percent > 100) {
    percent = 100;
  }
  led_brightness = percent;
  led_on_level = ledLevel(percent);
  updateLitLeds();
}

void set_led_fade(uint8_t level) {
  led_fade = level;
  updateLitLeds();
}

uint8_t get_led_brightness() {
  return led_brightness;
}

// Pico onboard LED, PWM driven (same rate as the front panel LEDs) so it can
// breathe while the Xbox is off
void pico_led_on() {
#ifdef PICO_DEFAULT_LED_PIN
  pwm_set_gpio_level(pins::PICO_LED, LED_PWM_WRAP + 1);
#endif
}

void pico_led_off() {
#ifdef PICO_DEFAULT_LED_PIN
  pwm_set_gpio_level(pins::PICO_LED, 0);
#endif
}

void set_pico_led_level(const uint8_t level) {
#ifdef PICO_DEFAULT_LED_PIN
  // Squared, so the change looks even
  pwm_set_gpio_level(pins::PICO_LED, static_cast<uint16_t>((uint32_t)(LED_PWM_WRAP + 1) * level * level / (255 * 255)));
#else
  (void)level;
#endif
}

// ADC: the RP2040/RP2350's internal temperature sensor and the optional NTC.
// Each read averages a few conversions (2 us each) to smooth the noise.
constexpr uint8_t ADC_SAMPLES = 16;

static uint16_t readAdc(const uint channel) {
  adc_select_input(channel);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
    sum += adc_read();
  }
  return static_cast<uint16_t>(sum / ADC_SAMPLES);
}

static void initAdc() {
  adc_init();
  adc_set_temp_sensor_enabled(true);
  if (SMC_NTC_SENSOR) {
    adc_gpio_init(pins::NTC_ADC);  // Digital input off, no pulls
  }
}

uint16_t read_pico_temperature_adc() {
  return readAdc(ADC_TEMPERATURE_CHANNEL_NUM);
}

uint16_t read_ntc_adc() {
  return readAdc(pins::NTC_ADC - ADC_BASE_PIN);
}

static bool state_pll = false;

void PLL_on() {
  gpio_put(pins::PLL_ENABLE, 1);

  if (!state_pll) {
    debug::print_message("GPIO: Assert PLL");
    state_pll = true;
  }
}

void PLL_off() {
  gpio_put(pins::PLL_ENABLE, 0);

  if (state_pll) {
    debug::print_message("GPIO: Deassert PLL");
    state_pll = false;
  }
}

void smi_pin_on() {
  gpio_put(pins::SMI, 1);
}

void smi_pin_off() {
  gpio_put(pins::SMI, 0);
}

void audio_clamp_on() {
  gpio_put(pins::AUDIO_CLAMP, 1);
}

void audio_clamp_off() {
  gpio_put(pins::AUDIO_CLAMP, 0);
}

static bool state_psu = false;

void turn_on_psu() {
  gpio_put(pins::POWER_ON, 1);

  if (!state_psu) {
    debug::print_message("GPIO: Turn on xbox PSU");
    state_psu = true;
  }
}

void turn_off_psu() {
  gpio_put(pins::POWER_ON, 0);

  if (state_psu) {
    debug::print_message("GPIO: Turn off xbox PSU");
    state_psu = false;
  }
}

// Open drain like the PIC (TRISA.5): pull the line low to assert, otherwise let
// the line's 1k pull-up hold it high
void dvd_eject_assert() {
  gpio_set_dir(pins::DVD_EJECT, GPIO_OUT);
}

void dvd_eject_release() {
  gpio_set_dir(pins::DVD_EJECT, GPIO_IN);
}

void rtc_dump_on() {
  gpio_put(pins::RTC_DUMP, 1);
}

void rtc_dump_off() {
  gpio_put(pins::RTC_DUMP, 0);
}

bool get_power_ok() {
  return gpio_get(pins::POWER_OK);
}

// RP2350-E9: while a pad's input buffer is on it leaks current that pulls a
// weakly driven pin up to about 2.2 V. The front panel buttons only pull their
// line down weakly (a press measured 1.6 V), and AV mode 1 is pulled up to
// POWON, so in standby only that unpowered rail holds it low and it read high
// (no cable, which powers the Xbox on). As the erratum suggests, the buffer is
// only switched on for the read.
static bool readWeakInput(const uint8_t pin) {
#if PICO_RP2350
  gpio_set_input_enabled(pin, true);
  busy_wait_us(10);
  const bool level = gpio_get(pin);
  gpio_set_input_enabled(pin, false);
  return level;
#else
  return gpio_get(pin);
#endif
}

// Gets video cable type
uint8_t get_video_mode() {
  uint8_t video_mode = 0;

  // Video mode is set as 0b0000_1110
  if (readWeakInput(pins::VIDEO_MODE_0)) {
    video_mode = video_mode | (1 << 1);
  }
  if (readWeakInput(pins::VIDEO_MODE_1)) {
    video_mode = video_mode | (1 << 2);
  }
  if (readWeakInput(pins::VIDEO_MODE_2)) {
    video_mode = video_mode | (1 << 3);
  }

  return video_mode;
}

uint8_t get_tray_state() {
  uint8_t tray_state = 0;

  // Tray state is set as 0b0111_0000
  if (gpio_get(pins::TRAY_STATE_0)) {
    tray_state = tray_state | (1 << 4);
  }
  if (gpio_get(pins::TRAY_STATE_1)) {
    tray_state = tray_state | (1 << 5);
  }
  if (gpio_get(pins::TRAY_STATE_2)) {
    tray_state = tray_state | (1 << 6);
  }

  return tray_state;
}

bool power_button_pressed() {
  // High is not pressed
  return !readWeakInput(pins::SW_POWER);
}

bool eject_button_pressed() {
  // High is not pressed
  return !readWeakInput(pins::SW_EJECT);
}

// Fan PWM, same output as the PIC's CCP1: ~9.8 kHz, 10-bit, and
// inverted: 50 holds the pin low, and lower speeds keep it high for
// (50 - speed)% of each period. The PIC also has speed 0 (pin held high, fan
// stopped); here the speed never goes below FAN_MIN_SPEED.
constexpr uint16_t FAN_PWM_WRAP = 1023;
constexpr uint32_t FAN_PWM_HZ = 9766;  // PIC: 10 MHz / 4 / 256 (PR2 = 0xFF)

static uint8_t fan_speed_set = 0xFF;

void set_fan_speed(uint8_t speed) {
  if (speed < FAN_MIN_SPEED) {
    speed = FAN_MIN_SPEED;
  } else if (speed > FAN_MAX_SPEED) {
    speed = FAN_MAX_SPEED;
  }

  const uint16_t level = static_cast<uint16_t>((uint32_t)(FAN_PWM_WRAP + 1) * (FAN_MAX_SPEED - speed) / (2 * FAN_MAX_SPEED));
  pwm_set_chan_level(pwm_slice, pwm_channel, level);

  if (speed != fan_speed_set) {
    debug::print_debug("GPIO: Fan speed " + std::to_string(speed)); // Fan::setSpeed() logs the change (INFO)
    fan_speed_set = speed;
  }
}

void fan_pin_low() {
  pwm_set_chan_level(pwm_slice, pwm_channel, 0);
  fan_speed_set = 0xFF;
}

// SMBus master on the slave's own pins, bit-banged like the PIC does.
// Switching the I2C block between slave and master doesn't
// work reliably once the host has used the slave, so the block stays a slave
// and only loses its pins for the duration of a transfer.
namespace {

constexpr uint32_t i2c_half_period_us = 5;  // ~100 kHz
constexpr uint32_t i2c_clock_stretch_timeout_us = 1000;
constexpr uint32_t i2c_bus_idle_us = 50;

// Open drain by hand: output low to pull down, input to let the pull-up win
void sclLow() {
  gpio_set_dir(pins::I2C_SCL, GPIO_OUT);
}

void sdaLow() {
  gpio_set_dir(pins::I2C_SDA, GPIO_OUT);
}

void sdaRelease() {
  gpio_set_dir(pins::I2C_SDA, GPIO_IN);
}

void halfPeriod() {
  busy_wait_us(i2c_half_period_us);
}

bool sclReleaseAndWait() {
  gpio_set_dir(pins::I2C_SCL, GPIO_IN);

  const absolute_time_t deadline = make_timeout_time_us(i2c_clock_stretch_timeout_us);

  while (!gpio_get(pins::I2C_SCL)) {
    if (time_reached(deadline)) {
      return false;
    }
  }

  return true;
}

void takePins() {
  gpio_put(pins::I2C_SCL, 0);
  gpio_put(pins::I2C_SDA, 0);
  gpio_set_dir(pins::I2C_SCL, GPIO_IN);
  gpio_set_dir(pins::I2C_SDA, GPIO_IN);
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_SIO);
  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_SIO);
}

void returnPins() {
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_I2C);
  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_I2C);
}

// Both lines must stay high for a while, so we don't cut into someone else's
// transfer (the input works whatever the pin function is)
bool busIdle() {
  const absolute_time_t deadline = make_timeout_time_us(i2c_bus_idle_us);

  while (!time_reached(deadline)) {
    if (!gpio_get(pins::I2C_SCL) || !gpio_get(pins::I2C_SDA)) {
      return false;
    }
  }

  return true;
}

I2CMasterResult start(bool repeated) {
  sdaRelease();
  halfPeriod();

  if (!sclReleaseAndWait()) {
    return I2CMasterResult::timeout;
  }

  if (!gpio_get(pins::I2C_SDA)) {
    return repeated ? I2CMasterResult::arbitration_lost : I2CMasterResult::bus_busy;
  }

  halfPeriod();
  sdaLow();

  halfPeriod();
  sclLow();

  return I2CMasterResult::ok;
}

void stop() {
  sdaLow();
  halfPeriod();

  sclReleaseAndWait();
  halfPeriod();

  sdaRelease();
  halfPeriod();
}

I2CMasterResult writeBit(bool bit) {
  if (bit) {
    sdaRelease();
  } else {
    sdaLow();
  }

  halfPeriod();

  if (!sclReleaseAndWait()) {
    return I2CMasterResult::timeout;
  }

  // Someone else pulling SDA low while we send a 1: they win the bus
  const bool lost = bit && !gpio_get(pins::I2C_SDA);

  halfPeriod();
  sclLow();

  return lost ? I2CMasterResult::arbitration_lost : I2CMasterResult::ok;
}

I2CMasterResult readBit(bool& bit) {
  sdaRelease();
  halfPeriod();

  if (!sclReleaseAndWait()) {
    return I2CMasterResult::timeout;
  }

  bit = gpio_get(pins::I2C_SDA);

  halfPeriod();
  sclLow();

  return I2CMasterResult::ok;
}

I2CMasterResult writeByte(uint8_t value) {
  for (int8_t i = 7; i >= 0; i--) {
    const I2CMasterResult result = writeBit(value & (1 << i));
    if (result != I2CMasterResult::ok) {
      return result;
    }
  }

  bool                  nack;
  const I2CMasterResult result = readBit(nack);

  if (result != I2CMasterResult::ok) {
    return result;
  }

  return nack ? I2CMasterResult::nack : I2CMasterResult::ok;
}

I2CMasterResult readByte(uint8_t& value, bool ack) {
  value = 0;

  for (uint8_t i = 0; i < 8; i++) {
    bool                  bit;
    const I2CMasterResult result = readBit(bit);

    if (result != I2CMasterResult::ok) {
      return result;
    }

    value = (value << 1) | bit;
  }

  return writeBit(!ack);
}

I2CMasterResult transfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
  I2CMasterResult result = start(false);

  if (result == I2CMasterResult::ok) {
    result = writeByte(address << 1);
  }

  for (size_t i = 0; i < tx_len && result == I2CMasterResult::ok; i++) {
    result = writeByte(tx[i]);
  }

  if (result == I2CMasterResult::ok && rx_len > 0) {
    result = start(true);

    if (result == I2CMasterResult::ok) {
      result = writeByte((address << 1) | 1);
    }

    for (size_t i = 0; i < rx_len && result == I2CMasterResult::ok; i++) {
      result = readByte(rx[i], i + 1 < rx_len);
    }
  }

  if (result == I2CMasterResult::arbitration_lost || result == I2CMasterResult::bus_busy) {
    // Not our bus right now, just let go
    sdaRelease();
    sclReleaseAndWait();
  } else {
    stop();
  }

  return result;
}

}  // namespace

I2CMasterResult i2c_master_transfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
  if (!busIdle()) {
    return I2CMasterResult::bus_busy;
  }

  takePins();
  const I2CMasterResult result = transfer(address, tx, tx_len, rx, rx_len);
  returnPins();

  return result;
}

// Frees a device stuck holding SDA low: 9 clocks on SCL, then a STOP. The PIC
// does the same when the temperature sensor stops answering.
void i2c_bus_recovery() {
  debug::print_message("I2C: Bus recovery");

  takePins();

  for (uint8_t i = 0; i < 9; i++) {
    sclLow();
    halfPeriod();
    sclReleaseAndWait();
    halfPeriod();
  }

  stop();
  returnPins();
}

void init() {
  debug::print_message("GPIO: Init");

  initLeds();
  initAdc();
  rgb_led_init();

  // Idle levels before the pins become outputs, as in the PIC's port_init:
  // SMI released, DVD eject released
  gpio_put(pins::SMI, 1);
  gpio_put(pins::DVD_EJECT, 0);  // Open drain, only ever drives low

  gpio_set_function(pins::SW_POWER, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SW_POWER, GPIO_IN);
  gpio_pull_up(pins::SW_POWER);

  gpio_set_function(pins::SW_EJECT, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SW_EJECT, GPIO_IN);
  gpio_pull_up(pins::SW_EJECT);

#if PICO_RP2350
  // Input buffers only on while reading, see readWeakInput()
  gpio_set_input_enabled(pins::SW_POWER, false);
  gpio_set_input_enabled(pins::SW_EJECT, false);
#endif

  gpio_set_function(pins::SYSTEM_RESET, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SYSTEM_RESET, GPIO_OUT);

  gpio_set_function(pins::SMI, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SMI, GPIO_OUT);

  gpio_set_function(pins::DVD_EJECT, GPIO_FUNC_SIO);
  gpio_disable_pulls(pins::DVD_EJECT);
  dvd_eject_release();

  gpio_set_function(pins::AUDIO_CLAMP, GPIO_FUNC_SIO);
  gpio_set_dir(pins::AUDIO_CLAMP, GPIO_OUT);

  gpio_set_function(pins::POWER_OK, GPIO_FUNC_SIO);
  gpio_set_dir(pins::POWER_OK, GPIO_IN);
  gpio_disable_pulls(pins::POWER_OK);

  gpio_set_function(pins::FAN_PWM1, GPIO_FUNC_PWM);
  gpio_set_dir(pins::FAN_PWM1, GPIO_OUT);

  gpio_set_function(pins::POWER_ON, GPIO_FUNC_SIO);
  gpio_set_dir(pins::POWER_ON, GPIO_OUT);

  gpio_set_function(pins::RTC_DUMP, GPIO_FUNC_SIO);
  gpio_set_dir(pins::RTC_DUMP, GPIO_OUT);

  gpio_set_function(pins::PLL_ENABLE, GPIO_FUNC_SIO);
  gpio_set_dir(pins::PLL_ENABLE, GPIO_OUT);

  // No internal pulls on the drive, AV and PSU inputs
  gpio_set_function(pins::VIDEO_MODE_0, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_0, GPIO_IN);
  gpio_disable_pulls(pins::VIDEO_MODE_0);
  gpio_set_function(pins::VIDEO_MODE_1, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_1, GPIO_IN);
  gpio_disable_pulls(pins::VIDEO_MODE_1);
  gpio_set_function(pins::VIDEO_MODE_2, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_2, GPIO_IN);
  gpio_disable_pulls(pins::VIDEO_MODE_2);

#if PICO_RP2350
  // Input buffers only on while reading, see readWeakInput()
  gpio_set_input_enabled(pins::VIDEO_MODE_0, false);
  gpio_set_input_enabled(pins::VIDEO_MODE_1, false);
  gpio_set_input_enabled(pins::VIDEO_MODE_2, false);
#endif

  gpio_set_function(pins::TRAY_STATE_0, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_0, GPIO_IN);
  gpio_disable_pulls(pins::TRAY_STATE_0);

  gpio_set_function(pins::TRAY_STATE_1, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_1, GPIO_IN);
  gpio_disable_pulls(pins::TRAY_STATE_1);

  gpio_set_function(pins::TRAY_STATE_2, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_2, GPIO_IN);
  gpio_disable_pulls(pins::TRAY_STATE_2);

  gpio_set_function(pins::DVD_ACTIVE, GPIO_FUNC_SIO);
  gpio_set_dir(pins::DVD_ACTIVE, GPIO_IN);
  gpio_disable_pulls(pins::DVD_ACTIVE);

#ifdef PICO_DEFAULT_LED_PIN
  {
    const uint slice = pwm_gpio_to_slice_num(pins::PICO_LED);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, (float)clock_get_hz(clk_sys) / (LED_PWM_HZ * (LED_PWM_WRAP + 1)));
    pwm_config_set_wrap(&cfg, LED_PWM_WRAP);
    pwm_init(slice, &cfg, true);
    pwm_set_gpio_level(pins::PICO_LED, 0);
    gpio_set_function(pins::PICO_LED, GPIO_FUNC_PWM);
  }
#endif

  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(pins::I2C_SDA);
  gpio_pull_up(pins::I2C_SCL);

  debug::print_message("GPIO: PWM Init");
  pwm_slice = pwm_gpio_to_slice_num(pins::FAN_PWM1);
  pwm_channel = pwm_gpio_to_channel(pins::FAN_PWM1);
  pwm_cfg = pwm_get_default_config();
  pwm_config_set_clkdiv(&pwm_cfg, (float)clock_get_hz(clk_sys) / (FAN_PWM_HZ * (FAN_PWM_WRAP + 1)));
  pwm_config_set_wrap(&pwm_cfg, FAN_PWM_WRAP);
  pwm_init(pwm_slice, &pwm_cfg, false);
  fan_pin_low();
  pwm_set_enabled(pwm_slice, true);

  debug::print_message("GPIO: I2C Init");
  i2c_init(I2C_PORT, I2C_BAUDRATE);
}

void assertSystemReset() {
  debug::print_message("GPIO: Assert reset");
  gpio_put(pins::SYSTEM_RESET, 0);
}

void liftSystemReset() {
  debug::print_message("GPIO: Lift reset");
  gpio_put(pins::SYSTEM_RESET, 1);
}

bool system_reset_asserted() {
  return !gpio_get_out_level(pins::SYSTEM_RESET);
}

#if SMC_RGB_LED
// WS2812 RGB LED on pins::RGB_LED, driven by a PIO state machine
static PIO      rgb_pio;
static uint     rgb_sm;
static bool     rgb_ready = false;
static uint32_t rgb_sent = 0xFFFFFFFF;  // Nothing sent yet

void rgb_led_init() {
  uint offset;

  if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &rgb_pio, &rgb_sm, &offset, pins::RGB_LED, 1, true)) {
    debug::print_warn("GPIO: No free PIO state machine for the RGB LED");
    return;
  }

  ws2812_program_init(rgb_pio, rgb_sm, offset, pins::RGB_LED);
  rgb_ready = true;
  set_rgb_led(0, 0, 0);
}

void set_rgb_led(const uint8_t red, const uint8_t green, const uint8_t blue) {
  if (!rgb_ready) {
    return;
  }

  const uint32_t grb = ((uint32_t)(green) << 16) | ((uint32_t)(red) << 8) | (blue);

  if (grb == rgb_sent || pio_sm_is_tx_fifo_full(rgb_pio, rgb_sm)) {
    return;  // Unchanged, or try again next tick
  }

  pio_sm_put(rgb_pio, rgb_sm, grb << 8);
  rgb_sent = grb;
}
#else
void rgb_led_init() {}
void set_rgb_led(uint8_t, uint8_t, uint8_t) {}
#endif

void enableWatchdog() {
  debug::print_message("PICO: Watchdog enabled");
  watchdog_enable(1000, 1);
}

void petWatchdog() {
  watchdog_update();
}

// Main loop tick, same as the PIC's Timer1: timer1_init() starts a period,
// timer1_wait() blocks until it has elapsed (returns at once if it already
// has) and starts the next one.
static uint32_t        timer1_period_us = TICK_US;
static absolute_time_t timer1_deadline;

void timer1_init(uint32_t delay_us) {
  timer1_period_us = delay_us;
  timer1_deadline = make_timeout_time_us(delay_us);
}

void timer1_wait() {
  busy_wait_until(timer1_deadline);
  timer1_init(timer1_period_us);
}

void disable_and_discard_interrupts() {
  (void)save_and_disable_interrupts();
}

}  // namespace hal
