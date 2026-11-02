#include "web.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "dhcp_server.hpp"

#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/unique_id.h"
#include "pico/util/queue.h"
#include "pico/critical_section.h"
#include "lwip/tcp.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Core 1 runs Wi-Fi, lwIP and the HTTP server (polled, nothing else on that
// core). Core 0 keeps running the SMC: once per tick it publishes a status
// snapshot for the page and runs the page's commands, which set the same flags
// as the front panel buttons and the SMBus commands. Core 1 never prints, its
// messages are printed by core 0 (std::cout isn't safe from both cores).

namespace SMC {
namespace Web {
namespace {

enum class Command : uint8_t {
  power,       // Press the power button
  eject,       // Press the eject button
  reset,       // Reset (SMBus 0x02 bit 0)
  fan_speed,   // Fixed fan speed 10-50 (SMBus 0x06 + 0x05)
  fan_auto,    // Back to temperature control
  led_pattern, // Manual LED pattern, arg = red << 4 | green (SMBus 0x08 + 0x07)
  led_auto,    // Back to the normal LED patterns
  threshold,   // Fan temperature threshold index = arg (°C)
  thresholds_default, // All fan temperature thresholds back to the defaults
  led_brightness      // Front panel LED brightness, arg = 1-100 %
};

struct CommandMessage {
  Command command;
  uint8_t arg;
  uint8_t index;
};

struct LogMessage {
  char text[96];
};

struct WebStatus {
  uint32_t uptime_s;
  bool powered;
  uint8_t cpu_temperature;
  uint8_t board_temperature;
  int16_t pico_temperature; // Tenths of °C
  int16_t ntc_temperature;  // Tenths of °C
  bool ntc_connected;
  uint8_t fan_speed;
  bool fan_override;
  uint8_t tray_state;
  bool tray_closed;
  uint8_t av_pack;
  bool av_missing;
  bool overheated;
  bool led_override;
  uint8_t led_red;
  uint8_t led_green;
  uint8_t led_brightness;
  uint8_t interrupt_reason;
  uint8_t thresholds[Fan::max_thresholds];
};

constexpr uint16_t http_port = 80;
constexpr uint8_t max_connections = 4;
constexpr size_t max_request = 512;

critical_section_t status_lock;
WebStatus status;
queue_t commands;
queue_t log_messages;
alignas(8) uint32_t core1_stack[2048]; // 8 KB for lwIP and the HTTP handlers

// ---- Core 1 -----------------------------------------------------------------

void log(const char* format, ...) {
  LogMessage message;
  va_list args;
  va_start(args, format);
  vsnprintf(message.text, sizeof(message.text), format, args);
  va_end(args);
  queue_try_add(&log_messages, &message);
}

const char page[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>OG SMC</title>
<style>
:root{--bg:#101410;--card:#1a221a;--fg:#e4ece4;--dim:#8a9a8a;--accent:#5fbf3f;--warn:#e0a030;--bad:#e05040}
body{margin:0;background:var(--bg);color:var(--fg);font:15px system-ui,sans-serif}
main{max-width:720px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px}#conn{color:var(--dim);font-size:13px;margin-bottom:12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:12px}
.card{background:var(--card);border-radius:10px;padding:12px 14px}
.card h2{font-size:12px;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);margin:0 0 6px}
.value{font-size:22px;font-weight:600}.note{color:var(--dim);font-size:13px;margin-top:2px}
.warn{color:var(--warn)}.bad{color:var(--bad)}
button,select{background:#2a362a;color:var(--fg);border:1px solid #3a4a3a;border-radius:8px;padding:8px 12px;font:inherit;cursor:pointer}
button:hover{border-color:var(--accent)}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin-top:8px}
input[type=range]{flex:1;min-width:120px;accent-color:var(--accent)}
.wide{grid-column:1/-1}.thr{flex:1;min-width:200px}
input[type=number]{width:64px;background:#2a362a;color:var(--fg);border:1px solid #3a4a3a;border-radius:8px;padding:7px 8px;font:inherit}
</style></head><body><main>
<h1>OG SMC</h1><div id="conn">Connecting...</div>
<div class="grid">
<div class="card"><h2>Power</h2><div class="value" id="power">-</div><div class="note" id="extra"></div>
<div class="row"><button onclick="act('power')">Power button</button><button onclick="act('reset')">Reset</button></div></div>
<div class="card"><h2>Temperatures</h2><div class="value"><span id="cpu">-</span> / <span id="board">-</span></div><div class="note">CPU / board</div>
<div class="value"><span id="pico">-</span> / <span id="ntc">-</span></div><div class="note">Pico / NTC</div></div>
<div class="card"><h2>DVD tray</h2><div class="value" id="tray">-</div><div class="note" id="trayNote"></div>
<div class="row"><button onclick="act('eject')">Eject button</button></div></div>
<div class="card"><h2>AV pack</h2><div class="value" id="av">-</div></div>
<div class="card"><h2>Fan</h2><div class="value" id="fan">-</div><div class="note" id="fanNote"></div>
<div class="row"><input type="range" id="fanSpeed" min="10" max="50" value="10" oninput="fanTouched=true;fanValue.textContent=this.value"><span id="fanValue">-</span></div>
<div class="row"><button onclick="fanTouched=false;act('fan&speed='+fanSpeed.value)">Set speed</button><button onclick="fanTouched=false;act('fan_auto')">Auto</button></div></div>
<div class="card"><h2>LEDs</h2><div class="value" id="led">-</div>
<div class="row"><select id="ledPattern">
<option value="0,15">Green</option><option value="15,0">Red</option><option value="15,15">Orange</option>
<option value="0,10">Blink green</option><option value="10,0">Blink red</option><option value="10,10">Blink orange</option>
<option value="5,10">Red / green</option><option value="0,0">Off</option></select></div>
<div class="row"><button onclick="setLed()">Set pattern</button><button onclick="act('led_auto')">Auto</button></div>
<div class="row"><span>Brightness</span><input type="range" id="ledBright" min="1" max="100" value="100" oninput="brightTouched=true;ledBrightValue.textContent=this.value+'%'" onchange="brightTouched=false;act('led_brightness&percent='+this.value)"><span id="ledBrightValue">-</span></div></div>
<div class="card wide"><h2>Fan thresholds (&deg;C)</h2><div class="note">Can only be lowered below the firmware's defaults. Kept until the Pico restarts.</div>
<div id="thr"></div>
<div class="row"><button onclick="act('thresholds_default').then(loadThresholds)">Defaults</button></div></div>
</div></main>
<script>
const AV=["Advanced SCART","High Definition AV Pack","VGA","RF adapter","Advanced AV (S-Video)","Unknown (5)","Standard AV","No cable"];
const TRAY=["Closed","Open","Unloading","Opening","No disc","Closing","Disc detected","Drive off"];
const $=id=>document.getElementById(id);
let fanTouched=false; // The slider follows the fan speed until it's moved
let brightTouched=false; // Same for the LED brightness, until it's released
async function act(cmd){
  if(cmd=="reset"&&!confirm("Reset the Xbox?"))return;
  try{const r=await fetch("/action?cmd="+cmd,{method:"POST"});if(!r.ok)alert(await r.text());}catch(e){alert("Not connected");}
  tick();
}
function setLed(){const [r,g]=$("ledPattern").value.split(",");act("led&red="+r+"&green="+g);}
async function tick(){
  try{
    const s=await(await fetch("/status.json")).json();
    $("conn").textContent="Connected, up "+Math.floor(s.uptime/60)+" min";
    $("power").textContent=s.powered?"On":"Off";
    $("extra").innerHTML=(s.overheated?'<span class="bad">Overheated</span> ':'')+"Interrupt reasons 0x"+s.irq.toString(16).padStart(2,"0");
    $("cpu").textContent=s.powered?s.cpu+" \u00b0C":"-";
    $("board").textContent=s.powered?s.board+" \u00b0C":"-";
    $("pico").textContent=(s.pico/10).toFixed(1)+" \u00b0C";
    $("ntc").textContent=s.ntc===null?(s.ntcEnabled?"Not connected":"Off"):(s.ntc/10).toFixed(1)+" \u00b0C";
    $("tray").textContent=s.dvd?(s.powered?TRAY[s.tray]:"-"):"No drive";
    $("trayNote").textContent=s.dvd&&s.powered?(s.trayClosed?"Closed":"Open"):"";
    $("av").textContent=!s.powered?"-":s.avMissing?"No cable":AV[s.av];
    $("fan").textContent=s.fan+" / 50";
    if(!fanTouched&&s.fan>=10){$("fanSpeed").value=s.fan;$("fanValue").textContent=s.fan;}
    $("fanNote").innerHTML=s.fanOverride?'<span class="warn">Manual</span>':"Automatic";
    $("led").innerHTML=s.ledOverride?'<span class="warn">Manual</span> (red '+s.ledRed+', green '+s.ledGreen+')':"Automatic";
    if(!brightTouched){$("ledBright").value=s.ledBrightness;$("ledBrightValue").textContent=s.ledBrightness+"%";}
  }catch(e){$("conn").textContent="Disconnected";}
}
async function loadThresholds(){
  try{
    const t=await(await fetch("/thresholds.json")).json();
    $("thr").innerHTML=t.items.map((x,i)=>'<div class="row"><span class="thr">'+x.label+'</span>'+
      '<input type="number" id="thr'+i+'" min="'+t.min+'" max="'+x.def+'" value="'+x.value+'">'+
      '<span class="note'+(x.value<x.def?' warn':'')+'">default '+x.def+'</span>'+
      '<button onclick="setThreshold('+i+')">Set</button></div>').join("");
  }catch(e){}
}
async function setThreshold(i){
  const el=$("thr"+i),v=Number(el.value);
  if(!(v>=Number(el.min)&&v<=Number(el.max))){alert("Must be "+el.min+"-"+el.max);return;}
  await act("threshold&index="+i+"&value="+v);loadThresholds();
}
setInterval(tick,1000);tick();loadThresholds();
</script></body></html>
)HTML";

int statusJson(char* out, size_t size) {
  WebStatus s;
  critical_section_enter_blocking(&status_lock);
  s = status;
  critical_section_exit(&status_lock);

  char ntc[8] = "null";
  if (s.ntc_connected) {
    snprintf(ntc, sizeof(ntc), "%d", s.ntc_temperature);
  }

  return snprintf(out, size,
                  "{\"uptime\":%lu,\"powered\":%s,\"cpu\":%u,\"board\":%u,\"pico\":%d,\"ntc\":%s,\"ntcEnabled\":%s,"
                  "\"fan\":%u,\"fanOverride\":%s,"
                  "\"dvd\":%s,\"tray\":%u,\"trayClosed\":%s,\"av\":%u,\"avMissing\":%s,\"overheated\":%s,"
                  "\"ledOverride\":%s,\"ledRed\":%u,\"ledGreen\":%u,\"ledBrightness\":%u,\"irq\":%u}",
                  (unsigned long)s.uptime_s, s.powered ? "true" : "false", s.cpu_temperature,
                  s.board_temperature, s.pico_temperature, ntc, SMC_NTC_SENSOR ? "true" : "false",
                  s.fan_speed, s.fan_override ? "true" : "false",
                  SMC_DVD_SUPPORT ? "true" : "false", s.tray_state, s.tray_closed ? "true" : "false", s.av_pack,
                  s.av_missing ? "true" : "false", s.overheated ? "true" : "false",
                  s.led_override ? "true" : "false", s.led_red, s.led_green, s.led_brightness,
                  s.interrupt_reason);
}

// Labels and defaults are constant, the values come from the core 0 snapshot
int thresholdsJson(char* out, size_t size) {
  uint8_t values[Fan::max_thresholds];
  critical_section_enter_blocking(&status_lock);
  memcpy(values, status.thresholds, sizeof(values));
  critical_section_exit(&status_lock);

  int len = snprintf(out, size, "{\"min\":%u,\"items\":[", Fan::min_threshold);
  const uint8_t count = Fan::thresholdCount();
  for (uint8_t i = 0; i < count && len < (int)size; i++) {
    len += snprintf(out + len, size - len, "%s{\"label\":\"%s\",\"def\":%u,\"value\":%u}", i != 0 ? "," : "",
                    Fan::thresholdLabel(i), Fan::thresholdDefault(i), values[i]);
  }
  if (len < (int)size) {
    len += snprintf(out + len, size - len, "]}");
  }
  return len < (int)size ? len : (int)size - 1;
}

// Value of key in "a=1&b=2" (the query ends at a space or the end), or -1
long queryNumber(const char* query, const char* key) {
  const size_t key_len = strlen(key);
  const char* p = query;
  while (p != nullptr && *p != '\0' && *p != ' ') {
    if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
      char* end;
      const long value = strtol(p + key_len + 1, &end, 10);
      return end == p + key_len + 1 ? -1 : value;
    }
    p = strchr(p, '&');
    if (p != nullptr) {
      p++;
    }
  }
  return -1;
}

bool queryIs(const char* query, const char* key, const char* value) {
  const size_t key_len = strlen(key);
  const size_t value_len = strlen(value);
  const char* p = query;
  while (p != nullptr && *p != '\0' && *p != ' ') {
    if (strncmp(p, key, key_len) == 0 && p[key_len] == '=' && strncmp(p + key_len + 1, value, value_len) == 0) {
      const char next = p[key_len + 1 + value_len];
      return next == '&' || next == ' ' || next == '\0';
    }
    p = strchr(p, '&');
    if (p != nullptr) {
      p++;
    }
  }
  return false;
}

// Parses /action?cmd=..., returns an error text or nullptr when queued
const char* queueCommand(const char* query) {
  CommandMessage message{Command::power, 0};
  if (queryIs(query, "cmd", "power")) {
    message.command = Command::power;
  } else if (queryIs(query, "cmd", "eject")) {
    message.command = Command::eject;
  } else if (queryIs(query, "cmd", "reset")) {
    message.command = Command::reset;
  } else if (queryIs(query, "cmd", "fan")) {
    const long speed = queryNumber(query, "speed");
    if (speed < hal::FAN_MIN_SPEED || speed > hal::FAN_MAX_SPEED) {
      return "speed must be 10-50";
    }
    message = {Command::fan_speed, (uint8_t)speed};
  } else if (queryIs(query, "cmd", "fan_auto")) {
    message.command = Command::fan_auto;
  } else if (queryIs(query, "cmd", "led")) {
    const long red = queryNumber(query, "red");
    const long green = queryNumber(query, "green");
    if (red < 0 || red > 15 || green < 0 || green > 15) {
      return "red and green must be 0-15";
    }
    message = {Command::led_pattern, (uint8_t)((red << 4) | green)};
  } else if (queryIs(query, "cmd", "led_auto")) {
    message.command = Command::led_auto;
  } else if (queryIs(query, "cmd", "threshold")) {
    const long index = queryNumber(query, "index");
    const long value = queryNumber(query, "value");
    if (index < 0 || index >= Fan::thresholdCount()) {
      return "unknown threshold";
    }
    // Never above the firmware's default
    if (value < Fan::min_threshold || value > Fan::thresholdDefault(index)) {
      static char error[48];
      snprintf(error, sizeof(error), "value must be %u-%u", Fan::min_threshold, Fan::thresholdDefault(index));
      return error;
    }
    message = {Command::threshold, (uint8_t)value, (uint8_t)index};
  } else if (queryIs(query, "cmd", "thresholds_default")) {
    message.command = Command::thresholds_default;
  } else if (queryIs(query, "cmd", "led_brightness")) {
    const long percent = queryNumber(query, "percent");
    if (percent < 1 || percent > 100) {
      return "percent must be 1-100";
    }
    message = {Command::led_brightness, (uint8_t)percent};
  } else {
    return "unknown command";
  }
  return queue_try_add(&commands, &message) ? nullptr : "busy, try again";
}

void respond(tcp_pcb* pcb, const char* status_line, const char* type, const char* body, size_t len,
             bool body_is_static) {
  char header[192];
  const int header_len =
      snprintf(header, sizeof(header),
               "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nCache-Control: no-store\r\n"
               "Connection: close\r\n\r\n",
               status_line, type, (unsigned)len);
  tcp_write(pcb, header, header_len, TCP_WRITE_FLAG_COPY | (len != 0 ? TCP_WRITE_FLAG_MORE : 0));
  if (len != 0) {
    tcp_write(pcb, body, len, body_is_static ? 0 : TCP_WRITE_FLAG_COPY);
  }
  tcp_output(pcb);
}

void handleRequest(tcp_pcb* pcb, char* request) {
  if (strncmp(request, "GET / ", 6) == 0 || strncmp(request, "GET /index.html ", 16) == 0) {
    respond(pcb, "200 OK", "text/html; charset=utf-8", page, sizeof(page) - 1, true);
  } else if (strncmp(request, "GET /status.json ", 17) == 0) {
    char json[512];
    const int len = statusJson(json, sizeof(json));
    respond(pcb, "200 OK", "application/json", json, len, false);
  } else if (strncmp(request, "GET /thresholds.json ", 21) == 0) {
    char json[900];
    const int len = thresholdsJson(json, sizeof(json));
    respond(pcb, "200 OK", "application/json", json, len, false);
  } else if (strncmp(request, "POST /action?", 13) == 0) {
    const char* error = queueCommand(request + 13);
    if (error == nullptr) {
      respond(pcb, "200 OK", "text/plain", "ok", 2, true);
    } else {
      respond(pcb, "400 Bad Request", "text/plain", error, strlen(error), false);
    }
  } else {
    respond(pcb, "404 Not Found", "text/plain", "not found", 9, true);
  }
}

struct Connection {
  tcp_pcb* pcb;
  char request[max_request + 1];
  size_t len;
  bool in_use;
};

Connection connections[max_connections];

void closeConnection(Connection* connection) {
  tcp_pcb* pcb = connection->pcb;
  connection->in_use = false;
  tcp_arg(pcb, nullptr);
  tcp_recv(pcb, nullptr);
  tcp_err(pcb, nullptr);
  // Queued data still goes out before the FIN
  if (tcp_close(pcb) != ERR_OK) {
    tcp_abort(pcb);
  }
}

err_t onReceive(void* arg, tcp_pcb* pcb, pbuf* p, err_t err) {
  Connection* connection = static_cast<Connection*>(arg);
  if (p == nullptr || err != ERR_OK) {
    if (p != nullptr) {
      pbuf_free(p);
    }
    closeConnection(connection);
    return ERR_OK;
  }

  const size_t room = max_request - connection->len;
  const size_t copied = pbuf_copy_partial(p, connection->request + connection->len, room, 0);
  connection->len += copied;
  connection->request[connection->len] = '\0';
  tcp_recved(pcb, p->tot_len);
  pbuf_free(p);

  // Only the request line and headers matter
  if (strstr(connection->request, "\r\n\r\n") != nullptr || connection->len == max_request) {
    handleRequest(pcb, connection->request);
    closeConnection(connection);
  }
  return ERR_OK;
}

void onError(void* arg, err_t) {
  // lwIP already freed the pcb
  if (arg != nullptr) {
    static_cast<Connection*>(arg)->in_use = false;
  }
}

err_t onAccept(void*, tcp_pcb* pcb, err_t err) {
  if (err != ERR_OK || pcb == nullptr) {
    return ERR_VAL;
  }
  for (Connection& connection : connections) {
    if (!connection.in_use) {
      connection = {pcb, {}, 0, true};
      tcp_arg(pcb, &connection);
      tcp_recv(pcb, onReceive);
      tcp_err(pcb, onError);
      return ERR_OK;
    }
  }
  tcp_abort(pcb); // All busy
  return ERR_ABRT;
}

bool startHttpServer() {
  tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (pcb == nullptr || tcp_bind(pcb, IP_ANY_TYPE, http_port) != ERR_OK) {
    return false;
  }
  tcp_pcb* listener = tcp_listen_with_backlog(pcb, max_connections);
  if (listener == nullptr) {
    tcp_close(pcb);
    return false;
  }
  tcp_accept(listener, onAccept);
  return true;
}

// SMC_WIFI_PASSWORD, or when that's empty one made from this board's unique ID:
// different for every Pico and shown in the boot log. The SDK reads the ID at
// boot, so this is safe from core 1.
bool wifiPassword(char* out, size_t size) {
  if (SMC_WIFI_PASSWORD[0] != '\0') {
    snprintf(out, size, "%s", SMC_WIFI_PASSWORD);
    return false;
  }
  char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
  pico_get_unique_board_id_string(id, sizeof(id));
  snprintf(out, size, "ogsmc-%s", id);
  return true;
}

void core1Main() {
  if (cyw43_arch_init() != 0) {
    log("WEB: Wi-Fi init failed");
    return;
  }
  char password[64];
  wifiPassword(password, sizeof(password));
  cyw43_arch_enable_ap_mode(SMC_WIFI_SSID, password, CYW43_AUTH_WPA2_AES_PSK);

  netif* ap = &cyw43_state.netif[CYW43_ITF_AP];
  if (!dhcp_server::start(ap) || !startHttpServer()) {
    log("WEB: Server start failed");
    return;
  }
  log("WEB: Access point up, http://%s", ip4addr_ntoa(netif_ip4_addr(ap)));
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); // Onboard LED: access point up

