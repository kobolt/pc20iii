#include "net.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "edfs.h"
#include "panic.h"

#define NET_TRACE_BUFFER_SIZE 256
#define NET_TRACE_MAX 80

#define FLAGS_SYN 0x02
#define FLAGS_RST 0x04
#define FLAGS_ACK 0x10
#define FLAGS_FIN_ACK 0x11
#define FLAGS_SYN_ACK 0x12
#define FLAGS_RST_ACK 0x14
#define FLAGS_PSH_ACK 0x18

static char net_trace_buffer[NET_TRACE_BUFFER_SIZE][NET_TRACE_MAX];
static int net_trace_buffer_n = 0;



static void net_trace(const char *format, ...)
{
  va_list args;

  va_start(args, format);
  vsnprintf(net_trace_buffer[net_trace_buffer_n],
    NET_TRACE_MAX, format, args);
  va_end(args);

  net_trace_buffer_n++;
  if (net_trace_buffer_n >= NET_TRACE_BUFFER_SIZE) {
    net_trace_buffer_n = 0;
  }
}



static char *net_trace_ip(uint32_t ip)
{
  static char s[16];
  uint32_t net_ip = htonl(ip);
  inet_ntop(AF_INET, &net_ip, s, 16);
  return s;
}



static uint16_t net_ip_checksum(uint8_t buffer[], size_t len)
{
  size_t i;
  int32_t add = -1;
  uint16_t checksum;

  for (i = 0; i < len; i += 2) {
    add += buffer[i] << 8;
    if (i + 1 < len) {
      add += buffer[i + 1];
    }
  }

  while (add >> 16) {
    add = (add & 0xFFFF) + (add >> 16);
  }
  checksum = ~add;

  return checksum;
}



static uint16_t net_proto_checksum(uint8_t buffer[], size_t len,
  uint32_t src_ip, uint32_t dst_ip, uint8_t proto)
{
  size_t i;
  uint32_t add = 0;
  uint16_t checksum;

  /* Pseudo Header */
  add += src_ip >> 16;
  add += src_ip & 0xFFFF;
  add += dst_ip >> 16;
  add += dst_ip & 0xFFFF;
  add += proto;
  add += len;

  /* Protocol Header + Payload */
  for (i = 0; i < len; i += 2) {
    add += buffer[i] << 8;
    if (i + 1 < len) {
      add += buffer[i + 1];
    }
  }

  while (add >> 16) {
    add = (add & 0xFFFF) + (add >> 16);
  }
  checksum = ~add;

  if (proto == IPPROTO_UDP && checksum == 0x0000) {
    return 0xFFFF; /* UDP special case since 0x0000 is not a valid checksum. */
  } else {
    return checksum;
  }
}



static void net_ethernet_reply(net_t *net)
{
  net->rx_frame[0x00] = NET_MAC_LOCAL; /* Destination MAC */
  net->rx_frame[0x01] = NET_MAC_LOCAL;
  net->rx_frame[0x02] = NET_MAC_LOCAL;
  net->rx_frame[0x03] = NET_MAC_LOCAL;
  net->rx_frame[0x04] = NET_MAC_LOCAL;
  net->rx_frame[0x05] = NET_MAC_LOCAL;

  net->rx_frame[0x06] = NET_MAC_REMOTE; /* Source MAC */
  net->rx_frame[0x07] = NET_MAC_REMOTE;
  net->rx_frame[0x08] = NET_MAC_REMOTE;
  net->rx_frame[0x09] = NET_MAC_REMOTE;
  net->rx_frame[0x0A] = NET_MAC_REMOTE;
  net->rx_frame[0x0B] = NET_MAC_REMOTE;
}



static void net_ipv4_reply(net_t *net, uint16_t ip_len,
  uint8_t proto, uint32_t src_ip)
{
  uint16_t checksum;

  net->rx_frame[0x0C] = 0x08;  /* / Ethertype = IP  */
  net->rx_frame[0x0D] = 0x00;  /* \                 */
  net->rx_frame[0x0E] = 0x45;  /* - Version + IHL   */
  net->rx_frame[0x0F] = 0x00;  /* - TOS             */
  net->rx_frame[0x10] = (ip_len >> 8); /* / Total   */
  net->rx_frame[0x11] = ip_len & 0xFF; /* \ Length  */
  net->rx_frame[0x12] = (net->ip_id >> 8); /* / ID  */
  net->rx_frame[0x13] = net->ip_id & 0xFF; /* \     */
  net->rx_frame[0x14] = 0x00;  /* / Flags +         */
  net->rx_frame[0x15] = 0x00;  /* \ Fragment Offset */
  net->rx_frame[0x16] = 0x40;  /* - TTL             */
  net->rx_frame[0x17] = proto; /* - Protocol        */
  net->rx_frame[0x18] = 0x00;  /* / Header Checksum */
  net->rx_frame[0x19] = 0x01;  /* \                 */

  net->rx_frame[0x1A] = src_ip >> 24; /* Source Address */
  net->rx_frame[0x1B] = src_ip >> 16;
  net->rx_frame[0x1C] = src_ip >> 8;
  net->rx_frame[0x1D] = src_ip;

  net->rx_frame[0x1E] = (NET_IP_LOCAL >> 24) & 0xFF; /* Destination Address */
  net->rx_frame[0x1F] = (NET_IP_LOCAL >> 16) & 0xFF;
  net->rx_frame[0x20] = (NET_IP_LOCAL >> 8)  & 0xFF;
  net->rx_frame[0x21] =  NET_IP_LOCAL        & 0xFF;

  checksum = net_ip_checksum(&net->rx_frame[0x0E], 20);
  net->rx_frame[0x18] = checksum >> 8;
  net->rx_frame[0x19] = checksum & 0xFF;

  net->ip_id++;
}



