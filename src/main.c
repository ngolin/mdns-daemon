#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <errno.h>
#include "mdns.h"
#include "host.h"
#include "ipad.h"

#define BUFFERSIZE 4096
#define PACKETSIZE 512

int open_socket()
{
    int sockfd;
    size_t enabled = 1;
    struct sockaddr_in bind_addr;
    struct ip_mreq mreq;

    if (0 > (sockfd = socket(AF_INET, SOCK_DGRAM, 0)))
    {
        fprintf(stderr, "Could not open\n");
        return -1;
    }
    if (0 > setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(size_t)))
    {
        fprintf(stderr, "Reuseaddr failed\n");
        return -1;
    }
#ifdef SO_REUSEPORT
    if (0 > setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(size_t)))
    {
        fprintf(stderr, "Reuseport failed\n");
        return -1;
    }
#endif
    memset(&bind_addr, 0, sizeof(struct sockaddr_in));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(5353);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    if (0 > bind(sockfd, (struct sockaddr *)&bind_addr, sizeof(struct sockaddr_in)))
    {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }
    // TODO: join IPv6 membership
    mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (0 > setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)))
    {
        fprintf(stderr, "Could not join\n");
        return -1;
    }
    return sockfd;
}

void print_buffer(const unsigned char *buffer, size_t size)
{
    size_t i;
    for (i = 0; i < size; i++)
    {
        printf(" %02X", buffer[i]);
        if (i % 2)
        {
            putchar('\n');
        }
    }
    putchar('\n');
}

int main(void)
{
    // about socket
    int sockfd;
    // about peer socket
    struct sockaddr_storage peer_addr;
    // struct sockaddr_in6 *peer_addr6;
    struct sockaddr_in *peer_addr4;
    uint32_t uint_ipv4;
    // char peer_ip[INET6_ADDRSTRLEN];
    socklen_t peer_slen;
    // about buffer
    unsigned char hostlist[BUFFERSIZE];
    char hostname[NI_MAXHOST];
    unsigned char *buffer, *puffer;
    size_t buffersize = 0;
    ssize_t iosize;
    struct dns_header_t *dns_header;
    struct answer_t *answer;
    // about dns header
    // open socket
    if (0 > (sockfd = open_socket()))
    {
        fprintf(stderr, "Could not open\n");
        exit(EXIT_FAILURE);
    }
    // query all available hostnames
    loadup_hostlist(hostlist, BUFFERSIZE - PACKETSIZE);
    printf("Respnod for:\n");
    while (hostlist[buffersize])
    {
        printf("    %s\n", hostlist + buffersize + 1);
        buffersize += hostlist[buffersize];
    }
    buffer = hostlist + buffersize + 1;
    // wait for messages
    for (;;)
    {
        peer_slen = sizeof(struct sockaddr_storage);
        iosize = recvfrom(sockfd, buffer, PACKETSIZE, 0, (struct sockaddr *)&peer_addr, &peer_slen);
        if (0 > iosize || peer_addr.ss_family != AF_INET)
        {
            // TODO: join IPv6 membership
            printf("Got IPv6 message\n");
            continue;
        }
        // parse dns header
        dns_header = (struct dns_header_t *)buffer;

        if (IS_DNSFLAG_RESPD(dns_header->DNSFLAG) || 0 == dns_header->QDCOUNT)
        {
            printf("Got response message(%ld)\n", iosize);
            continue;
        }
        printf("Got query message(%ld)\n", iosize);
        puffer = buffer + sizeof(struct dns_header_t);
        if (offset_hostname(puffer))
        {
            // TODO: answer questions when QDCOUNT > 1
            continue;
        }
        puffer = pull_hostname(puffer, hostname);
        answer = (struct answer_t *)puffer;
        if (hostname[0] == '_' || answer->TYPE != TYPE_A || !lookup_hostname(hostlist, hostname))
        {
            // TODO: answer IPv6 query
            printf("Not me: %s\n", hostname);
            continue;
        }
        dns_header->DNSFLAG = DNSFLAG_RESPD_NO_ORROR;
        dns_header->QDCOUNT = 0;
        dns_header->ANCOUNT = ENCOUNT(1);
        dns_header->NSCOUNT = 0;
        dns_header->ARCOUNT = 0;
        answer->CLASS = ACLASS_IN;
        answer->TTL = TTL_4_HOURS;
        answer->RDLENGTH = RDLENGTH_IPv4;
        peer_addr4 = (struct sockaddr_in *)&peer_addr;
        uint_ipv4 = htonl(peer_addr4->sin_addr.s_addr);
        uint_ipv4 = lookup_ipv4(uint_ipv4);
        *(uint32_t *)(puffer + sizeof(struct answer_t)) = htonl(uint_ipv4);
        iosize = puffer - buffer + sizeof(struct answer_t) + sizeof(uint32_t);
        sendto(sockfd, buffer, iosize, 0, (struct sockaddr *)&peer_addr, peer_slen);
        printf("Send to %s(%ld)\n", hostname, iosize);
    }
}
