#ifndef DHCP_SERVER_HPP
#define DHCP_SERVER_HPP

struct netif;

namespace dhcp_server {

// Minimal DHCP server for the web interface's access point: hands out
// x.x.x.16-23 in the access point's subnet. Call from the lwIP core (core 1).
bool start(netif* nif);

} // namespace dhcp_server

#endif // DHCP_SERVER_HPP
