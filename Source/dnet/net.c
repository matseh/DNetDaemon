
/*
 *  NET.C
 *
 *      DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
 *
 *  NetWork raw device interface.  Replace with whatever interface you
 *  want.
 */

#include <sys/stat.h>
#include <sgtty.h>
#include <unistd.h>
#include "dnet.h"
#include "../lib/dnetutil.h"

void RcvInt(void)
{
    int n = read(0, RcvBuf + RcvData, RCVBUF - RcvData);
    int i;
    extern int errno;

    errno = 0;
    if (n >= 0)
        RcvData += n;
    if (n <= 0)         /* disallow infinite fast-timeout select loops */
        RExpect = 0;
    Log(LogLevelDebug, "read(%d,%d)\n", n, errno);
}

static struct sgttyb    ttym;
static struct stat      Stat;

void NetOpen(void)
{
    int async = 1;

    fstat(0, &Stat);
    fchmod(0, 0600);
    /*
    signal(SIGIO, RcvInt);
    */
    ioctl (0, TIOCGETP, &ttym);
    if (Mode7) {
        ttym.sg_flags &= ~RAW;
        ttym.sg_flags |= CBREAK;
    } else {
        ttym.sg_flags |= RAW;
        ttym.sg_flags &= ~CBREAK;
    }
    ttym.sg_flags &= ~ECHO;
    ioctl (0, TIOCSETP, &ttym);
    /*
    ioctl (0, FIOASYNC, &async);
    */
    ioctl (0, FIONBIO, &async);
}

void NetClose(void)
{
    int async = 0;

    fchmod(0, Stat.st_mode);
    ioctl (0, FIONBIO, &async);
    /*
    ioctl (0, FIOASYNC, &async);
    */
    ioctl (0, TIOCGETP, &ttym);
    ttym.sg_flags &= ~RAW;
    ttym.sg_flags |= ECHO;
    ioctl (0, TIOCSETP, &ttym);
}

void NetWrite(ubyte *buf, int bytes)
{
    Log(LogLevelDebug, "NETWRITE %08lx %d\n", (unsigned long) buf, bytes);
    gwrite(0, buf, bytes);
}

void gwrite(int fd, const void * const buffer, long bytes)
{
    const char *buf = buffer;
    register long n;

    Log(LogLevelDebug, "gread(fd=%d,buf=%p,bytes=%d)\n", fd, buf, bytes);

    while (bytes) {
        n = write(fd, buf, bytes);
        if (n > 0) {
            bytes -= n;
            buf += n;
            continue;
        }
        if (errno == EINTR)
            continue;
        if (errno == EWOULDBLOCK) {
            fd_set fd_wr;
            fd_set fd_ex;
            FD_ZERO(&fd_wr);
            FD_ZERO(&fd_ex);
            FD_SET(fd, &fd_wr);
            FD_SET(fd, &fd_ex);
            if (select(fd+1, NULL, &fd_wr, &fd_ex, NULL) < 0) {
                perror("select");
            }
            continue;
        }
        if (errno == EPIPE)
            return;
        dneterror("gwrite");
    }
}

