/*
 *  DNET.C
 *
 *      DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
 *
 *      Handles action on all active file descriptors and dispatches
 *      to the proper function in FILES.C
 *
 */

#include "dnet.h"
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "../lib/dnetutil.h"

void do_restart(void);

void handle_child(int signal)
{
    int stat;
    struct rusage rus;
    while (wait3(&stat, WNOHANG, &rus) > 0);
}

char *showselect(fd_set *ptr)
{
    static char buf[FD_SETSIZE+32];
    short i;

    for (i = 0; i < FD_SETSIZE; ++i) {
        buf[i] = (FD_ISSET(i, ptr)) ? '1' : '0';
    }
    buf[i] = 0;
    return(buf);
}

void loganddie(int signal)
{
    Log(LogLevelInfo, "\nHUPSIGNAL\n");
    Log(LogLevelInfo, "HUP, last error: %s", strerror(errno));
    Log(LogLevelInfo, "Last select return:\n");
    Log(LogLevelInfo, "  %s\n", showselect(&Fdread));
    Log(LogLevelInfo, "  %s\n", showselect(&Fdwrite));
    Log(LogLevelInfo, "  %s\n", showselect(&Fdexcept));
    Log(LogLevelInfo, "RcvData = %ld\n", (long) RcvData);
    Log(LogLevelInfo, "RChan/WChan = %ld/%ld\n", (long) RChan, (long) WChan);
    Log(LogLevelInfo, "RPStart = %ld\n", (long) RPStart);
    Log(LogLevelInfo, "WPStart = %ld\n", (long) WPStart);
    Log(LogLevelInfo, "WPUsed = %ld\n", (long) WPUsed);
    Log(LogLevelInfo, "RState = %ld\n", (long) RState);
    kill(0, SIGILL);
    exit(1);
}

#define SASIZE(sa)      (sizeof(sa)-sizeof((sa).sa_data)+strlen((sa).sa_data))

int main(int ac,char *av[])
{
    long sink_mask, dnet_mask;
    ubyte notdone;
    int netFd;
    int i;
    int baudRate = 0;

    const char *serialDevicePath = NULL;
    const char *dnetDir          = GetDnetDir();
    const char *logPath          = "DNET.LOG";

    for (i = 1; i < ac; ++i) {
        register char *ptr = av[i];
        if (*ptr != '-') {
            SetLogLevel(LogLevelDebug);
            Log(LogLevelInfo, "Debug mode on\n");
            setlinebuf(stderr);
            setlinebuf(stdout);
            continue;
        }
        ++ptr;
        switch(*ptr) {
            case 'b':
                if(av[i][2]) {
                    baudRate = atoi(&av[i][2]);
                } else if((i + 1) < ac) {
                    i++;
                    baudRate = atoi(av[i]);
                }
                break;
            case 'B':
                break;
            case 'D':
                if(av[i][2]) {
                    serialDevicePath = &av[i][2];
                } else if((i + 1) < ac) {
                    i++;
                    serialDevicePath = av[i];
                }
                logPath = NULL;
                break;
            case 'm':   /* Mode7 */
                Mode7 = atoi(ptr + 1);
                if (Mode7 == 0)
                    Log(LogLevelInfo, "8 bit mode selected\n");
                else
                    Log(LogLevelInfo, "7 bit mode selected\n");
                break;
            default:
                Log(LogLevelInfo, "Unknown option: %c\n", *ptr);
                printf("Unknown option: %c\n", *ptr);
                exit(1);
        }
    }

    if (chdir(dnetDir)) {
        mkdir(dnetDir, 0700);
        if (chdir(dnetDir)) {
            Log(LogLevelInfo, "Unable to create dir %s\n", dnetDir);
            exit(1);
        }
    }

    if(logPath) {
        dup2(open("DNET.LOG", O_WRONLY|O_CREAT|O_TRUNC, 0666), 2);
        setlinebuf(stderr);        
        Log(LogLevelInfo, "DNet startup\n");
        Log(LogLevelInfo, "Log file placed in %s\n", dnetDir);
    }

    /*signal(SIGINT, SIG_IGN);*/
    signal(SIGPIPE, SIG_IGN);
    /*signal(SIGQUIT, SIG_IGN);*/
    signal(SIGCHLD, handle_child);
    signal(SIGHUP, loganddie);

    bzero(Pkts,sizeof(Pkts));
    setlistenport("3");

    NewList(&TxList);

    netFd = NetOpen(serialDevicePath, baudRate); /* initialize network and interrupt driven read */

    if(netFd != -1) {
        Fdperm[netFd] = 1;
        Fdstate[netFd] = (void (*)(int,int)) RcvInt;
        FD_SET(netFd, &Fdread);
        FD_SET(netFd, &Fdexcept);

        Log(LogLevelInfo, "DNET RUNNING, Listenfd=%ld\n", (long) DNet_fd);
        
        TimerOpen();        /* initialize timers                            */

        do_netreset();
        do_restart();

        notdone = 1;
        while (notdone) {
            /*
             *    MAIN LOOP.  select() on all the file descriptors.  Set the
             *    timeout to infinity (NULL) normally.  However, if there is
             *    a pending read or write timeout, set the select timeout
             *    to 2 seconds in case they timeout before we call select().
             *    (i.e. a timing window).  OR, if we are in the middle of a
             *    read, don't use descriptor 0 and timeout according to
             *    the expected read length, then set the descriptor as ready.
             */

            fd_set fd_rd;
            fd_set fd_wr;
            fd_set fd_ex;
            struct timeval tv, *ptv;
            int err;

            fd_rd = Fdread;
            fd_wr = Fdwrite;
            fd_ex = Fdexcept;

            tv.tv_sec = 0;          /* normally wait forever for an event */
            tv.tv_usec= 0;
            ptv = NULL;
            if ((Rto_act || Wto_act)) {     /* unless timeout pending */
                ptv = &tv;
                tv.tv_sec = 2;
            }

            /*   ... or expecting data (don't just wait for one byte).
             *
             *   This is an attempt to reduce the CPU usage for the process.
             *   If we are expecting data over the serial line, then don't
             *   return from the select() even if data is available, but
             *   wait for the timeout period indicated before reading the
             *   data.  Don't wait more than 64 byte times or we may loose
             *   some data (the silo's are only so big.. like 128 bytes).
             *
             *   Currently we wait 1562uS/byte (1/10 second for 64 bytes)
             *   This number is used simply so we don't hog the cpu reading
             *   a packet.
             */

            if (RExpect) {
                ptv = &tv;
                tv.tv_usec= 1562L * ((RExpect < 64) ? RExpect : 64);
                tv.tv_sec = 0;
                FD_CLR(netFd, &fd_rd);
            }
            if (WReady) {   /* transmit stage has work to do */
                ptv = &tv;
                tv.tv_usec = 0;
                tv.tv_sec = 0;
            }
            err = select(FD_SETSIZE, &fd_rd, &fd_wr, &fd_ex, ptv);
            if (RExpect) {
                FD_SET(netFd, &fd_rd);   /* pretend data ready */
            }
            Log(LogLevelDebug, "SERR %ld %ld %08lx %08lx\n",
                (long) err, (long) errno, (long) RExpect, (long) ptv
            );

            if (RTimedout) {
                RTimedout = 0;
                do_rto();
            }
            if (WTimedout) {
                WTimedout = 0;
                Wto_act = 0;
                do_wto();
            }
            if (err < 0) {
                if (errno == EBADF) {
                    perror("select");
                    dneterror(NULL);
                }
            } else {
                register short i;
                register short j;
                register long mask;

                for (i = 0; i < FD_SETSIZE/NFDBITS; ++i) {
                    if ((mask = fd_ex.fds_bits[i])) {
                        for (j = i * NFDBITS; mask; (mask >>= 1),(++j)) {
                            if (mask & 1)
                                (*Fdstate[j])(2,j);
                        }
                    }
                    if ((mask = fd_wr.fds_bits[i])) {
                        for (j = i * NFDBITS; mask; (mask >>= 1),(++j)) {
                            if (mask & 1)
                                (*Fdstate[j])(1,j);
                        }
                    }
                    if ((mask = fd_rd.fds_bits[i])) {
                        for (j = i * NFDBITS; mask; (mask >>= 1),(++j)) {
                            if (mask & 1)
                                (*Fdstate[j])(0,j);
                        }
                    }
                }
            }
            if (RcvData)
                do_rnet();
            do_wupdate();
        }
    }

    dneterror(NULL);
}

