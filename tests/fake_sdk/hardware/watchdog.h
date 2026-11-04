#ifndef FAKE_HARDWARE_WATCHDOG_H
#define FAKE_HARDWARE_WATCHDOG_H
inline bool watchdog_caused_reboot() {
  return false;
}
#endif