static void net_udp_reply(net_t *net, ssize_t recv_bytes, uint32_t src_ip,
  uint16_t src_port, uint16_t dst_port)
{
  uint16_t checksum;

  net->rx_frame[0x22] = src_port >> 8;           /* / Source       */
  net->rx_frame[0x23] = src_port & 0xFF;         /* \ Port         */
  net->rx_frame[0x24] = dst_port >> 8;           /* / Destination  */
  net->rx_frame[0x25] = dst_port & 0xFF;         /* \ Port         */
  net->rx_frame[0x26] = (8 + recv_bytes) >> 8;   /* / UDP Length   */
  net->rx_frame[0x27] = (8 + recv_bytes) & 0xFF; /* \              */
  net->rx_frame[0x28] = 0x00;                    /* / UDP Checksum */
  net->rx_frame[0x29] = 0x00;                    /* \              */

  checksum = net_proto_checksum(&net->rx_frame[0x22], 8 + recv_bytes,
    src_ip, NET_IP_LOCAL, IPPROTO_UDP);
  net->rx_frame[0x28] = checksum >> 8;
  net->rx_frame[0x29] = checksum & 0xFF;
}



static void net_tcp_reply(net_t *net, size_t len,
  int socket_index, uint8_t flags)
{
  uint16_t checksum;
  uint16_t dst_port;
  uint16_t src_port;
  uint32_t send_ack;
  uint32_t send_seq;
  uint32_t src_ip;

  net_trace("TCP [%d] rx: flags = %02x\n", socket_index, flags);

  src_ip   = net->tcp_sockets[socket_index].dst_ip;
  src_port = net->tcp_sockets[socket_index].dst_port;
  dst_port = net->tcp_sockets[socket_index].src_port;
  send_ack = net->tcp_sockets[socket_index].recv_seq;
  send_seq = net->tcp_sockets[socket_index].send_seq;

  net->rx_frame[0x22] = src_port >> 8;   /* / Source       */
  net->rx_frame[0x23] = src_port & 0xFF; /* \ Port         */
  net->rx_frame[0x24] = dst_port >> 8;   /* / Destination  */
  net->rx_frame[0x25] = dst_port & 0xFF; /* \ Port         */

  net->rx_frame[0x26] = send_seq >> 24;  /* / Sequence     */
  net->rx_frame[0x27] = send_seq >> 16;  /* | Number       */
  net->rx_frame[0x28] = send_seq >> 8;   /* |              */
  net->rx_frame[0x29] = send_seq & 0xFF; /* \              */

  net->rx_frame[0x2A] = send_ack >> 24;  /* / Acknowledgement */
  net->rx_frame[0x2B] = send_ack >> 16;  /* | Number          */
  net->rx_frame[0x2C] = send_ack >> 8;   /* |                 */
  net->rx_frame[0x2D] = send_ack & 0xFF; /* \                 */

  net->rx_frame[0x2E] = 0x50;   /* - Data Offset       */
  net->rx_frame[0x2F] = flags;  /* - Flags (PSH + ACK) */
  net->rx_frame[0x30] = 0xFF;   /* / Window Size       */
  net->rx_frame[0x31] = 0x00;   /* \                   */
  net->rx_frame[0x32] = 0x00;   /* / TCP Checksum      */
  net->rx_frame[0x33] = 0x00;   /* \                   */
  net->rx_frame[0x34] = 0x00;   /* / Urgent Pointer    */
  net->rx_frame[0x35] = 0x00;   /* \                   */

  checksum = net_proto_checksum(&net->rx_frame[0x22], len,
    src_ip, NET_IP_LOCAL, IPPROTO_TCP);
  net->rx_frame[0x32] = checksum >> 8;
  net->rx_frame[0x33] = checksum & 0xFF;
}



