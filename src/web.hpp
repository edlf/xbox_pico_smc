#ifndef WEB_HPP
#define WEB_HPP

#include "config.hpp"

namespace SMC {
namespace Web {

// Starts the access point and the web server on core 1, unless it's off in the
// settings (Settings::webEnabled()): then nothing runs and service() does nothing
void start();

// Called from the main loop on core 0 every tick: runs the page's commands,
// publishes the status for the page and prints core 1's log messages
void service();

}  // namespace Web
}  // namespace SMC

#endif  // WEB_HPP
