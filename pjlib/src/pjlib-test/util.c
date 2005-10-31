/* $Header: /pjproject-0.3/pjlib/src/pjlib-test/util.c 3     10/29/05 11:51a Bennylp $
 */
/* 
 * $Log: /pjproject-0.3/pjlib/src/pjlib-test/util.c $
 * 
 * 3     10/29/05 11:51a Bennylp
 * Version 0.3-pre2.
 * 
 * 2     10/14/05 12:26a Bennylp
 * Finished error code framework, some fixes in ioqueue, etc. Pretty
 * major.
 * 
 * 1     10/12/05 10:00a Bennylp
 * Created.
 *
 */
#include "test.h"
#include <pjlib.h>

void app_perror(const char *msg, pj_status_t rc)
{
    char errbuf[256];

    PJ_CHECK_STACK();

    pj_strerror(rc, errbuf, sizeof(errbuf));
    PJ_LOG(1,("test", "%s: [pj_status_t=%d] %s", msg, rc, errbuf));
}

#define SERVER 0
#define CLIENT 1

pj_status_t app_socket(int family, int type, int proto, int port,
                       pj_sock_t *ptr_sock)
{
    pj_sockaddr_in addr;
    pj_sock_t sock;
    pj_status_t rc;

    rc = pj_sock_socket(family, type, proto, &sock);
    if (rc != PJ_SUCCESS)
        return rc;

    pj_memset(&addr, 0, sizeof(addr));
    addr.sin_family = (pj_uint16_t)family;
    addr.sin_port = (short)(port!=-1 ? pj_htons((pj_uint16_t)port) : 0);
    rc = pj_sock_bind(sock, &addr, sizeof(addr));
    if (rc != PJ_SUCCESS)
        return rc;
    
    if (type == PJ_SOCK_STREAM) {
        rc = pj_sock_listen(sock, 5);
        if (rc != PJ_SUCCESS)
            return rc;
    }

    *ptr_sock = sock;
    return PJ_SUCCESS;
}

pj_status_t app_socketpair(int family, int type, int protocol,
                           pj_sock_t *serverfd, pj_sock_t *clientfd)
{
    int i;
    static unsigned short port = 11000;
    pj_sockaddr_in addr;
    pj_str_t s;
    pj_status_t rc = 0;
    pj_sock_t sock[2];

    /* Create both sockets. */
    for (i=0; i<2; ++i) {
        rc = pj_sock_socket(family, type, protocol, &sock[i]);
        if (rc != PJ_SUCCESS) {
            if (i==1)
                pj_sock_close(sock[0]);
            return rc;
        }
    }

    /* Retry bind */
    pj_memset(&addr, 0, sizeof(addr));
    addr.sin_family = PJ_AF_INET;
    for (i=0; i<5; ++i) {
        addr.sin_port = pj_htons(port++);
        rc = pj_sock_bind(sock[SERVER], &addr, sizeof(addr));
        if (rc == PJ_SUCCESS)
            break;
    }

    if (rc != PJ_SUCCESS)
        goto on_error;

    /* For TCP, listen the socket. */
    if (type == PJ_SOCK_STREAM) {
        rc = pj_sock_listen(sock[SERVER], PJ_SOMAXCONN);
        if (rc != PJ_SUCCESS)
            goto on_error;
    }

    /* Connect client socket. */
    addr.sin_addr = pj_inet_addr(pj_cstr(&s, "127.0.0.1"));
    rc = pj_sock_connect(sock[CLIENT], &addr, sizeof(addr));
    if (rc != PJ_SUCCESS)
        goto on_error;

    /* For TCP, must accept(), and get the new socket. */
    if (type == PJ_SOCK_STREAM) {
        pj_sock_t newserver;

        rc = pj_sock_accept(sock[SERVER], &newserver, NULL, NULL);
        if (rc != PJ_SUCCESS)
            goto on_error;

        /* Replace server socket with new socket. */
        pj_sock_close(sock[SERVER]);
        sock[SERVER] = newserver;
    }

    *serverfd = sock[SERVER];
    *clientfd = sock[CLIENT];

    return rc;

on_error:
    for (i=0; i<2; ++i)
        pj_sock_close(sock[i]);
    return rc;
}
