#include "serial_console.hpp"
#include "smc.hpp"
#include "hal.hpp"

#include <cstring>

namespace SMC {
namespace SerialConsole {

constexpr size_t line_size = 16;  // Longer lines are cut off and refused
static char      line[line_size];
static size_t    length = 0;

static void command(const char* text) {
  if (strcmp(text, "power") == 0) {
    debug::print(debug::Level::Info, "SERIAL: Power button");
    SMI::flags.power_change_requested = true;
  } else if (strcmp(text, "eject") == 0) {
    debug::print(debug::Level::Info, "SERIAL: Eject button");
    Dvd::flags.eject_change_requested = true;
    Dvd::flags.eject_button_event = true;
  } else if (strcmp(text, "help") == 0) {
    debug::print(debug::Level::Info, "SERIAL: Commands: power (power button), eject (eject button)");
  } else {
    debug::print(debug::Level::Info, "SERIAL: Unknown command '%s', try help", text);
  }
}

void service() {
  // A few characters per tick at most: the UART is slow, this never waits
  for (int count = 0; count < 32; count++) {
    const int c = hal::serial_read();
    if (c < 0) {
      return;
    }
    if (c == '\r' || c == '\n') {
      if (length > 0) {
        line[length < line_size ? length : line_size - 1] = '\0';
        command(line);
      }
      length = 0;
    } else if (length < line_size - 1) {
      line[length++] = static_cast<char>(c);
    } else {
      length = line_size;  // Too long: refused at the end of the line
    }
  }
}

}  // namespace SerialConsole
}  // namespace SMC
