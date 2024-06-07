
/*
 *  SUBS.C
 *
 *	DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
 *
 *	Support subroutines
 *
 */

#include "dnet.h"

/*
 *   WRITESTREAM()
 *
 *	Queues new SCMD_?? level commands to be sent
 */

WriteStream(sdcmd, buf, len, chan)
ubyte *buf;
uword chan;
{
    register XIOR *ior = (XIOR *)malloc(sizeof(XIOR));

    if (DDebug)
	printf("-writestr cmd %ld (%ld bytes chan %ld)\n", sdcmd, len, chan);

    ior->io_Data = (ubyte *)malloc(len);
    ior->io_Length = len;
    ior->io_Actual = 0;
    ior->io_Command = sdcmd;
    ior->io_Error = 0;
    ior->io_Channel = chan;
    ior->io_Pri = (chan > MAXCHAN) ? 126 : Chan[chan].pri;
    bcopy(buf, ior->io_Data, len);
    Enqueue(&TxList, ior);
    /*
     *	REMOVED 21 SEPT 1988
     * do_wupdate();
     */
}

/*
 *  ALLOC_CHANNEL()
 *
 *	Allocate a free channel.  Used in SCMD_OPEN and SCMD_ACKCMD
 */

alloc_channel()
{
    static ulong ran = 13;
    register uword i;

    ran = ((ran * 13) + 1) ^ (ran >> 9) + time(0);
    for (i = ran % MAXCHAN; i < MAXCHAN; ++i) {
	if (Chan[i].state == 0)
	    return(i);
    }
    for (i = ran % MAXCHAN; i < MAXCHAN; --i) {
	if (Chan[i].state == 0)
	    return(i);
    }
    return(-1);
}

/*
 *    Remove all nodes with the given channel ID.
 */

ClearChan(list, chan, all)
LIST *list;
uword chan;
{
    register XIOR *io, *in;

    for (io = (XIOR *)list->lh_Head; io != (XIOR *)&list->lh_Tail; io = in) {
	in = (XIOR *)io->io_Node.ln_Succ;
	if (io->io_Channel == chan) {
	    if (all || io->io_Command == SCMD_DATA) {
		io->io_Node.ln_Succ->ln_Pred = io->io_Node.ln_Pred;
		io->io_Node.ln_Pred->ln_Succ = io->io_Node.ln_Succ;
		free(io->io_Data);
		free(io);
	    }
	}
    }
}

/*
 *  Queue a packet into a prioritized list.  FIFO is retained for packets
 *  of the same priority.  This implements one level of channel priorities,
 *  before the packets actually get queued to the network.  Since up to
 *  4 packets might be queued (200 * 4 = 800 bytes of data or 4 seconds @
 *  2400 baud), a second level of prioritization will also reduce the
 *  physical packet size when two channels at relatively large differing
 *  priorities are in use.
 *
 *	These and other list routines compatible with Amiga list routines.
 */

Enqueue(list, ior)
LIST *list;
XIOR *ior;
{
    register XIOR *io;
    char pri = ior->io_Pri;

    io = (XIOR *)list->lh_Head;
    while (io != (XIOR *)&list->lh_Tail) {
	if (pri > io->io_Pri)
	    break;
	io = (XIOR *)io->io_Node.ln_Succ;
    }
    ior->io_Node.ln_Succ = (NODE *)io;
    ior->io_Node.ln_Pred = io->io_Node.ln_Pred;
    ior->io_Node.ln_Succ->ln_Pred = (NODE *)ior;
    ior->io_Node.ln_Pred->ln_Succ = (NODE *)ior;
}

AddTail(list, node)
LIST *list;
NODE *node;
{
    node->ln_Succ = (NODE *)&list->lh_Tail;
    node->ln_Pred = list->lh_TailPred;
    node->ln_Succ->ln_Pred = node;
    node->ln_Pred->ln_Succ = node;
}

AddHead(list, node)
LIST *list;
NODE *node;
{
    node->ln_Succ = list->lh_Head;
    node->ln_Pred = (NODE *)list;
    node->ln_Succ->ln_Pred = node;
    node->ln_Pred->ln_Succ = node;
}

ubyte *
RemHead(list)
LIST *list;
{
    NODE *node;

    node = list->lh_Head;
    if (node->ln_Succ == NULL)
	return(NULL);
    node->ln_Succ->ln_Pred = node->ln_Pred;
    node->ln_Pred->ln_Succ = node->ln_Succ;
    return((ubyte *)node);
}

NewList(list)
LIST *list;
{
    list->lh_Head = (NODE *)&list->lh_Tail;
    list->lh_Tail = NULL;
    list->lh_TailPred = (NODE *)&list->lh_Head;
}

GetNext(node)
NODE *node;
{
    register NODE *next = node->ln_Succ;
    if (*(long *)next)
	return((long)next);
    return(NULL);
}

/*
 *  CHKBUF
 *
 *	Checksum a buffer.  Uses a simple, but supposedly very good
 *	scheme.
 */

chkbuf(buf, bytes)
register ubyte *buf;
register uword bytes;
{
    register uword i;
    register ubyte c1,c2;

    for (i = c1 = c2 = 0; i < bytes; ++i) {
	c1 += buf[i];
	c2 += c1;
    }
    c1 = -(c1 + c2);
    return((c1<<8)|c2);
}

/*
 *   Write timeout signal handler.
 */

sigwto()
{
    WTimedout = 1;
}

TimerOpen()
{
    static struct sigvec SA = { sigwto, 0, 0 };
    sigvec(SIGALRM, &SA, NULL);
}