void nop(int dummy0, int dummy1)
{
}

void do_netreset(void)
{
    register short i;
    register CHAN *ch;
    for (i = 0; i < FD_SETSIZE; ++i) {
        if (!Fdperm[i])
            Fdstate[i] = nop;
    }
    for (i = 0, ch = Chan; i < MAXCHAN; ++i, ++ch) {
        switch(ch->state) {
        case CHAN_OPEN:
        case CHAN_LOPEN:        /*  pending on network      */
        case CHAN_CLOSE:
            if (ch->fd >= 0) {
                FD_CLR(ch->fd, &Fdread);
                FD_CLR(ch->fd, &Fdexcept);
                Fdstate[ch->fd] = nop;
                close(ch->fd);
                ch->fd = -1;
                ch->state = CHAN_FREE;
                ch->flags = 0;
                --NumCon;
            }
            ClearChan(&TxList, i, 1);
            break;
        }
    }
    RPStart = 0;
    WPStart = 0;
    WPUsed  = 0;
    RState  = 0;
    RChan = 0;
    WChan = 0;
}

void do_restart(void)
{
    WriteRestart();
    Restart = 1;
}

void setlistenport(char *remotehost)
{
    static struct sockaddr_un sa;
    int s;
    extern void do_localopen();

    if (DNet_fd >= 0) {
        unlink(sa.sun_path);
        Fdstate[DNet_fd] = nop;
        Fdperm[DNet_fd] = 0;
        FD_CLR(DNet_fd, &Fdread);
        FD_CLR(DNet_fd, &Fdexcept);
        close(DNet_fd);
    }
#ifdef __APPLE__
    setenv("DNETHOST", remotehost, 1);
#else
    setenv("DNETHOST=", remotehost);
#endif
    sprintf(sa.sun_path, "DNET.%s", remotehost);
    unlink(sa.sun_path);
    sa.sun_family = AF_UNIX;

    s = socket(PF_UNIX, SOCK_STREAM, 0);
    /* fcntl(s, F_SETOWN, getpid()); */
    fcntl(s, F_SETFL,  FNDELAY);

    Log(LogLevelInfo, "Binding \"%s\"\n", sa.sun_path);

#ifdef __APPLE__
    if (bind(s, (struct sockaddr *) &sa, sizeof(sa.sun_family) + sizeof(sa.sun_len) + strlen(sa.sun_path) + 1) < 0) {
#else
    if (bind(s, (struct sockaddr *) &sa, sizeof(sa.sun_family) + strlen(sa.sun_path)) < 0) {
#endif
        perror("bind");
        exit(1);
    }
    if (listen(s, 5) < 0) {
        unlink(sa.sun_path);
        perror("listen");
        exit(1);
    }
    DNet_fd = s;
    Fdstate[DNet_fd] = do_localopen;
    Fdperm[DNet_fd] = 1;
    FD_SET(DNet_fd, &Fdread);
    FD_SET(DNet_fd, &Fdexcept);
}