static void net_handle_icmp(net_t *net, uint8_t tx_frame[], uint16_t tx_len)
{
  int i;
  uint8_t type;
  uint16_t checksum;
  uint32_t dst_ip;

  type = tx_frame[0x22];

  if (type != 8) {
    return; /* Only handle ICMP echo requests. */
  }

  dst_ip  = tx_frame[0x1E] << 24;
  dst_ip += tx_frame[0x1F] << 16;
  dst_ip += tx_frame[0x20] << 8;
  dst_ip += tx_frame[0x21];

  if (dst_ip != NET_IP_REMOTE) {
    return; /* Ignore other addresses. */
  }

  net->rx_frame[0x22] = 0x00; /* - Type = Echo Reply */
  net->rx_frame[0x23] = 0x00; /* - Code              */
  net->rx_frame[0x24] = 0x00; /* / ICMP Checksum     */
  net->rx_frame[0x25] = 0x01; /* \                   */

  /* Reply with same identifier, sequence number and payload. */
  for (i = 0x26; i < tx_len; i++) {
    net->rx_frame[i] = tx_frame[i];
  }

  checksum = net_ip_checksum(&net->rx_frame[0x22], tx_len - 0x22);
  net->rx_frame[0x24] = checksum >> 8;
  net->rx_frame[0x25] = checksum & 0xFF;

  net_ipv4_reply(net, tx_len - 14, IPPROTO_ICMP, NET_IP_REMOTE);
  net_ethernet_reply(net);
  net->rx_len = tx_len;
  net->rx_ready = true;
}



static void net_udp_close(net_t *net, int socket_index)
{
  close(net->udp_sockets[socket_index].fd);
  net->udp_sockets[socket_index].fd = -1;
  net_trace("UDP [%d] close\n", socket_index);
}



static void net_tcp_close(net_t *net, int socket_index, uint8_t flags)
{
  if (flags > 0) {
    /* Send a packet while taking down the connection. */
    net_tcp_reply(net, 20, socket_index, flags);
    net_ipv4_reply(net, 20 + 20, IPPROTO_TCP,
      net->tcp_sockets[socket_index].dst_ip);
    net_ethernet_reply(net);
    net->rx_len = 14 + 20 + 20;
    net->rx_ready = true;
  }

  close(net->tcp_sockets[socket_index].fd);
  net->tcp_sockets[socket_index].fd = -1;
  net->tcp_sockets[socket_index].send_seq = (socket_index * 0x1000000);
  net_trace("TCP [%d] close\n", socket_index);
}