  while (true) {
    cyw43_arch_poll();
    cyw43_arch_wait_for_work_until(make_timeout_time_ms(10));
  }
}

// ---- Core 0 -----------------------------------------------------------------

void runCommand(const CommandMessage& message) {
  switch (message.command) {
  case Command::power:
    debug::print_message("WEB: Power button");
    SMI::flags.power_change_requested = true;
    break;

  case Command::eject:
    debug::print_message("WEB: Eject button");
    Dvd::flags.eject_change_requested = true;
    Dvd::flags.eject_button_event = true;
    break;

  case Command::reset:
    if (hal::get_power_ok()) {
      debug::print_message("WEB: Reset");
      PLL_Reset::flags.warm_reset_request = true;
      SMI::flags.os_reset_request = true;
    }
    break;

  case Command::fan_speed:
    debug::print_message("WEB: Fan speed " + std::to_string(message.arg));
    Fan::setCustomSpeed(message.arg);
    Fan::flags.fan_override = true;
    break;

  case Command::fan_auto:
    debug::print_message("WEB: Fan automatic");
    Fan::flags.fan_override = false;
    break;

  case Command::led_pattern:
    debug::print_message("WEB: LED pattern red " + std::to_string(message.arg >> 4) + " green " +
                         std::to_string(message.arg & 0x0F));
    Led::setRedPhases(message.arg >> 4);
    Led::setGreenPhases(message.arg & 0x0F);
    Led::flags.led_override = true;
    break;

  case Command::led_auto:
    debug::print_message("WEB: LEDs automatic");
    Led::flags.led_override = false;
    break;

  case Command::threshold:
    if (Fan::setThreshold(message.index, message.arg)) {
      debug::print_message(std::string("WEB: Fan threshold \"") + Fan::thresholdLabel(message.index) + "\" " +
                           std::to_string(message.arg) + " (default " +
                           std::to_string(Fan::thresholdDefault(message.index)) + ")");
    }
    break;

  case Command::thresholds_default:
    debug::print_message("WEB: Fan thresholds back to the defaults");
    Fan::resetThresholds();
    break;

  case Command::led_brightness:
    debug::print_message("WEB: LED brightness " + std::to_string(message.arg) + "%");
    hal::set_led_brightness(message.arg);
    break;
  }
}

void publishStatus() {
  WebStatus s;
  s.uptime_s = time_us_64() / 1000000;
  s.powered = hal::get_power_ok();
  s.cpu_temperature = Fan::sensors.CPU_temperature;
  s.board_temperature = Fan::sensors.board_temperature;
  s.pico_temperature = ExtraTemp::sensors.pico_temperature;
  s.ntc_temperature = ExtraTemp::sensors.ntc_temperature;
  s.ntc_connected = ExtraTemp::sensors.ntc_connected;
  s.fan_speed = Fan::getFanSpeed();
  s.fan_override = Fan::flags.fan_override;
  s.tray_state = (static_cast<uint8_t>(Dvd::sensors.tray_status) >> 4) & 0x07;
  s.tray_closed = Dvd::isTrayClosing();
  s.av_pack = static_cast<uint8_t>(Video::sensors.vmode) >> 1;
  s.av_missing = Video::flags.av_cable_missing;
  s.overheated = Fan::flags.overheated;
  s.led_override = Led::flags.led_override;
  s.led_red = Led::getRedPhases();
  s.led_green = Led::getGreenPhases();
  s.led_brightness = hal::get_led_brightness();
  s.interrupt_reason = SMI::flags.interrupt_reason;
  for (uint8_t i = 0; i < Fan::max_thresholds; i++) {
    s.thresholds[i] = Fan::getThreshold(i);
  }

  critical_section_enter_blocking(&status_lock);
  status = s;
  critical_section_exit(&status_lock);
}

} // namespace

void start() {
  critical_section_init(&status_lock);
  queue_init(&commands, sizeof(CommandMessage), 8);
  queue_init(&log_messages, sizeof(LogMessage), 8);
  publishStatus();
  // Printed here, before the Wi-Fi is started, so it shows even if that fails
  char password[64];
  if (wifiPassword(password, sizeof(password))) {
    debug::print_message(std::string("WEB: Access point \"") + SMC_WIFI_SSID + "\", password " + password);
  } else {
    debug::print_message(std::string("WEB: Access point \"") + SMC_WIFI_SSID + "\", password from SMC_WIFI_PASSWORD");
  }
  debug::print_message("WEB: Starting core 1");
  multicore_launch_core1_with_stack(core1Main, core1_stack, sizeof(core1_stack));
}

void service() {
  CommandMessage command;
  while (queue_try_remove(&commands, &command)) {
    runCommand(command);
  }

  publishStatus();

  LogMessage message;
  while (queue_try_remove(&log_messages, &message)) {
    debug::print_message(message.text);
  }
}

} // namespace Web
} // namespace SMC

