
/*
 *  DPRINT.C
 *
 *	DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved
 *
 *  send files to the other computer's printer.
 *
 *  DPRINT [files]	(stdin if no files specified)
 */

#include <stdio.h>
#include "../server/servers.h"

main(ac,av)
char *av[];
{
    register short i;
    long chan;

    chan = DOpen(NULL, PORT_PRINTER, -80, 126);
    if (chan < 0) {
	puts("Unable to connect");
	exit(1);
    }
    for (i = 1; i < ac; ++i) {
	FILE *fi;

	printf("%-20s ", av[i]);
	fflush(stdout);
	if (fi = fopen(av[i], "r")) {
	    printf("dumping ");
	    fflush(stdout);
	    dump_file(fi, chan);
	    fclose(fi);
	    puts("");
	}
    }
    if (ac == 1)
	dump_file(stdin, chan);
    close(chan);
}

dump_file(fi, chan)
FILE *fi;
{
    int n;
    char buf[256];

    while ((n = fread(buf, 1, sizeof(buf), fi)) > 0) {
	gwrite(chan, buf, n);
    }
}

