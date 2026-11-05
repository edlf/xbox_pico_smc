// smcx_sim: the SMC firmware running on the PC, driven by commands typed in
// or read from a script file (smcx_sim script.txt). See tests/README.md.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "fakes.hpp"
#include "simulator.hpp"

static const char* const help = R"(Commands (times: 500, 500ms, 2s, 1.5s):
  run <time>         let it run
  power              press the power button (100 ms)
  hold <time>        hold the power button
  eject              press the eject button (100 ms)
  push               push the open tray closed by hand
  disc on|off        a disc in the drive or not
  drive on|off       a drive connected or not
  av <pins>          AV pack pins: 0x02 HDTV, 0x0C standard, 0x0E none, ...
  psu-drop <time>    POWER_OK drops for a while
  os ok|hang         the OS answers SMIs or not
  os power-off|busy|ignore   the OS's answer to a power button SMI
  os 19 on|off       the OS writes 0x19 = 0x01 after an eject request
  write <cmd> <data> SMBus write, e.g. write 0x0C 0x00
  read <cmd>         SMBus read
  status             what the Xbox is doing
  debug on|off       show DEBUG log lines
  quit
Lines starting with # are comments.)";

static bool parseTime(const std::string& text, uint32_t& ms) {
  char*        end = nullptr;
  const double value = std::strtod(text.c_str(), &end);
  if (end == text.c_str() || value < 0) {
    return false;
  }
  const std::string unit(end);
  if (unit.empty() || unit == "ms") {
    ms = static_cast<uint32_t>(value);
  } else if (unit == "s") {
    ms = static_cast<uint32_t>(value * 1000);
  } else {
    return false;
  }
  return true;
}

static bool parseByte(const std::string& text, uint8_t& value) {
  char*      end = nullptr;
  const long v = std::strtol(text.c_str(), &end, 0);
  if (end == text.c_str() || *end != '\0' || v < 0 || v > 0xFF) {
    return false;
  }
  value = static_cast<uint8_t>(v);
  return true;
}

static bool onOff(const std::string& text, bool& value) {
  if (text == "on") {
    value = true;
  } else if (text == "off") {
    value = false;
  } else {
    return false;
  }
  return true;
}

// Runs one command, false to quit
static bool command(sim::Simulator& s, const std::string& line) {
  std::istringstream in(line);
  std::string        cmd, a, b;
  in >> cmd >> a >> b;
  uint32_t ms = 0;
  uint8_t  x = 0, y = 0;
  bool     flag = false;

  if (cmd.empty() || cmd[0] == '#') {
  } else if (cmd == "quit" || cmd == "exit") {
    return false;
  } else if (cmd == "help") {
    puts(help);
  } else if (cmd == "run" && parseTime(a, ms)) {
    s.run(ms);
  } else if (cmd == "power") {
    s.pressPower();
  } else if (cmd == "hold" && parseTime(a, ms)) {
    s.holdPower(ms);
  } else if (cmd == "eject") {
    s.pressEject();
  } else if (cmd == "push") {
    s.pushTray();
  } else if (cmd == "disc" && onOff(a, flag)) {
    s.setDisc(flag);
  } else if (cmd == "drive" && onOff(a, flag)) {
    s.drive.present = flag;
  } else if (cmd == "av" && parseByte(a, x)) {
    s.setAvPins(x);
  } else if (cmd == "psu-drop" && parseTime(a, ms)) {
    s.psuDropout(ms);
  } else if (cmd == "os" && (a == "ok" || a == "hang")) {
    s.os.answers = a == "ok";
  } else if (cmd == "os" && a == "power-off") {
    s.os.power_answer = sim::OsOptions::PowerAnswer::power_off;
  } else if (cmd == "os" && a == "busy") {
    s.os.power_answer = sim::OsOptions::PowerAnswer::busy;
  } else if (cmd == "os" && a == "ignore") {
    s.os.power_answer = sim::OsOptions::PowerAnswer::none;
  } else if (cmd == "os" && a == "19" && onOff(b, flag)) {
    s.os.write_19_after_eject = flag;
  } else if (cmd == "write" && parseByte(a, x) && parseByte(b, y)) {
    s.smbusWrite(x, y);
  } else if (cmd == "read" && parseByte(a, x)) {
    printf("0x%02X: 0x%02X\n", x, s.smbusRead(x));
  } else if (cmd == "status") {
    puts(s.status().c_str());
  } else if (cmd == "debug" && onOff(a, flag)) {
    fake::echo_debug = flag;
  } else {
    printf("? %s (help lists the commands)\n", line.c_str());
  }
  fflush(stdout);
  return true;
}

int main(int argc, char** argv) {
  std::ifstream script;
  if (argc > 1) {
    script.open(argv[1]);
    if (!script) {
      fprintf(stderr, "Can't open %s\n", argv[1]);
      return 1;
    }
  }
  std::istream& in = argc > 1 ? static_cast<std::istream&>(script) : std::cin;
  const bool    typed = argc <= 1;

  fake::echo_log = true;
  sim::Simulator s;
  if (typed) {
    puts("SMCX simulator: the Xbox is off. Type help for the commands.");
  }

  std::string line;
  while (true) {
    if (typed) {
      printf("> ");
      fflush(stdout);
    }
    if (!std::getline(in, line)) {
      break;
    }
    if (!typed && !line.empty() && line[0] != '#') {
      printf("> %s\n", line.c_str());
    }
    if (!command(s, line)) {
      break;
    }
  }
  if (typed) {
    puts(s.status().c_str());
  }
  return 0;
}
