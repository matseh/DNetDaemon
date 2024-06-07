
/*
 * FILES.C
 *
 *      DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
 *
 *      handles actions on a per file descriptor basis, including accepting
 *      new connections, closing old connections, and transfering data
 *      between connections.
 */

#include <string.h>
#include <unistd.h>
#include "dnet.h"
#include "../lib/dnetutil.h"

void do_open1(int n_notused, int fd);
void do_openwait(int n, int fd);

/*
 *  new connection over master port... open request.  read two byte port
 *  number, allocate a channel, and send off to the remote
 */

void do_localopen(int n, int fd)
{
    struct sockaddr sa;
    socklen_t addrlen = sizeof(sa);
    int s;
    uword chan;

    Log(LogLevelDebug, "DO_LOCALOPEN %d %d\n", n, fd);
    while ((s = accept(fd, &sa, &addrlen)) >= 0) {
        chan = alloc_channel();
        fcntl(s, F_SETFL, FNDELAY);
        Log(LogLevelDebug, " ACCEPT: %d on channel %d ", s, chan);
        if (chan == 0xFFFF) {
            ubyte error = 1;
            gwrite(s, &error, 1);
            close(s);
            Log(LogLevelDebug, "(no channels)\n");
            continue;
        } 
        Fdstate[s] = do_open1;
        FdChan[s] = chan;
        FD_SET(s, &Fdread);
        FD_SET(s, &Fdexcept);
        Chan[chan].fd = s;
        Chan[chan].state = CHAN_LOPEN;
        Log(LogLevelDebug, "(State = CHAN_LOPEN)\n");
    }
}

void do_open1(int n_notused, int fd)
{
    uword port;
    char  trxpri[2];
    uword chan = FdChan[fd];
    COPEN co;
    int n;

    Log(LogLevelDebug, "DO_OPEN %d %d on channel %d  ", n, fd, chan);
    for (;;) {
        n = read(fd, &port, 2);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EWOULDBLOCK)
                return;
        }
        read(fd, trxpri, 2);
        if (n != 2)
            dneterror("do_open1: unable to read 2 bytes");
        break;
    }
    Log(LogLevelDebug, "Port %d\n", port);
    co.chanh = chan >> 8;
    co.chanl = chan;
    co.porth = port >> 8;
    co.portl = port;
    co.error = 0;
    co.pri   = trxpri[1];
    Chan[chan].port = port;
    Chan[chan].pri = 126;
    WriteStream(SCMD_OPEN, &co, sizeof(co), chan);
    Chan[chan].pri = trxpri[0];
    Fdstate[fd] = do_openwait;
    Log(LogLevelDebug, " Newstate = openwait\n");
}

void do_openwait(int n, int fd)
{
    ubyte buf[32];
    Log(LogLevelDebug, "************ ERROR DO_OPENWAIT %d %d\n", n, fd);
    n = read(fd, buf, 32);
    Log(LogLevelDebug, "    OPENWAIT, READ %d bytes\n", n);
    Log(LogLevelDebug, "openwait:read: %s\n", strerror(errno));
}

void do_open(int nn, int fd)
{
    extern void nop();
    char buf[256];
    uword chan = FdChan[fd];
    int n;

    n = read(fd, buf, sizeof(buf));
    Log(LogLevelDebug, "DO_OPEN %d %d, RECEIVE DATA on chan %d (%d by)\n",
            nn, fd, chan, n);
    Log(LogLevelDebug, " fd, chanfd %d %d\n", fd, Chan[chan].fd);
    Log(LogLevelDebug, "open:read: %s\n", strerror(errno));
    if (n == 0 || nn == 2) {    /* application closed / exception cond */
        CCLOSE cc;

        Log(LogLevelDebug, " DO_OPEN: REMOTE EOF, channel %d\n", chan);

        cc.chanh = chan >> 8;
        cc.chanl = chan;
        WriteStream(SCMD_CLOSE, &cc, sizeof(CCLOSE), chan);
        Chan[chan].state = CHAN_CLOSE;
        Chan[chan].flags |= CHANF_LCLOSE;
        if (Chan[chan].flags & CHANF_RCLOSE) {
            ;
            /* should never happen
            int fd = Chan[chan].fd;
            Chan[chan].state = CHAN_FREE;
            Chan[chan].fd = -1;
            Fdstate[fd] = nop;
            FD_CLR(fd, &Fdread);
            FD_CLR(fd, &Fdexcept);
            close(fd);
            */
        } else {
            FD_CLR(fd, &Fdread);
            FD_CLR(fd, &Fdexcept);
        }
    }
    if (n > 0) {
        WriteStream(SCMD_DATA, buf, n, chan);
    }
}