static void net_handle_tcp(net_t *net, uint8_t tx_frame[])
{
  int i;
  int fcntl_flags;
  int socket_index;
  size_t data_len;
  ssize_t send_bytes;
  struct sockaddr_in sa;
  uint8_t data_offset;
  uint8_t flags;
  uint16_t data_index;
  uint16_t dst_port;
  uint16_t ip_len;
  uint16_t src_port;
  uint16_t win_size;
  uint32_t dst_ip;
  uint32_t recv_seq;

  ip_len  = tx_frame[0x10] << 8;
  ip_len += tx_frame[0x11];

  dst_ip  = tx_frame[0x1E] << 24;
  dst_ip += tx_frame[0x1F] << 16;
  dst_ip += tx_frame[0x20] << 8;
  dst_ip += tx_frame[0x21];

  src_port  = tx_frame[0x22] << 8;
  src_port += tx_frame[0x23];
  dst_port  = tx_frame[0x24] << 8;
  dst_port += tx_frame[0x25];

  recv_seq  = tx_frame[0x26] << 24;
  recv_seq += tx_frame[0x27] << 16;
  recv_seq += tx_frame[0x28] << 8;
  recv_seq += tx_frame[0x29];

  data_offset = tx_frame[0x2E] >> 4;
  flags       = tx_frame[0x2F];
  win_size    = tx_frame[0x30] << 8;
  win_size   += tx_frame[0x31];

  socket_index = -1;

  /* First check if this is a SYN packet. */
  if (flags == FLAGS_SYN) {
    /* Find a new available socket index. */
    for (i = 0; i < NET_SOCKETS_MAX; i++) {
      if (net->tcp_sockets[i].fd == -1) {
        socket_index = i;
        break;
      }
    }

    if (socket_index == -1) {
      panic("No more TCP sockets available!\n");
      return;
    }

    /* Open the socket and try to connect already. */
    net->tcp_sockets[socket_index].fd = socket(AF_INET, SOCK_STREAM, 0);
    if (net->tcp_sockets[socket_index].fd == -1) {
      panic("socket() failed with errno: %d\n", errno);
      return;
    }

    net_trace("TCP [%d] open: %d -- %s:%d\n", socket_index,
      src_port, net_trace_ip(dst_ip), dst_port);

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(dst_port);
    sa.sin_addr.s_addr = htonl(dst_ip);

    if (connect(net->tcp_sockets[socket_index].fd,
      (struct sockaddr *)&sa, sizeof(sa)) == -1) {
      net_tcp_close(net, socket_index, 0); /* Time out. */
      return;
    }

    /* Set non-blocking after connect() to avoid the need to retry. */
    fcntl_flags = fcntl(net->tcp_sockets[socket_index].fd, F_GETFL, 0);
    if (fcntl(net->tcp_sockets[socket_index].fd,
      F_SETFL, fcntl_flags | O_NONBLOCK) == 1) {
      panic("fcntl() failed with errno: %d\n", errno);
      net_tcp_close(net, socket_index, 0); /* Time out. */
      return;
    }

    net->tcp_sockets[socket_index].src_port = src_port;
    net->tcp_sockets[socket_index].dst_port = dst_port;
    net->tcp_sockets[socket_index].dst_ip   = dst_ip;
    net->tcp_sockets[socket_index].recv_seq = recv_seq + 1;
    net->tcp_sockets[socket_index].inactivity_timeout = 0;
    net->tcp_sockets[socket_index].fin_ack_sent = false;
    net->tcp_sockets[socket_index].ack_wait = 0;

    net_tcp_reply(net, 20, socket_index, FLAGS_SYN_ACK);
    net->tcp_sockets[socket_index].send_seq++; /* Increment after! */

    net_ipv4_reply(net, 20 + 20, IPPROTO_TCP, dst_ip);
    net_ethernet_reply(net);
    net->rx_len = 14 + 20 + 20;
    net->rx_ready = true;
    return;
  }

  /* If it is not a SYN packet, there should already be a active connection
     with a socket index associated. */
  for (i = 0; i < NET_SOCKETS_MAX; i++) {
    if (net->tcp_sockets[i].fd != -1 &&
        net->tcp_sockets[i].src_port == src_port &&
        net->tcp_sockets[i].dst_port == dst_port &&
        net->tcp_sockets[i].dst_ip == dst_ip) {
      socket_index = i;
      break;
    }
  }

  if (socket_index == -1) {
    /* Just ignore it if no active connection was found. */
    return;
  }

  net->udp_sockets[socket_index].inactivity_timeout = 0; /* Reset */

  net_trace("TCP [%d] tx: flags = %02x win = %d\n",
    socket_index, flags, win_size);

  if (flags == FLAGS_ACK) {
    /* Possibly let the next incoming packet through. */
    net->tcp_sockets[socket_index].ack_wait = 0;

  } else if (flags == FLAGS_RST) {
    /* Ignore. */

  } else if (flags == FLAGS_PSH_ACK) {
    /* Send data. */
    net->tcp_sockets[socket_index].ack_wait = 0;

    data_index = 0x22 + (data_offset * 4);
    data_len = ip_len - 20 - (data_offset * 4);

    send_bytes = send(net->tcp_sockets[socket_index].fd,
      &tx_frame[data_index], data_len, 0);
    if (send_bytes == -1) {
      panic("send() failed with errno: %d\n", errno);
      net_tcp_close(net, socket_index, FLAGS_RST_ACK);
      return;
    }

    net_trace("TCP [%d] send: %d -> %s:%d (%d bytes)\n",
      socket_index, src_port, net_trace_ip(dst_ip), dst_port, send_bytes);

    net->tcp_sockets[socket_index].recv_seq = recv_seq + data_len;

    net_tcp_reply(net, 20, socket_index, FLAGS_ACK);
    net_ipv4_reply(net, 20 + 20, IPPROTO_TCP, dst_ip);
    net_ethernet_reply(net);
    net->rx_len = 14 + 20 + 20;
    net->rx_ready = true;

  } else if (flags == FLAGS_FIN_ACK) {
    /* Graceful close of the socket. */
    if (net->tcp_sockets[socket_index].fin_ack_sent == true) {
      /* Remote host closed the socket, this is the final ACK. */
      net_tcp_close(net, socket_index, FLAGS_ACK);
    } else {
      /* Local host closed the socket, quickly terminate with RST-ACK. */
      net_tcp_close(net, socket_index, FLAGS_RST_ACK);
    }

  } else if (flags == FLAGS_RST_ACK) {
    /* Make sure socket is closed. */
    net_tcp_close(net, socket_index, FLAGS_ACK);

  } else {
    panic("Unhandled TCP flags: %02x\n", flags);
    net_tcp_close(net, socket_index, FLAGS_RST_ACK);
  }
}



