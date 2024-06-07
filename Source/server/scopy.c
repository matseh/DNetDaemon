
/*
 *	SCOPY.C
 *
 *	DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
 *
 *	Remote file copy server (putfiles is the client program)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "servers.h"
#include "../lib/dnetlib.h"
#include "../lib/dnetutil.h"

int putdir(int chan, char *dirname);
int putfile(int chan, char *name, int len);

char Buf[4096];

void chandler(int signal)
{
    int stat;
    struct rusage rus;
    while (wait3(&stat, WNOHANG, &rus) > 0);
}

int main(int ac,char *av[])
{
    CHANN *chann = DListen(PORT_FILECOPY);
    int fd;
    int n;
    char buf[256];
    extern int errno;

    Log(LogLevelDebug, "SCOPY START");
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
        Log(LogLevelDebug, "SCOPY CONNECT");
	if (fork() == 0) {
	    putdir(fd, "."); 
	    _exit(1);
	}
	close(fd);
    }
    perror("SCOPY");
}

int putdir(int chan, char *dirname)
{
    struct stat stat;
    char olddir[256];
    char co, nl, name[128];
    uint32_t len;
    int ret = -1;

    getwd(olddir);
    if (lstat(dirname, &stat) >= 0 && !(stat.st_mode & S_IFDIR)) {
	char rc = 'N';
	gwrite(chan, &rc, 1);
	Log(LogLevelWarning, "SCOPY: Unable to cd to dir '%s'", dirname);
	return(1);
    }
    if (chdir(dirname) < 0) {
	if (mkdir(dirname, 0777) < 0 || chdir(dirname) < 0) {
	    char rc = 'N';
	    Log(LogLevelWarning, "SCOPY: Unable to create directory '%s'", dirname);
	    gwrite(chan, &rc, 1);
	    return(1);
	}
    }
    co = 'Y';
    gwrite(chan, &co, 1);
    while (ggread(chan, &co, 1) == 1) {
	if (ggread(chan, &nl, 1) != 1 || ggread(chan, name, nl) != nl)
	    break;
	if (ggread(chan, (char *) &len, 4) != 4)
	    break;
	len = ntohl68(len);
	switch(co) {
	case 'C':
	    co = 'Y';
    	    if (chdir(name) < 0) {
		if (mkdir(name, 0777) < 0 || chdir(name) < 0)  {
		    co = 'N';
	            Log(LogLevelWarning, "SCOPY: Unable to create directory '%s'", 
			dirname);
		}
	    }
	    gwrite(chan, &co, 1);
	    break;
	case 'W':
	    if (putfile(chan, name, len) < 0) {
		ret = -1;
		Log(LogLevelWarning, "SCOPY: Failure on file %.*s", len, name);
		goto fail;
	    }
	    break;
	case 'X':
	    if (putdir(chan, name) < 0) {
		ret = -1;
		goto fail;
	    }
	    break;
	case 'Y':
	    ret = 1;
	    co = 'Y';
	    gwrite(chan, &co, 1);
	    goto fail;
	default:
	    co = 'N';
	    gwrite(chan, &co, 1);
	    break;
	}
    }
fail:
    chdir(olddir);
    return(ret);
}

int putfile(int chan, char *name, int len)
{
    long fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0666);
    long n, r;
    char rc;

    if (fd < 0) {
	rc = 'N';
	gwrite(chan, &rc, 1);
	return(0);
    }
    rc = 'Y';
    gwrite(chan, &rc, 1);
    while (len) {
	r = (len > sizeof(Buf)) ? sizeof(Buf) : len;
	n = ggread(chan, Buf, r);
	if (n != r)
	    break;
        if (write(fd, Buf, n) != n)
	    break;
	len -= n;
    }
    close(fd);
    if (len) {
	unlink(name);
	return(-1);
    }
    rc = 'Y';
    gwrite(chan, &rc, 1);
    return(0);
}

