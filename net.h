#ifndef _NET_H
#define _NET_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define NET_MTU 1514
#define NET_SOCKETS_MAX 5
#define NET_SOCKET_INACTIVITY_TIMEOUT 1000000
#define NET_SOCKET_ACK_WAIT 100

/* The local MAC is the address for the emulated network card,
   while the remote MAC is the address for the one and only remote host on
   the emulated network. For simplicity, all 6 bytes of the address are just
   repeated, so e.g. 0x22 becomes 22:22:22:22:22:22 */
#define NET_MAC_REMOTE 0x11
#define NET_MAC_LOCAL  0x22

/* Hardcoded IPv4 addresses used by the emulated remote host in the emulated
   network. For simplicity, they are defined as 32-bit big-endian values. */
#define NET_IP_REMOTE 0x0A000001 /* 10.0.0.1 */
#define NET_IP_LOCAL  0x0A000002 /* 10.0.0.2 */

typedef struct net_udp_socket_s {
  int fd;
  int inactivity_timeout;
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t dst_ip;
} net_udp_socket_t;

typedef struct net_tcp_socket_s {
  int fd;
  int inactivity_timeout;
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t dst_ip;
  uint32_t send_seq; /* Next number to send to client. */
  uint32_t recv_seq; /* Last received number from client. */
  bool fin_ack_sent; /* Used during graceful shutdown. */
  int ack_wait; /* Wait counter for flow control. */
} net_tcp_socket_t;

typedef struct net_s {
  uint8_t rx_frame[NET_MTU];
  uint16_t rx_len;
  bool rx_ready;
  uint16_t ip_id;
  net_udp_socket_t udp_sockets[NET_SOCKETS_MAX];
  net_tcp_socket_t tcp_sockets[NET_SOCKETS_MAX];
} net_t;

void net_tx_frame(net_t *net, uint8_t tx_frame[],
  uint16_t tx_len);
void net_init(net_t *net);
void net_execute(net_t *net);
void net_trace_dump(FILE *fh);

#endif /* _NET_H */