static void net_handle_dhcp(net_t *net, uint8_t tx_frame[])
{
  int i;
  uint8_t type;

  if (! (tx_frame[0x116] == 0x63 &&
         tx_frame[0x117] == 0x82 &&
         tx_frame[0x118] == 0x53 &&
         tx_frame[0x119] == 0x63)) {
    return; /* Magic cookie not present, so not a DHCP packet. */
  }

  /* Note: Assumes message type is the first DHCP option. */
  if (tx_frame[0x11C] == 0x01) { /* DHCPDISCOVER */
    type = 0x02; /* DHCPOFFER */
  } else if (tx_frame[0x11C] == 0x03) { /* DHCPREQUEST */
    type = 0x05; /* DHCPACK */
  } else {
    return; /* Not handled. */
  }

  net->rx_frame[0x2A] = 2;              /* - OP = BOOTREPLY */
  net->rx_frame[0x2B] = tx_frame[0x2B]; /* - HTYPE          */
  net->rx_frame[0x2C] = tx_frame[0x2C]; /* - HLEN           */
  net->rx_frame[0x2D] = tx_frame[0x2D]; /* - HOPS           */
  net->rx_frame[0x2E] = tx_frame[0x2E]; /* / XID            */
  net->rx_frame[0x2F] = tx_frame[0x2F]; /* |                */
  net->rx_frame[0x30] = tx_frame[0x30]; /* |                */
  net->rx_frame[0x31] = tx_frame[0x31]; /* \                */
  net->rx_frame[0x32] = 0; /* / SECS   */
  net->rx_frame[0x33] = 0; /* \        */
  net->rx_frame[0x34] = 0; /* / FLAGS  */
  net->rx_frame[0x35] = 0; /* \        */
  net->rx_frame[0x36] = 0; /* / CIADDR */
  net->rx_frame[0x37] = 0; /* |        */
  net->rx_frame[0x38] = 0; /* |        */
  net->rx_frame[0x39] = 0; /* \        */
  net->rx_frame[0x3A] = (NET_IP_LOCAL >> 24) & 0xFF;  /* / YIADDR */
  net->rx_frame[0x3B] = (NET_IP_LOCAL >> 16) & 0xFF;  /* |        */
  net->rx_frame[0x3C] = (NET_IP_LOCAL >> 8)  & 0xFF;  /* |        */
  net->rx_frame[0x3D] =  NET_IP_LOCAL        & 0xFF;  /* \        */
  net->rx_frame[0x3E] = (NET_IP_REMOTE >> 24) & 0xFF; /* / SIADDR */
  net->rx_frame[0x3F] = (NET_IP_REMOTE >> 16) & 0xFF; /* |        */
  net->rx_frame[0x40] = (NET_IP_REMOTE >> 8)  & 0xFF; /* |        */
  net->rx_frame[0x41] =  NET_IP_REMOTE        & 0xFF; /* \        */
  net->rx_frame[0x42] = 0; /* / GIADDR */
  net->rx_frame[0x43] = 0; /* |        */
  net->rx_frame[0x44] = 0; /* |        */
  net->rx_frame[0x45] = 0; /* \        */

  for (i = 0; i < 208; i++) {
    net->rx_frame[0x46 + i] = 0;
  }

  net->rx_frame[0x116] = 0x63; /* / DHCP Magic Cookie */
  net->rx_frame[0x117] = 0x82; /* |                   */
  net->rx_frame[0x118] = 0x53; /* |                   */
  net->rx_frame[0x119] = 0x63; /* \                   */

  net->rx_frame[0x11A] = 0x35; /* / DHCP Message Type */
  net->rx_frame[0x11B] = 0x01; /* |                   */
  net->rx_frame[0x11C] = type; /* \                   */

  net->rx_frame[0x11D] = 0x01; /* / Subnet Mask */
  net->rx_frame[0x11E] = 0x04; /* |             */
  net->rx_frame[0x11F] = 0xff; /* |             */
  net->rx_frame[0x120] = 0xff; /* |             */
  net->rx_frame[0x121] = 0xff; /* |             */
  net->rx_frame[0x122] = 0x00; /* \             */

  net->rx_frame[0x123] = 0x03;                         /* / Gateway */
  net->rx_frame[0x124] = 0x04;                         /* |         */
  net->rx_frame[0x125] = (NET_IP_REMOTE >> 24) & 0xFF; /* |         */
  net->rx_frame[0x126] = (NET_IP_REMOTE >> 16) & 0xFF; /* |         */
  net->rx_frame[0x127] = (NET_IP_REMOTE >> 8)  & 0xFF; /* |         */
  net->rx_frame[0x128] =  NET_IP_REMOTE        & 0xFF; /* \         */

  net->rx_frame[0x129] = 0x36;                         /* / DHCP Server */
  net->rx_frame[0x12A] = 0x04;                         /* |             */
  net->rx_frame[0x12B] = (NET_IP_REMOTE >> 24) & 0xFF; /* |             */
  net->rx_frame[0x12C] = (NET_IP_REMOTE >> 16) & 0xFF; /* |             */
  net->rx_frame[0x12D] = (NET_IP_REMOTE >> 8)  & 0xFF; /* |             */
  net->rx_frame[0x12E] =  NET_IP_REMOTE        & 0xFF; /* \             */

  net->rx_frame[0x12F] = 0x33; /* / Lease Time  */
  net->rx_frame[0x130] = 0x04; /* |             */
  net->rx_frame[0x131] = 0xFF; /* |             */
  net->rx_frame[0x132] = 0xFF; /* |             */
  net->rx_frame[0x133] = 0xFF; /* |             */
  net->rx_frame[0x134] = 0xFF; /* \             */

  for (i = 0x135; i < 0x24E; i++) {
    net->rx_frame[i] = 0;
  }

  net_udp_reply(net, 548, NET_IP_REMOTE, 67, 68);
  net_ipv4_reply(net, 20 + 8 + 548, IPPROTO_UDP, NET_IP_REMOTE);
  net_ethernet_reply(net);
  net->rx_len = 14 + 20 + 8 + 548;
  net->rx_ready = true;
}



