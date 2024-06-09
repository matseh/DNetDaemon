
/*
 *  NET.C
 *
 *      DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
 *
 *  NetWork raw device interface.  Replace with whatever interface you
 *  want.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sgtty.h>
#include <termios.h>
#include <unistd.h>
#include "dnet.h"
#include "../lib/dnetutil.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

static int netFd = -1;

static struct termios originalTtyAttributes;

void RcvInt(void)
{
    int n = read(netFd, RcvBuf + RcvData, RCVBUF - RcvData);
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

static int configureSerialDevice(const char *serialDevicePath, int serialDeviceFd, int baudRate)
{
    if(ioctl(serialDeviceFd, TIOCEXCL) == -1)
    {
        Log(LogLevelError, "Error setting TIOCEXCL on %s: %s (%d).\n", serialDevicePath, strerror(errno), errno);
    }
    else if(tcgetattr(serialDeviceFd, &originalTtyAttributes) == -1)
    {
        Log(LogLevelError, "Error getting tty attributes on %s: %s (%d).\n", serialDevicePath, strerror(errno), errno);
    }
    else
    {
        struct termios ttyAttributes = originalTtyAttributes;

        cfmakeraw(&ttyAttributes);

        ttyAttributes.c_cflag = CS8|CREAD|CLOCAL;

        if(baudRate > 0)
        {
            cfsetspeed(&ttyAttributes, baudRate);                
        }

        if(tcsetattr(serialDeviceFd, TCSANOW, &ttyAttributes) == -1)
        {
            Log(LogLevelError, "Error setting tty attributes on %s: %s (%d).\n", serialDevicePath, strerror(errno), errno);
        }

        if(tcgetattr(serialDeviceFd, &ttyAttributes) == -1)
        {
            Log(LogLevelError, "Error getting tty attributes on %s: %s (%d).\n", serialDevicePath, strerror(errno), errno);
        }
        else
        {
            Log(LogLevelInfo, "Input baud rate is %d\n", (int) cfgetispeed(&ttyAttributes));
            Log(LogLevelInfo, "Output baud rate is %d\n", (int) cfgetospeed(&ttyAttributes));

            return TRUE;
        }
    }

    return FALSE;
}

int NetOpen(const char *serialDevicePath, int baudRate)
{
    int async = 1;

    if(serialDevicePath)
    {
        Log(LogLevelInfo, "Opening serial port %s.\n", serialDevicePath);

        netFd = open(serialDevicePath, O_RDWR|O_NOCTTY|O_NONBLOCK);

        if(netFd == -1)
        {
            Log(LogLevelError, "Error opening serial port %s: %s (%d).\n", serialDevicePath, strerror(errno), errno);
        }
        else if(!configureSerialDevice(serialDevicePath, netFd, baudRate))
        {
            close(netFd);

            netFd = -1;
        }
    }
    else
    {
        netFd = 0;

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
    }

    if(netFd != -1)
    {
        ioctl (netFd, FIONBIO, &async);
    }

    return netFd;
}

void NetClose(void)
{
    int async = 0;

    fchmod(netFd, Stat.st_mode);
    ioctl (netFd, FIONBIO, &async);

    if(netFd > 0)
    {
        tcsetattr(netFd, TCSANOW, &originalTtyAttributes);

        close(netFd);
    }
    else
    {
        /*
        ioctl (0, FIOASYNC, &async);
        */
        ioctl (0, TIOCGETP, &ttym);
        ttym.sg_flags &= ~RAW;
        ttym.sg_flags |= ECHO;
        ioctl (0, TIOCSETP, &ttym);        
    }
}

void NetWrite(ubyte *buf, int bytes)
{
    Log(LogLevelDebug, "NETWRITE %08lx %d\n", (unsigned long) buf, bytes);
    gwrite(netFd, buf, bytes);
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

