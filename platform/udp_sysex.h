/* SPDX-License-Identifier: GPL-3.0-only */
/* plat_sysex_* without a USB cable: one datagram is one message, the
 * bytes between F0 and F7. For the simulator and the tests, so the link
 * can be tried with tools/fm1_send.py --udp before the machine is on
 * the desk. Bound to 127.0.0.1 only. */
#ifndef UDP_SYSEX_H
#define UDP_SYSEX_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define UDP_SYSEX_PORT 19780 /* 0x4D44, "MD"; MOONDAW_LINK_PORT overrides */

static int udp_fd = -1;
static struct sockaddr_in udp_peer;
static uint8_t udp_msg[700];
static uint32_t udp_len;
static uint32_t udp_seen; /* messages taken so far */

static int udp_sysex_open(void)
{
    const char *env = getenv("MOONDAW_LINK_PORT");
    struct sockaddr_in a = { 0 };
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons((uint16_t)(env ? atoi(env) : UDP_SYSEX_PORT));
    if (bind(fd, (struct sockaddr *)&a, sizeof a) < 0 || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        close(fd);
        return -1; /* the port is taken: another simulator has the link */
    }
    udp_fd = fd;
    return 0;
}

int plat_sysex_get(const uint8_t **p, uint32_t *n)
{
    while (!udp_len && udp_fd >= 0) {
        socklen_t sl = sizeof udp_peer;
        ssize_t got = recvfrom(udp_fd, udp_msg, sizeof udp_msg, 0, (struct sockaddr *)&udp_peer, &sl);
        if (got <= 0)
            return 0;
        if (got >= 4 && udp_msg[0] == 0x7D && udp_msg[1] == 0x4D && udp_msg[2] == 0x44)
            udp_len = (uint32_t)got, udp_seen++;
    }
    if (!udp_len)
        return 0;
    *p = udp_msg;
    *n = udp_len;
    return 1;
}

void plat_sysex_done(void) { udp_len = 0; }

int plat_sysex_send(const uint8_t *p, uint32_t n)
{
    if (udp_fd < 0)
        return -1;
    return sendto(udp_fd, p, n, 0, (struct sockaddr *)&udp_peer, sizeof udp_peer) == (ssize_t)n ? 0 : -1;
}

#endif