static void net_handle_udp(net_t *net, uint8_t tx_frame[])
{
  int i;
  int socket_index;
  ssize_t send_bytes;
  struct sockaddr_in sa;
  uint16_t dst_port;
  uint16_t send_len;
  uint16_t src_port;
  uint32_t dst_ip;

  dst_ip  = tx_frame[0x1E] << 24;
  dst_ip += tx_frame[0x1F] << 16;
  dst_ip += tx_frame[0x20] << 8;
  dst_ip += tx_frame[0x21];

  src_port  = tx_frame[0x22] << 8;
  src_port += tx_frame[0x23];
  dst_port  = tx_frame[0x24] << 8;
  dst_port += tx_frame[0x25];
  send_len  = tx_frame[0x26] << 8;
  send_len += tx_frame[0x27];

  socket_index = -1;

  /* Intercept broadcast attempts. */
  if (dst_ip == 0xffffffff) {
    if (dst_port == 67 && src_port == 68) { /* Handle DHCP request. */
      net_handle_dhcp(net, tx_frame);
    }
    return;
  }

  /* Check if socket for this connection is already open, then re-use it! */
  for (i = 0; i < NET_SOCKETS_MAX; i++) {
    if (net->udp_sockets[i].fd != -1 &&
        net->udp_sockets[i].src_port == src_port &&
        net->udp_sockets[i].dst_ip == dst_ip) {
      socket_index = i;
      break;
    }
  }

  if (socket_index == -1) {
    /* Open a new socket. */
    for (i = 0; i < NET_SOCKETS_MAX; i++) {
      if (net->udp_sockets[i].fd == -1) {
        socket_index = i;
        break;
      }
    }

    if (socket_index == -1) {
      panic("No more UDP sockets available!\n");
      return;
    }

    net->udp_sockets[socket_index].fd = socket(AF_INET,
      SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (net->udp_sockets[socket_index].fd == -1) {
      panic("socket() failed with errno: %d\n", errno);
      return;
    }

    net->udp_sockets[socket_index].src_port = src_port;
    net->udp_sockets[socket_index].dst_port = dst_port;
    net->udp_sockets[socket_index].dst_ip   = dst_ip;
  }

  net->udp_sockets[socket_index].inactivity_timeout = 0; /* Reset */

  memset(&sa, 0, sizeof(sa));
  sa.sin_family = AF_INET;
  sa.sin_port = htons(dst_port);
  sa.sin_addr.s_addr = htonl(dst_ip);

  send_bytes = sendto(net->udp_sockets[socket_index].fd, &tx_frame[0x2A],
    send_len - 8, 0, (struct sockaddr *)&sa, sizeof(sa));

  if (send_bytes == -1) {
    panic("sendto() failed with errno: %d\n", errno);
    net_udp_close(net, socket_index);
    return;
  }

  net_trace("UDP [%d] send: %d -> %s:%d (%d bytes)\n",
    socket_index, src_port, net_trace_ip(dst_ip), dst_port, send_bytes);
}



static void net_check_tcp_socket(net_t *net, int socket_index)
{
  ssize_t recv_bytes;
  uint32_t src_ip;

  src_ip = net->tcp_sockets[socket_index].dst_ip;

  if (net->tcp_sockets[socket_index].ack_wait > 0) {
    /* Wait for the ACK to arrive from the client to prevent overwhelming
       its stack with too many incoming packets. */
    net->tcp_sockets[socket_index].ack_wait--;
    return;
  }

  recv_bytes = recv(net->tcp_sockets[socket_index].fd,
    &net->rx_frame[0x36], NET_MTU - 0x36, 0);

  if (recv_bytes == -1) {
    if (errno == EAGAIN) {
      /* Close socket after a certain time of inactivity. */
      net->tcp_sockets[socket_index].inactivity_timeout++;
      if (net->tcp_sockets[socket_index].inactivity_timeout >
        NET_SOCKET_INACTIVITY_TIMEOUT) {
        net_tcp_close(net, socket_index, FLAGS_RST_ACK);
      }
      return;

    } else {
      panic("recv() failed with errno: %d\n", errno);
      net_tcp_close(net, socket_index, FLAGS_RST_ACK);
      return;
    }
  }
  if (recv_bytes == 0) {
    if (errno == EAGAIN) {
      /* Most likely remote socket was closed, start a graceful shutdown. */
      if (net->tcp_sockets[socket_index].fin_ack_sent == false) {
        net_tcp_reply(net, 20, socket_index, FLAGS_FIN_ACK);
        net->tcp_sockets[socket_index].send_seq++; /* Increment after! */
        net_ipv4_reply(net, 20 + 20, IPPROTO_TCP, src_ip);
        net_ethernet_reply(net);
        net->rx_len = 14 + 20 + 20;
        net->rx_ready = true;
        net->tcp_sockets[socket_index].fin_ack_sent = true;
      }
      return;
    }
  }
  net->tcp_sockets[socket_index].inactivity_timeout = 0; /* Reset */
  net->tcp_sockets[socket_index].ack_wait = NET_SOCKET_ACK_WAIT;

  net_trace("TCP [%d] recv: %d <- %s:%d (%d bytes)\n", socket_index,
    net->tcp_sockets[socket_index].src_port,
    net_trace_ip(net->tcp_sockets[socket_index].dst_ip),
    net->tcp_sockets[socket_index].dst_port, recv_bytes);

  net_tcp_reply(net, 20 + recv_bytes, socket_index, FLAGS_PSH_ACK);
  net->tcp_sockets[socket_index].send_seq += recv_bytes; /* Increment after! */

  net_ipv4_reply(net, 20 + 20 + recv_bytes, IPPROTO_TCP, src_ip);
  net_ethernet_reply(net);
  net->rx_len = 14 + 20 + 20 + recv_bytes;
  net->rx_ready = true;
}



static void net_check_udp_socket(net_t *net, int socket_index)
{
  uint32_t src_ip;
  uint16_t src_port;
  uint16_t dst_port;
  socklen_t recv_sa_len;
  ssize_t recv_bytes;
  struct sockaddr recv_sa;

  recv_sa_len = sizeof(recv_sa);
  recv_bytes = recvfrom(net->udp_sockets[socket_index].fd,
    &net->rx_frame[0x2A], NET_MTU - 0x2A, 0, &recv_sa, &recv_sa_len);

  if (recv_bytes == -1) {
    if (errno == EAGAIN) {
      /* Close socket after a certain time of inactivity. */
      net->udp_sockets[socket_index].inactivity_timeout++;
      if (net->udp_sockets[socket_index].inactivity_timeout >
        NET_SOCKET_INACTIVITY_TIMEOUT) {
        net_udp_close(net, socket_index);
      }
      return;

    } else {
      panic("recvfrom() failed with errno: %d\n", errno);
      net_udp_close(net, socket_index);
      return;
    }
  }
  net->udp_sockets[socket_index].inactivity_timeout = 0; /* Reset */

  src_ip = ntohl(((struct sockaddr_in *)&recv_sa)->sin_addr.s_addr);
  src_port = ntohs(((struct sockaddr_in *)&recv_sa)->sin_port);
  dst_port = net->udp_sockets[socket_index].src_port;

  net_trace("UDP [%d] recv: %d <- %s:%d (%d bytes)\n",
    socket_index, dst_port, net_trace_ip(src_ip), src_port, recv_bytes);

  net_udp_reply(net, recv_bytes, src_ip, src_port, dst_port);
  net_ipv4_reply(net, 20 + 8 + recv_bytes, IPPROTO_UDP, src_ip);
  net_ethernet_reply(net);
  net->rx_len = 14 + 20 + 8 + recv_bytes;
  net->rx_ready = true;
}



static void net_handle_ipv4(net_t *net, uint8_t tx_frame[], uint16_t tx_len)
{
  uint8_t proto;

  proto = tx_frame[0x17];

  switch (proto) {
  case IPPROTO_ICMP:
    net_handle_icmp(net, tx_frame, tx_len);
    break;
  case IPPROTO_TCP:
    net_handle_tcp(net, tx_frame);
    break;
  case IPPROTO_UDP:
    net_handle_udp(net, tx_frame);
    break;
  default:
    break;
  }
}



static void net_handle_arp(net_t *net, uint8_t tx_frame[], uint16_t tx_len)
{
  uint16_t oper;
  uint32_t who_has_ip;

  if (tx_len < 0x29) {
    return;
  }

  oper  = tx_frame[0x14] << 8;
  oper += tx_frame[0x15];

  if (oper != 1) {
    return; /* Only handle ARP requests. */
  }

  net->rx_frame[0x0C] = 0x08; /* / Ethertype = ARP  */
  net->rx_frame[0x0D] = 0x06; /* \                  */
  net->rx_frame[0x0E] = 0x00; /* / HTYPE = Ethernet */
  net->rx_frame[0x0F] = 0x01; /* \                  */
  net->rx_frame[0x10] = 0x08; /* / PTYPE = IPv4     */
  net->rx_frame[0x11] = 0x00; /* \                  */
  net->rx_frame[0x12] = 0x06; /* - HLEN = 6         */
  net->rx_frame[0x13] = 0x04; /* - PLEN = 4         */
  net->rx_frame[0x14] = 0x00; /* / OPER = 2 = Reply */
  net->rx_frame[0x15] = 0x02; /* \                  */

  who_has_ip  = tx_frame[0x26] << 24;
  who_has_ip += tx_frame[0x27] << 16;
  who_has_ip += tx_frame[0x28] << 8;
  who_has_ip += tx_frame[0x29];

  if (who_has_ip == NET_IP_REMOTE) {
    net->rx_frame[0x16] = NET_MAC_REMOTE; /* Sender MAC */
    net->rx_frame[0x17] = NET_MAC_REMOTE;
    net->rx_frame[0x18] = NET_MAC_REMOTE;
    net->rx_frame[0x19] = NET_MAC_REMOTE;
    net->rx_frame[0x1A] = NET_MAC_REMOTE;
    net->rx_frame[0x1B] = NET_MAC_REMOTE;

    net->rx_frame[0x1C] = (NET_IP_REMOTE >> 24) & 0xFF; /* Sender IP */
    net->rx_frame[0x1D] = (NET_IP_REMOTE >> 16) & 0xFF;
    net->rx_frame[0x1E] = (NET_IP_REMOTE >> 8)  & 0xFF;
    net->rx_frame[0x1F] =  NET_IP_REMOTE        & 0xFF;

  } else if (who_has_ip == NET_IP_LOCAL) {
    net->rx_frame[0x16] = NET_MAC_LOCAL; /* Sender MAC */
    net->rx_frame[0x17] = NET_MAC_LOCAL;
    net->rx_frame[0x18] = NET_MAC_LOCAL;
    net->rx_frame[0x19] = NET_MAC_LOCAL;
    net->rx_frame[0x1A] = NET_MAC_LOCAL;
    net->rx_frame[0x1B] = NET_MAC_LOCAL;

    net->rx_frame[0x1C] = (NET_IP_LOCAL >> 24) & 0xFF; /* Sender IP */
    net->rx_frame[0x1D] = (NET_IP_LOCAL >> 16) & 0xFF;
    net->rx_frame[0x1E] = (NET_IP_LOCAL >> 8)  & 0xFF;
    net->rx_frame[0x1F] =  NET_IP_LOCAL        & 0xFF;

  } else {
    return; /* Unknown IP... */
  }

  net->rx_frame[0x20] = NET_MAC_LOCAL; /* Destination MAC */
  net->rx_frame[0x21] = NET_MAC_LOCAL;
  net->rx_frame[0x22] = NET_MAC_LOCAL;
  net->rx_frame[0x23] = NET_MAC_LOCAL;
  net->rx_frame[0x24] = NET_MAC_LOCAL;
  net->rx_frame[0x25] = NET_MAC_LOCAL;

  net->rx_frame[0x26] = (NET_IP_LOCAL >> 24) & 0xFF; /* Destination IP */
  net->rx_frame[0x27] = (NET_IP_LOCAL >> 16) & 0xFF;
  net->rx_frame[0x28] = (NET_IP_LOCAL >> 8)  & 0xFF;
  net->rx_frame[0x29] =  NET_IP_LOCAL        & 0xFF;

  net_ethernet_reply(net);
  net->rx_len = 0x2A;
  net->rx_ready = true;
}



void net_tx_frame(net_t *net, uint8_t tx_frame[], uint16_t tx_len)
{
  uint16_t ethertype;

  /* Handle packet, and provide appropriate reply. */
  ethertype  = tx_frame[0xC] << 8;
  ethertype += tx_frame[0xD];

  switch (ethertype) {
  case 0x0806: /* ARP */
    net_handle_arp(net, tx_frame, tx_len);
    break;
  case 0x0800: /* IPv4 */
    net_handle_ipv4(net, tx_frame, tx_len);
    break;
  case 0xEDF5: /* EtherDFS */
    edfs_handle_packet(net, tx_frame, tx_len);
    break;
  default:
    break;
  }
}



void net_init(net_t *net)
{
  int i;

  memset(net, 0, sizeof(net_t));

  for (i = 0; i < NET_SOCKETS_MAX; i++) {
    net->udp_sockets[i].fd = -1;
  }
  for (i = 0; i < NET_SOCKETS_MAX; i++) {
    net->tcp_sockets[i].fd = -1;
    net->tcp_sockets[i].send_seq = (i * 0x1000000);
  }

  for (i = 0; i < NET_TRACE_BUFFER_SIZE; i++) {
    net_trace_buffer[i][0] = '\0';
  }
  net_trace_buffer_n = 0;
}



void net_execute(net_t *net)
{
  int i;

  for (i = 0; i < NET_SOCKETS_MAX; i++) {
    if (net->rx_ready == true) {
      return; /* Prevent overwriting pending packet! */
    }
    if (net->udp_sockets[i].fd != -1) {
      net_check_udp_socket(net, i);
    }
  }

  for (i = 0; i < NET_SOCKETS_MAX; i++) {
    if (net->rx_ready == true) {
      return; /* Prevent overwriting pending packet! */
    }
    if (net->tcp_sockets[i].fd != -1) {
      net_check_tcp_socket(net, i);
    }
  }
}



void net_trace_dump(FILE *fh)
{
  int i;

  for (i = net_trace_buffer_n; i < NET_TRACE_BUFFER_SIZE; i++) {
    if (net_trace_buffer[i][0] != '\0') {
      fprintf(fh, net_trace_buffer[i]);
    }
  }
  for (i = 0; i < net_trace_buffer_n; i++) {
    if (net_trace_buffer[i][0] != '\0') {
      fprintf(fh, net_trace_buffer[i]);
    }
  }
}



