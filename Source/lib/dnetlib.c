
/*
 *  DNETLIB.C
 *
 *      DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
 *
 *  Library Interface for DNET.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#ifdef O_CREAT
#include <sys/file.h>
#endif
#include <sys/un.h>
#include "dnetlib.h"
#include "dnetutil.h"

#define NAMEPAT "%sPORT.%ld"

static void serverShutdown(void)
{
    Log(LogLevelInfo, "Server shutdown.\n");
}

CHANN *DListen(uword port)
{
    CHANN *chan = NULL;

    atexit(serverShutdown);

    int serverSocket = BindUnixSocket(NAMEPAT, GetDnetDir(), (long) port);

    if(serverSocket >= 0)
    {
        chan = (CHANN *)malloc(sizeof(CHANN));
        chan->s = serverSocket;
        chan->port = port;      
    }

    return(chan);
}

void DUnListen(CHANN *chan)
{
    char path[PATH_MAX];

    close(chan->s);

    snprintf(path, sizeof(path), NAMEPAT, GetDnetDir(), (long) chan->port);

    unlink(path);

    free(chan);
}

int DAccept(CHANN *chan)
{
    SOCKADDR sa;
    socklen_t addrlen = sizeof(sa);
    int fd;

    fd = accept(chan->s, &sa, &addrlen);
    return(fd);
}

int DOpen(char *host,uword port,char txpri,char rxpri)
{
    int s;
    char rc;
    short xb[3];
    static struct sockaddr_un sa;
    char *dirstr = getenv("DNETDIR") ? getenv("DNETDIR") : "";

        fprintf(stderr, "DOpen(\"%s\",%u,%d,%d)\n", host, port, txpri, rxpri);


    if (rxpri < -127)
        rxpri = -127;
    if (rxpri > 126)
        rxpri = 126;
    if (txpri < -127)
        txpri = -127;
    if (txpri > 126)
        txpri = 126;

    if (host == NULL)
        host = (getenv("DNETHOST")) ? getenv("DNETHOST"):"3";

    sa.sun_family = AF_UNIX;
    sprintf(sa.sun_path, "%s%s%s", dirstr, "DNET.", host);

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    fcntl(s, F_SETOWN, getpid());
    if (connect(s, (struct sockaddr *) &sa, sizeof(sa.sun_family) + sizeof(sa.sun_len) + strlen(sa.sun_path)) < 0) {
        fprintf(stderr, "Opening \"%s\" failed with: %s.\n", sa.sun_path, strerror(errno));
        close(s);
        return(-1);
    }
    xb[0] = port;
    ((char *)&xb[1])[0] = txpri;
    ((char *)&xb[1])[1] = rxpri;
    write(s, xb, 4);
    if (read(s, &rc, 1) == 1 && rc == 0)
        return(s);
    close(s);
    return(-1);
}

void DEof(int fd)
{
    char dummy;

    shutdown(fd, 1);
    write(fd, &dummy, 0);
}

int gwrite(int fd, char *buf, int bytes)
{
    int n;
    int orig = bytes;

    Log(LogLevelDebug, "gwrite(fd=%d,buf=%p,bytes=%d)\n", fd, buf, bytes);

    while (bytes) {
        n = write(fd, buf, bytes);
        if (n > 0) {
            bytes -= n;
            buf += n;
            continue;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EWOULDBLOCK) {
                fd_set wm;
                fd_set em;
                FD_ZERO(&wm);
                FD_ZERO(&em);
                FD_SET(fd,&wm);
                FD_SET(fd,&em);
                if (select(fd+1, NULL, &wm, &em, NULL) < 0)
                    continue;
                if (FD_ISSET(fd,&wm))
                    continue;
            }
            return(orig - bytes);
        }
    }
    return(orig);
}

int gread(int fd, char *buf, int bytes)
{
    int n;
    int orig = bytes;

    Log(LogLevelDebug, "gread(fd=%d,buf=%p,bytes=%d)\n", fd, buf, bytes);

    while (bytes) {
        n = read(fd, buf, bytes);
        if (n > 0) {
            bytes -= n;
            buf += n;
            break;
        }
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EWOULDBLOCK) {
                fd_set rm;
                fd_set em;
                FD_ZERO(&rm);
                FD_ZERO(&em);
                FD_SET(fd,&rm);
                FD_SET(fd,&em);
                if (select(fd+1, &rm, NULL, &em, NULL) < 0)
                    continue;
                if (FD_ISSET(fd,&rm))
                    continue;
            }
            return(orig - bytes);
        }
        if (n == 0) {
            Log(LogLevelDebug, "Read 0 bytes.\n");
            break;
        }
    }
    return(orig - bytes);
}

int ggread(int fd, char *buf, int bytes)
{
    int n;
    int ttl = 0;

    Log(LogLevelDebug, "ggread(fd=%d,buf=%p,bytes=%d)\n", fd, buf, bytes);

    while (bytes) {
        n = gread(fd, buf, bytes);
        if (n > 0) {
            bytes -= n;
            buf += n;
            ttl += n;
            continue;
        }
        return(-1);
    }
    return(ttl);
}

/*
 *      Convert to and from 68000 longword format.  Of course, it really
 *      doesn't matter what format you use, just as long as it is defined.
 */

ulong ntohl68(ulong n)
{
    return(
        (((ubyte *)&n)[0] << 24)|
        (((ubyte *)&n)[1] << 16)|
        (((ubyte *)&n)[2] << 8)|
        (((ubyte *)&n)[3])
    );
}

ulong htonl68(ulong n)
{
    ulong v;
    ((ubyte *)&v)[0] = n >> 24;
    ((ubyte *)&v)[1] = n >> 16;
    ((ubyte *)&v)[2] = n >> 8;
    ((ubyte *)&v)[3] = n;
    return(v);
}


int DoOption(short ac,char *av[],char *ops,long args)
{
    register short i;
    short j;

    for (i = j = 1; i < ac; ++i) {
        register char *ptr = av[i];
        if (*ptr != '-') {
            av[j++] = av[i];
            continue;
        }
        while (*++ptr) {
            register char *op;
            long **ap = (long **)&args;
            short isshort;

            for (op = ops; *op && *op != *ptr;) {
                if (*op == *ptr)
                    break;
                if (*++op == '%') {
                    while (*op && *op != 's' && *op != 'd')
                        ++op;
                    if (*op)
                        ++op;
                }
                if (*op == ',')     /*  optional ,  */
                    ++op;
                ++ap;
            }
            if (*op == 0)
                return(-1);
            if (op[1] != '%') {
                *(short *)*ap = 1;
                ++ap;
                continue;
            }
            op += 2;
            isshort = 1;
            while (*op && *op != 's' && *op != 'd') {
                switch(*op) {
                case 'h':
                    isshort = 1;
                    break;
                case 'l':
                    isshort = 0;
                    break;
                default:
                    return(-1);
                }
                ++op;
            }
            switch(*op) {
            case 's':
                if (ptr[1]) {
                    *(char **)*ap = ptr + 1;
                    ptr = "\0";
                } else {
                    *(char **)*ap = av[++i];
                }
                break;
            case 'd':
                if (isshort)
                    *(short *)*ap = atoi(++ptr);
                else
                    *(long *)*ap = atoi(++ptr);
                while (*ptr >= '0' && *ptr <= '9')
                    ++ptr;
                break;
            default:
                return(-1);
            }
        }
    }
    return(j);
}
