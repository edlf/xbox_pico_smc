#include "dhcp_server.hpp"

#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include <cstring>

namespace dhcp_server {
namespace {

constexpr uint16_t server_port = 67;
constexpr uint16_t client_port = 68;

// Message types (option 53)
constexpr uint8_t DISCOVER = 1;
constexpr uint8_t OFFER = 2;
constexpr uint8_t REQUEST = 3;
constexpr uint8_t ACK = 5;
constexpr uint8_t NAK = 6;

// Options
constexpr uint8_t OPT_PAD = 0;
constexpr uint8_t OPT_SUBNET_MASK = 1;
constexpr uint8_t OPT_ROUTER = 3;
constexpr uint8_t OPT_DNS = 6;
constexpr uint8_t OPT_REQUESTED_IP = 50;
constexpr uint8_t OPT_LEASE_TIME = 51;
constexpr uint8_t OPT_MSG_TYPE = 53;
constexpr uint8_t OPT_SERVER_ID = 54;
constexpr uint8_t OPT_END = 255;

// BOOTP layout
constexpr size_t OFS_OP = 0;
constexpr size_t OFS_CIADDR = 12;
constexpr size_t OFS_YIADDR = 16;
constexpr size_t OFS_SIADDR = 20;
constexpr size_t OFS_CHADDR = 28;
constexpr size_t OFS_SNAME = 44;
constexpr size_t SNAME_FILE_LEN = 64 + 128;
constexpr size_t OFS_MAGIC = 236;
constexpr size_t OFS_OPTIONS = 240;
constexpr uint8_t magic[4] = {0x63, 0x82, 0x53, 0x63};

constexpr uint32_t lease_time_s = 24 * 60 * 60;
constexpr uint8_t first_host = 16;
constexpr uint8_t max_leases = 8;

struct Lease {
  uint8_t mac[6];
  bool used;
};

Lease leases[max_leases];
udp_pcb* pcb = nullptr;
netif* server_netif = nullptr;

const uint8_t* findOption(const uint8_t* options, size_t len, uint8_t code) {
  size_t i = 0;
  while (i < len && options[i] != OPT_END) {
    if (options[i] == OPT_PAD) {
      i++;
      continue;
    }
    if (i + 1 >= len || i + 2 + options[i + 1] > len) {
      break;
    }
    if (options[i] == code) {
      return &options[i];
    }
    i += 2 + options[i + 1];
  }
  return nullptr;
}

// The client's lease, or a free one; -1 when all are taken
int leaseFor(const uint8_t* mac) {
  int free_lease = -1;
  for (int i = 0; i < max_leases; i++) {
    if (leases[i].used && memcmp(leases[i].mac, mac, 6) == 0) {
      return i;
    }
    if (!leases[i].used && free_lease < 0) {
      free_lease = i;
    }
  }
  return free_lease;
}

uint8_t* putOption(uint8_t* out, uint8_t code, const void* data, uint8_t len) {
  out[0] = code;
  out[1] = len;
  memcpy(out + 2, data, len);
  return out + 2 + len;
}

void receive(void*, udp_pcb*, pbuf* p, const ip_addr_t*, u16_t) {
  uint8_t msg[548];
  const size_t len = pbuf_copy_partial(p, msg, sizeof(msg), 0);
  pbuf_free(p);

  if (len < OFS_OPTIONS || msg[OFS_OP] != 1 || memcmp(&msg[OFS_MAGIC], magic, 4) != 0) {
    return; // Not a BOOTREQUEST
  }
  const uint8_t* type = findOption(&msg[OFS_OPTIONS], len - OFS_OPTIONS, OPT_MSG_TYPE);
  if (type == nullptr || type[1] != 1) {
    return;
  }

  const int lease = leaseFor(&msg[OFS_CHADDR]);
  if (lease < 0) {
    return; // No free address
  }
  ip4_addr_t offered;
  const ip4_addr_t* server_ip = netif_ip4_addr(server_netif);
  IP4_ADDR(&offered, ip4_addr1(server_ip), ip4_addr2(server_ip), ip4_addr3(server_ip), first_host + lease);

  uint8_t reply_type;
  if (type[2] == DISCOVER) {
    reply_type = OFFER;
  } else if (type[2] == REQUEST) {
    // Requested address from option 50, or ciaddr when renewing
    uint32_t requested = 0;
    const uint8_t* option = findOption(&msg[OFS_OPTIONS], len - OFS_OPTIONS, OPT_REQUESTED_IP);
    if (option != nullptr && option[1] == 4) {
      memcpy(&requested, &option[2], 4);
    } else {
      memcpy(&requested, &msg[OFS_CIADDR], 4);
    }
    if (requested == ip4_addr_get_u32(&offered)) {
      reply_type = ACK;
      memcpy(leases[lease].mac, &msg[OFS_CHADDR], 6);
      leases[lease].used = true;
    } else {
      reply_type = NAK;
    }
  } else {
    return;
  }

  // Reply in place: keep xid, flags and chaddr
  msg[OFS_OP] = 2; // BOOTREPLY
  const uint32_t yiaddr = reply_type == NAK ? 0 : ip4_addr_get_u32(&offered);
  memcpy(&msg[OFS_YIADDR], &yiaddr, 4);
  memcpy(&msg[OFS_SIADDR], server_ip, 4);
  memset(&msg[OFS_SNAME], 0, SNAME_FILE_LEN);

  uint8_t* out = &msg[OFS_OPTIONS];
  out = putOption(out, OPT_MSG_TYPE, &reply_type, 1);
  out = putOption(out, OPT_SERVER_ID, server_ip, 4);
  if (reply_type != NAK) {
    const uint8_t lease_time[4] = {(uint8_t)(lease_time_s >> 24), (uint8_t)(lease_time_s >> 16),
                                   (uint8_t)(lease_time_s >> 8), (uint8_t)lease_time_s};
    out = putOption(out, OPT_LEASE_TIME, lease_time, 4);
    out = putOption(out, OPT_SUBNET_MASK, netif_ip4_netmask(server_netif), 4);
    out = putOption(out, OPT_ROUTER, server_ip, 4);
    out = putOption(out, OPT_DNS, server_ip, 4);
  }
  *out++ = OPT_END;

  const u16_t reply_len = out - msg;
  pbuf* reply = pbuf_alloc(PBUF_TRANSPORT, reply_len, PBUF_RAM);
  if (reply == nullptr) {
    return;
  }
  memcpy(reply->payload, msg, reply_len);
  udp_sendto_if(pcb, reply, IP_ADDR_BROADCAST, client_port, server_netif);
  pbuf_free(reply);
}

} // namespace

bool start(netif* nif) {
  server_netif = nif;
  pcb = udp_new();
  if (pcb == nullptr) {
    return false;
  }
  if (udp_bind(pcb, IP_ANY_TYPE, server_port) != ERR_OK) {
    udp_remove(pcb);
    pcb = nullptr;
    return false;
  }
  udp_recv(pcb, receive, nullptr);
  return true;
}

} // namespace dhcp_server
