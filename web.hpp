#ifndef __SMC_WEB__
#define __SMC_WEB__

// Set by CMake (option SMC_WEB_INTERFACE): Wi-Fi access point with a status
// and control page, served from core 1 (W boards only)
#ifndef SMC_WEB_INTERFACE
#define SMC_WEB_INTERFACE 0
#endif

namespace SMC {
namespace Web {

// Starts the access point and the web server on core 1
void start();

// Called from the main loop on core 0 every tick: runs the page's commands,
// publishes the status for the page and prints core 1's log messages
void service();

} // namespace Web
} // namespace SMC

#endif // __SMC_WEB__
