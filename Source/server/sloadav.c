
/*
 *	SLOADAV.C
 *
 *	DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
 *
 *	Reports the load average every 5 minutes or until the connection
 *	is closed.
 */

#include <stdio.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "servers.h"
#include "../lib/dnetlib.h"

void do_loadav(int fd);

void chandler(int signal)
{
    int stat;
    struct rusage rus;
    while (wait3(&stat, WNOHANG, &rus) > 0);
}

int main(int ac,char *av[])
{
    CHANN *chann = DListen(PORT_LOADAV);
    int fd;
    int n;
    char buf[256];
    extern int errno;

    if (av[1])
	chdir(av[1]);
    signal(SIGCHLD, chandler);
    signal(SIGPIPE, SIG_IGN);
    for (;;) {
	fd = DAccept(chann);
	if (fd < 0) {
	    if (errno == EINTR)
		continue;
	    break;
	}
	if (fork() == 0) {
	    do_loadav(fd);
	    close(fd);
	    _exit(1);
	}
	close(fd);
    }
    perror("SLOADAV");
}

void do_loadav(int fd)
{
    char dummy;
    char buf[256];
    FILE *fi;

    while (ggread(fd, &dummy, 1) == 1) {
	fi = popen("uptime", "r");
	if (fi == NULL)
	    break;
	if (fgets(buf, 256, fi)) {
	    dummy = strlen(buf);
	    buf[dummy-1] = 0;
	    gwrite(fd, &dummy, 1);
	    gwrite(fd, buf, dummy);
	}
	if (ferror(fi))
	    break;
	pclose(fi);
    }
}