TimerClose()
{
    signal(SIGALRM, SIG_IGN);
}

WTimeout(secs)
{
    static struct itimerval itv;
    struct itimerval ov;
    long mask;

    itv.it_value.tv_sec = secs;
    itv.it_value.tv_usec= 0;

    mask = sigblock(sigmask(SIGALRM));
    setitimer(ITIMER_REAL, &itv, &ov);
    Wto_act = 1;
    WTimedout = 0;
    sigsetmask(mask);
    if (DDebug)
	fprintf(stderr, "WTimeout set\n");
}

dneterror(str)
char *str;
{
    extern int errno;
    register short i;
    int er = errno;

    NetClose();
    TimerClose();
    if (str)
	fprintf(stderr, "%s %d\n", str, er);
    else
	fprintf(stderr, "-end-\n");
    exit(1);
}

/*
 *    setenv(name, str).  name must be of the form "NAME="
 */

setenv(name, str)
char *name;
char *str;
{
    extern char **environ;
    static char **elist;
    static int elen;
    char *ptr;
    int i, len;

    len = strlen(name);
    if (elist == NULL) {
	for (i = 0; environ[i]; ++i);
	elist = (char **)malloc((i+3)*sizeof(char *));
	elen = i + 3;
	bcopy(environ, elist, i*sizeof(char *));
	environ = elist;
    }
    for (i = 0; elist[i]; ++i) {
	if (strncmp(elist[i], name, len) == 0)
	    break;
    }
    if (i == elen) {
	elen += 4;
	elist = environ = (char **)realloc(elist, elen*sizeof(char *));
    }
    ptr = (char *)malloc(len + strlen(str) + 1);
    sprintf(ptr, "%s%s", name, str);
    if (elist[i]) {
	elist[i] = ptr;
    } else {
	elist[i] = ptr;
	elist[i+1] = NULL;
	elen = i + 1;
    }
}

void
startserver(port)
uword port;
{
    char dir[MAXPATHLEN];
    struct passwd pw_info;
    FILE *fi;

    if (!port)
	return;
    if (getenv("DNETDIR")) {
	strcpy(dir, getenv("DNETDIR"));
	strcat(dir, "dnet.servers");
	if (fi = fopen(dir, "r")) {
	    if (scan_for_server(fi, port))
		return;
	}
    }
    pw_info = *getpwuid(getuid());
    strcpy(dir, pw_info.pw_dir);
    strcat(dir, "/.dnet/dnet.servers");
    if (fi = fopen(dir, "r")) {
	if (scan_for_server(fi, port))
	    return;
    }
    /*
     *	LAST TRY
     */
    if (fi = fopen(LASTTRYDNETSERVERS, "r")) {
	if (scan_for_server(fi, port))
	    return;
    }
    fprintf(stderr, "Unable to find one of (1) dnet.servers or (2) server\n");
    fprintf(stderr, "entry for port %d\n", port);
    fflush(stderr);
    return;
}

scan_for_server(fi, port)
FILE *fi;
short port;
{
    char buf[256];
    char path[MAXPATHLEN];
    char cdir[MAXPATHLEN];
    long portno;
    short found = 0;
    void checktilda();

    while (fgets(buf, 256, fi)) {
	if (sscanf(buf, "%ld %s %s", &portno, path, cdir) == 3) {
	    checktilda(path);
	    checktilda(cdir);
	    if (portno == port) {
		if (!fork()) {
		    int i;
		    fclose(fi);
		    setuid(getuid());
		    signal(SIGHUP, SIG_DFL);
		    signal(SIGINT, SIG_DFL);
		    signal(SIGQUIT, SIG_DFL);
		    signal(SIGTERM, SIG_DFL);
		    signal(SIGCHLD, SIG_DFL);
		    signal(SIGTSTP, SIG_IGN);
		    ioctl(open("/dev/tty", 2), TIOCNOTTY, NULL);
		    i = open("/dev/null", O_RDWR, 0);
		    dup2(i, 0);
		    dup2(i, 1);
		    for (i = 3; i < 256; ++i)
			close(i);
		    sprintf(buf, "server.%ld.%ld", port, getuid());
		    execl(path, buf, cdir, NULL);
		    fprintf(stderr, "Unable to exec server: %s\n", path);
    		    fflush(stderr);
		    _exit(1);
		}
		sleep(4);    /* is a hack */
		found = 1;
		break;
	    }
	}
    }
    fclose(fi);
    return(found);
}

void
checktilda(buf)
char *buf;
{
    if (buf[0] == '~') {
	short bindex = 1;
	short pathlen;
	struct passwd pw_info, *pw;

	pw_info.pw_dir = getenv("HOME");
	if (buf[1] && buf[1] != '/') {
	    char username[128];
	    while (buf[bindex] && buf[bindex] != '/')
		++bindex;
	    bcopy(buf+1, username, bindex-1);
	    username[bindex-1] = 0;
	    if (pw = getpwnam(username)) {
		pw_info = *pw;
	    } else {
		fprintf(stderr, "Unable to find password entry for %s\n",
		    username
		);
		fprintf(stderr, "passing /tmp as dir for server");
    		fflush(stderr);
		pw_info.pw_dir = "/tmp";
	    }
	}

	/*
	 * ~[username]<rest of path>	 ->   <basedir><rest of path>
	 */

	pathlen = strlen(pw_info.pw_dir);
	bcopy(buf + bindex, buf + pathlen, strlen(buf + bindex) + 1);
	bcopy(pw_info.pw_dir, buf, pathlen);
    }
    fflush(stderr);
}

