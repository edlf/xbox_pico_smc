// Front panel LEDs (PWM, dimmable), the Pico's own LED and the optional WS2812
// RGB LED

#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include "hal.hpp"
#include "hal_internal.hpp"
#include "pin_assignments.hpp"
#include "debug.hpp"

#if SMC_RGB_LED
#include "hardware/pio.h"
#include "ws2812.pio.h"
#endif

namespace hal {

// Front panel LEDs are active low, like on the PIC (RA0/RA1 low = on). They
// run as PWM so they can be dimmed: ~1 kHz, output inverted so the level is the
// on-time. The brightness starts at SMC_LED_BRIGHTNESS (or the saved setting)
// and the web interface, SMBus 0x34 and the USB config can change it.
constexpr uint16_t LED_PWM_WRAP = 999;
constexpr uint32_t LED_PWM_HZ = 1000;

static constexpr uint16_t ledLevel(uint8_t percent) {
  return static_cast<uint16_t>(static_cast<uint32_t>(LED_PWM_WRAP + 1) * percent / 100);
}

struct FrontLeds {
  volatile uint8_t  brightness = SMC_LED_BRIGHTNESS;  // Percent
  volatile uint16_t on_level = ledLevel(SMC_LED_BRIGHTNESS);
  uint8_t           fade = 255;  // Power up/down fade, 255 = full brightness
  bool              green_lit = false;
  bool              red_lit = false;
};
static FrontLeds leds;

// PWM level of a lit LED: brightness scaled by the fade
static uint16_t litLevel() {
  return static_cast<uint16_t>(static_cast<uint32_t>(leds.on_level) * leds.fade / 255);
}

// Lit LEDs follow a brightness or fade change right away
static void updateLitLeds() {
  if (leds.green_lit) {
    pwm_set_gpio_level(pins::LED_GREEN, litLevel());
  }
  if (leds.red_lit) {
    pwm_set_gpio_level(pins::LED_RED, litLevel());
  }
}

// Starts the pin's PWM slice at the LED rate, off. Inverted for the active low
// front panel LEDs.
static void initLedPwm(const uint8_t pin, const bool inverted) {
  pwm_config cfg = pwm_get_default_config();
  pwm_config_set_clkdiv(&cfg, static_cast<float>(clock_get_hz(clk_sys)) / (LED_PWM_HZ * (LED_PWM_WRAP + 1)));
  pwm_config_set_wrap(&cfg, LED_PWM_WRAP);
  pwm_config_set_output_polarity(&cfg, inverted, inverted);
  pwm_init(pwm_gpio_to_slice_num(pin), &cfg, true);
  pwm_set_gpio_level(pin, 0);
}

void init_leds() {
  initLedPwm(pins::LED_RED, true);
  initLedPwm(pins::LED_GREEN, true);
  // Only now hand the pins to the PWM, so they never flash on
  gpio_set_function(pins::LED_RED, GPIO_FUNC_PWM);
  gpio_set_function(pins::LED_GREEN, GPIO_FUNC_PWM);

#ifdef PICO_DEFAULT_LED_PIN
  initLedPwm(pins::PICO_LED, false);
  gpio_set_function(pins::PICO_LED, GPIO_FUNC_PWM);
#endif
}

void led_green_on() {
  leds.green_lit = true;
  pwm_set_gpio_level(pins::LED_GREEN, litLevel());
}

void led_green_off() {
  leds.green_lit = false;
  pwm_set_gpio_level(pins::LED_GREEN, 0);
}

void led_red_on() {
  leds.red_lit = true;
  pwm_set_gpio_level(pins::LED_RED, litLevel());
}

void led_red_off() {
  leds.red_lit = false;
  pwm_set_gpio_level(pins::LED_RED, 0);
}

void set_led_brightness(uint8_t percent) {
  if (percent < 1) {
    percent = 1;
  } else if (percent > 100) {
    percent = 100;
  }
  leds.brightness = percent;
  leds.on_level = ledLevel(percent);
  updateLitLeds();
}

void set_led_fade(uint8_t level) {
  leds.fade = level;
  updateLitLeds();
}

uint8_t get_led_brightness() {
  return leds.brightness;
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
  pwm_set_gpio_level(pins::PICO_LED, static_cast<uint16_t>(static_cast<uint32_t>(LED_PWM_WRAP + 1) * level * level / (255 * 255)));
#else
  (void)level;
#endif
}

#if SMC_RGB_LED
// WS2812 RGB LED on pins::RGB_LED, driven by a PIO state machine
struct RgbLed {
  PIO      pio = nullptr;
  uint     sm = 0;
  bool     ready = false;
  uint32_t sent = 0xFFFFFFFF;  // Nothing sent yet
};
static RgbLed rgb;

void rgb_led_init() {
  uint offset;

  if (!pio_claim_free_sm_and_add_program_for_gpio_range(&ws2812_program, &rgb.pio, &rgb.sm, &offset, pins::RGB_LED, 1, true)) {
    debug::print_warn("GPIO: No free PIO state machine for the RGB LED");
    return;
  }

  ws2812_program_init(rgb.pio, rgb.sm, offset, pins::RGB_LED);
  rgb.ready = true;
  set_rgb_led(0, 0, 0);
}

void set_rgb_led(const uint8_t red, const uint8_t green, const uint8_t blue) {
  if (!rgb.ready) {
    return;
  }

  const uint32_t grb = (static_cast<uint32_t>(green) << 16) | (static_cast<uint32_t>(red) << 8) | (blue);

  if (grb == rgb.sent || pio_sm_is_tx_fifo_full(rgb.pio, rgb.sm)) {
    return;  // Unchanged, or try again next tick
  }

  pio_sm_put(rgb.pio, rgb.sm, grb << 8);
  rgb.sent = grb;
}
#else
void rgb_led_init() {}
void set_rgb_led(uint8_t, uint8_t, uint8_t) {}
#endif

}  // namespace hal
