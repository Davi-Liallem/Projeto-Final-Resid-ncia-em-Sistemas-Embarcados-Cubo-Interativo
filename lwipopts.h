#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

// =======================================================
// lwIP options para Pico W + cyw43 threadsafe_background
// (RAW API, sem sys_arch / sem sockets)
// =======================================================

// threadsafe_background usa lwIP NO_SYS (RAW API)
#ifndef NO_SYS
#define NO_SYS                      1
#endif

// Sem NETCONN e sem SOCKETS (evita exigir sys_arch.h)
#ifndef LWIP_NETCONN
#define LWIP_NETCONN                0
#endif

#ifndef LWIP_SOCKET
#define LWIP_SOCKET                 0
#endif

#if PICO_CYW43_ARCH_POLL
#define MEM_LIBC_MALLOC             1
#else
#define MEM_LIBC_MALLOC             0
#endif

#define MEM_ALIGNMENT               4
#ifndef MEM_SIZE
#define MEM_SIZE                    4000
#endif

#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              24

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1

#define TCP_WND                     (8 * TCP_MSS)
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1

#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0

#define LWIP_CHKSUM_ALGORITHM       3

#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1
#define LWIP_TCP_KEEPALIVE          1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#ifndef MEMP_NUM_SYS_TIMEOUT
#define MEMP_NUM_SYS_TIMEOUT  (LWIP_NUM_SYS_TIMEOUT_INTERNAL + 8)
#endif

#ifndef NDEBUG
#define LWIP_DEBUG                  1
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          1
#endif

// Logs OFF
#define ETHARP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG                 LWIP_DBG_OFF
#define PBUF_DEBUG                  LWIP_DBG_OFF
#define API_LIB_DEBUG               LWIP_DBG_OFF
#define API_MSG_DEBUG               LWIP_DBG_OFF
#define SOCKETS_DEBUG               LWIP_DBG_OFF
#define ICMP_DEBUG                  LWIP_DBG_OFF
#define INET_DEBUG                  LWIP_DBG_OFF
#define IP_DEBUG                    LWIP_DBG_OFF
#define IP_REASS_DEBUG              LWIP_DBG_OFF
#define RAW_DEBUG                   LWIP_DBG_OFF
#define MEM_DEBUG                   LWIP_DBG_OFF
#define MEMP_DEBUG                  LWIP_DBG_OFF
#define SYS_DEBUG                   LWIP_DBG_OFF
#define TCP_DEBUG                   LWIP_DBG_OFF
#define UDP_DEBUG                   LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_OFF

#endif /* __LWIPOPTS_H__ */
