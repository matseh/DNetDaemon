#ifndef DNETLIB_H
#define DNETLIB_H

typedef unsigned short uword;
typedef unsigned long ulong;
typedef unsigned char ubyte;
typedef struct sockaddr SOCKADDR;

typedef struct {
    int s;
    uword port;
} CHANN;

CHANN *DListen(uword port);
void DUnListen(CHANN *chan);
int DAccept(CHANN *chan);
int DOpen(char *host,uword port,char txpri,char rxpri);
void DEof(int fd);

int gwrite(int fd, char *buf, int bytes);
int gread(int fd, char *buf, int bytes);
int ggread(int fd, char *buf, int bytes);

ulong ntohl68(ulong n);
ulong htonl68(ulong n);

#endif
