#ifndef __DHCP_SERVER__
#define __DHCP_SERVER__

struct netif;

namespace dhcp_server {

// Minimal DHCP server for the web interface's access point: hands out
// x.x.x.16-23 in the access point's subnet. Call from the lwIP core (core 1).
bool start(netif* nif);

} // namespace dhcp_server

#endif // __DHCP_SERVER__
