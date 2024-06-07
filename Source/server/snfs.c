
/*
 *  SNFS.C       V1.1
 *
 *  DNET (c)Copyright 1988, Matthew Dillon, All Rights Reserved.
 *
 *  NETWORK FILE SYSTEM SERVER
 *
 *  Accepts connections to files or directories & read-write or dir-scan calls.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/dir.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include "servers.h"
#include "snfs.h"
#include "../lib/dnetlib.h"
#include "../lib/dnetutil.h"

#define MAXHANDLES      256

int OpenHandle(char *base,char *tail,int modes);
void CloseHandle(int h);
void NFs(int chan);
void AmigaToUnixPath(char *buf);
char *FDName(int h);
void ConcatPath(char *s1, char *s2,char *buf);
int DupHandle(int h);
int FDHandle(int h);
char *TailPart(char *path);
void SetDate(STAMP *date, time_t mtime);

int Chan;

typedef struct {
    short isopen;
    short fd;
    int   modes;
    int   remodes;
    long  pos;
    char  *name;
} HANDLE;

HANDLE Handle[MAXHANDLES];

void chandler(int signal)
{
    int stat;
    struct rusage rus;
    while (wait3(&stat, WNOHANG, &rus) > 0);
}

int main(int ac,char *av[])
{
    CHANN *chann;
    int fd;
    int n;
    char buf[1024];
    extern int errno;

    chann = DListen(PORT_NFS);

    if (av[1])
        chdir(av[1]);
    Log(LogLevelDebug, "RUNNING\n");
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
            NFs(fd);
            Log(LogLevelDebug, "CLOSING\n");
            _exit(1);
        }
        close(fd);
    }
    perror("NFS");
}

void NFs(int chan)
{
    OpenHandle("/", "", O_RDONLY); /* root */
    for (;;) {
        struct {
            char        cmd;
            unsigned char blen;
            uint32_t dlen;
        } __attribute__((packed)) Base;
        long bytes;
        union {
            OpOpen      Open;
            OpRead      Read;
            OpWrite     Write;
            OpClose     Close;
            OpSeek      Seek;
            OpParent    Parent;
            OpDelete    Delete;
            OpCreateDir CreateDir;
            OpDup       Dup;
            OpNextDir   NextDir;
            OpRename    Rename;
        } R;
        union {
            RtOpen      Open;
            RtRead      Read;
            RtWrite     Write;
            RtSeek      Seek;
            RtParent    Parent;
            RtDelete    Delete;
            RtCreateDir CreateDir;
            RtDup       Dup;
            RtNextDir   NextDir;
            RtRename    Rename;
        } W;
        long h;
        char buf[256];

        if (ggread(chan, (char *) &Base, sizeof(Base)) != sizeof(Base))
            break;

        Base.dlen = ntohl(Base.dlen);

        Log(LogLevelDebug, "command %02x %d %d\n", 
            Base.cmd, Base.blen, Base.dlen
        );
        if (ggread(chan, (char *) &R, Base.blen) != Base.blen)
            break;
        switch(Base.cmd) {
        case 'M':       /* create directory */
            {
                ggread(chan, buf, Base.dlen);
                AmigaToUnixPath(buf);
                mkdir(buf, 0777);
                Log(LogLevelDebug, "MakeDir %s\n", buf);
            }
            R.Open.DirHandle = R.CreateDir.DirHandle;
            /* FALL THROUGH */
        case 'P':
            if (Base.cmd == 'P') {
                char *name = FDName(ntohl(R.Parent.Handle));
                short i = strlen(name)-1;

                Log(LogLevelDebug, "Parent Dir of: %s\n", name);

                if (i >= 0 && name[i] == '/')   /* remove tailing /'s */
                    --i;
                if (i < 0) {
                    W.Open.Handle = -1;
                    gwrite(chan, (char *) &W.Open, sizeof(W.Open));
                    Log(LogLevelDebug, "NO PARENT\n");
                    break;
                }
                while (i >= 0 && name[i] != '/')  /* remove name */
                    --i;
                while (i >= 0 && name[i] == '/')  /* remove tailing /'s */
                    --i;
                ++i;
                if (i == 0) {   /* at root */
                    buf[i++] = '/';
                } 
                strncpy(buf, name, i);
                buf[i] = 0;
                Log(LogLevelDebug, "Parent Exists: %s\n", buf);
                R.Open.DirHandle = 0;
            }
            R.Open.Modes = htons(1005);
            /* FALL THROUGH */
        case 'O':       /*      open    */
            R.Open.DirHandle = ntohl(R.Open.DirHandle);
            R.Open.Modes     = ntohs(R.Open.Modes);

            if (Base.cmd == 'O')  {
                ggread(chan, buf, Base.dlen);
                AmigaToUnixPath(buf);
                Log(LogLevelDebug, "OPEN: %s %d\n", buf, Base.dlen);
            }
            if (R.Open.Modes == 1006)
                h = OpenHandle(FDName(R.Open.DirHandle),buf, 
                    O_CREAT|O_TRUNC|O_RDWR
                );
            else
                h = OpenHandle(FDName(R.Open.DirHandle),buf, O_RDWR);
                Log(LogLevelDebug, "Open h = %ld name = %s  modes=%d\n", 
                h, buf, R.Open.Modes
            );
            if (h >= 0) {
                struct stat stat;
                if (fstat(FDHandle(h), &stat) < 0)
                    perror("fstat");
                W.Open.Handle = htonl(h);
                W.Open.Prot = 0;
                W.Open.Type = (stat.st_mode & S_IFDIR) ? 1 : -1;
                Log(LogLevelDebug, "fstat type %d\n", W.Open.Type);
                W.Open.Type = htonl(W.Open.Type);
                W.Open.Size = htonl(stat.st_size);
                SetDate(&W.Open.Date, stat.st_mtime);
                gwrite(chan, (char *) &W.Open, sizeof(W.Open));
                if (Base.cmd == 'P') {  /* tag name */
                    char *tail = TailPart(buf);
                    unsigned char c = strlen(tail) + 1;

                    gwrite(chan, (char *) &c, 1);
                    gwrite(chan, tail, c);
                }
            } else {
                W.Open.Handle = -1;
                gwrite(chan, (char *) &W.Open, sizeof(W.Open));
            }
            break;
        case 'N':       /* next directory.  Scan beg. at index  */
            {
                R.NextDir.Handle = ntohl(R.NextDir.Handle);
                R.NextDir.Index  = ntohl(R.NextDir.Index);

                DIR *dir = opendir(FDName(R.NextDir.Handle));
                struct stat sbuf;
                struct direct *dp;
                long index = 0;
                char buf[1024];

                while (dir && index <= R.NextDir.Index + 2) {
                    if ((dp = readdir(dir)) == NULL)
                        break;
                    ++index;
                }
                if (dir)
                    closedir(dir);
                if (index <= R.NextDir.Index + 2) {
                    W.Open.Handle = -1;
                } else {
                    W.Open.Handle = htonl(index);
                    strcpy(buf, FDName(R.NextDir.Handle));
                    strcat(buf, "/");
                    strcat(buf, dp->d_name);
                    stat(buf, &sbuf);
                    W.Open.Prot = 0;
                    W.Open.Type = (sbuf.st_mode & S_IFDIR) ? 1 : -1;
                    Log(LogLevelDebug, "fstat type %d\n", W.Open.Type);
                    W.Open.Type = htonl(W.Open.Type);
                    W.Open.Size = htonl(sbuf.st_size);
                    SetDate(&W.Open.Date, sbuf.st_mtime);
                }
                gwrite(chan, (char *) &W.Open, sizeof(W.Open));
                if (W.Open.Handle != -1) {
                    unsigned char len = strlen(dp->d_name) + 1;
                    gwrite(chan, (char *) &len, 1);
                    gwrite(chan, dp->d_name, len);
                }
            }
            break;
        case 'r':       /*      RENAME  */
            {
                char tmp1[512];
                char tmp2[512];
                char buf1[1024];
                char buf2[1024];

                R.Rename.DirHandle1 = ntohl(R.Rename.DirHandle1);
                R.Rename.DirHandle2 = ntohl(R.Rename.DirHandle2);

                ggread(chan, buf, Base.dlen);
                strcpy(tmp1, buf);
                strcpy(tmp2, buf + strlen(buf) + 1);
                AmigaToUnixPath(tmp1);
                AmigaToUnixPath(tmp2);
                ConcatPath(FDName(R.Rename.DirHandle1), tmp1, buf1);
                ConcatPath(FDName(R.Rename.DirHandle2), tmp2, buf2);
                Log(LogLevelDebug, "Rename %s to %s\n", buf1, buf2);
                if (rename(buf1, buf2) < 0)
                    W.Rename.Error = htonl(1);
                else
                    W.Rename.Error = 0;
                gwrite(chan, (char *) &W.Rename.Error, sizeof(W.Rename.Error));
            }
            break;
        case 'd':       /*      DUP     */
            R.Dup.Handle = ntohl(R.Dup.Handle);

            h = DupHandle(R.Dup.Handle);
            if (h >= 0) {
                struct stat stat;
                if (fstat(FDHandle(h), &stat) < 0)
                    perror("fstat");
                W.Open.Handle = htonl(h);
                W.Open.Prot = 0;
                W.Open.Type = (stat.st_mode & S_IFDIR) ? 1 : -1;
                Log(LogLevelDebug, "fstat type %d\n", W.Open.Type);
                W.Open.Type = htonl(W.Open.Type);
                W.Open.Size = htonl(stat.st_size);
                SetDate(&W.Open.Date, stat.st_mtime);
            } else {
                W.Open.Handle = -1;
            }
            gwrite(chan, (char *) &W.Dup, sizeof(W.Dup));
            break;
        case 'R':       /*      READ    */
            R.Read.Handle = ntohl(R.Read.Handle);
            R.Read.Bytes  = ntohl(R.Read.Bytes);

            {
                int fd = FDHandle(R.Read.Handle);
                char *buf = malloc(R.Read.Bytes);

                ssize_t bytes = read(fd, buf, R.Read.Bytes);

                Log(LogLevelDebug, "h=%d fd %d Read %d  Result=%d\n", R.Read.Handle, fd, R.Read.Bytes, bytes);

                W.Read.Bytes = htonl(bytes);

                gwrite(chan, (char *) &W.Read, sizeof(W.Read));
                if (bytes > 0)
                    gwrite(chan, buf, bytes);
                free(buf);
            }
            break;
        case 'W':
            R.Write.Handle = ntohl(R.Write.Handle);
            R.Write.Bytes  = ntohl(R.Write.Bytes);
            {
                int fd = FDHandle(R.Write.Handle);
                char *buf = malloc(R.Write.Bytes);
                if (ggread(chan, buf, R.Write.Bytes) != R.Write.Bytes)
                    break;
                W.Write.Bytes = write(fd, buf, R.Write.Bytes);
                Log(LogLevelDebug, "h=%d fd %d Write %d  Result=%d\n", 
                    R.Write.Handle, fd, R.Write.Bytes, W.Write.Bytes
                );
                gwrite(chan, (char *) &W.Write, sizeof(W.Write));
                free(buf);
            }
            break;
        case 'C':
            R.Close.Handle = ntohl(R.Close.Handle);
            CloseHandle(R.Close.Handle);
            break;
        case 'S':
            R.Seek.Handle = ntohl(R.Seek.Handle);
            R.Seek.Offset = ntohl(R.Seek.Offset);
            R.Seek.How    = ntohl(R.Seek.How);
            {
                int fd = FDHandle(R.Seek.Handle);
                W.Seek.OldOffset = lseek(fd, 0, 1);
                W.Seek.NewOffset = lseek(fd, R.Seek.Offset, R.Seek.How);
                Log(LogLevelDebug, "h %d SEEK %d %d %d result = %d\n",
                    R.Seek.Handle, fd, R.Seek.Offset, R.Seek.How,
                    W.Seek.NewOffset
                );
                gwrite(chan, (char *) &W.Seek, sizeof(W.Seek));
            }
            break;
        case 'D':
            R.Delete.DirHandle = ntohl(R.Delete.DirHandle);
            {
                char buf2[1024];

                ggread(chan, buf, Base.dlen);    /* get name to delete */
                AmigaToUnixPath(buf);
                ConcatPath(FDName(R.Delete.DirHandle), buf, buf2);

                unlink(buf2);
                rmdir(buf2);
                Log(LogLevelDebug, "Delete %s\n", buf2);
                W.Delete.Error = 0;
                gwrite(chan, (char *) &W.Delete, sizeof(W.Delete));
            }
            break;
        default:
            exit(1);
            break;
        }
    }
}

int OpenHandle(char *base,char *tail,int modes)
{
    short i;
    int fd;
    char name[1024];

    ConcatPath(base, tail, name);
    for (i = 0; i < MAXHANDLES; ++i) {
        if (Handle[i].isopen == 0)
            break;
    }
    if (i == MAXHANDLES)
        return(-1);
    fd = open(name, modes, 0666);
    if (fd < 0 && (modes & O_RDWR) && !(modes & O_CREAT)) {
        modes &= ~O_RDWR;
        fd = open(name, modes);
    }
    Handle[i].name = strcpy(malloc(strlen(name)+1), name);
    Handle[i].fd = fd;
    Log(LogLevelDebug, "OpenHandle: %d = open %s %d\n", Handle[i].fd, name,modes);
    if (Handle[i].fd < 0)
        return(-1);
    Handle[i].modes = modes;
    Handle[i].remodes= modes & ~(O_TRUNC|O_CREAT);
    Handle[i].isopen = 1;
    return(i);
}

void CloseHandle(int h)
{
    Log(LogLevelDebug, " Close Handle %d\n", h);
    if (h >= 0 && h < MAXHANDLES && Handle[h].isopen) {
        if (Handle[h].fd >= 0)
            close(Handle[h].fd);
        Handle[h].fd = -1;
        Handle[h].isopen = 0;
        free(Handle[h].name);
    }
}

/*
 *  Insert ../ for / at beginning.
 */

void AmigaToUnixPath(char *buf)
{
    char *base = buf;
    Log(LogLevelDebug, "AmigaToUnixPath %s", buf);
    if (*buf == ':')
        *buf++ = '/';
    while (*buf == '/') {
        bcopy(buf, buf + 2, strlen(buf)+1);
        buf[0] = buf[1] = '.';
        buf += 3;
    }
    Log(LogLevelDebug, " TO %s\n", base);
}

void ConcatPath(char *s1, char *s2,char *buf)
{
    Log(LogLevelDebug, "ConCatPaths From '%s' '%s'\n", s1, s2);
    while (strncmp(s2, "../", 3) == 0) {        /* parent */
        ;
        break;
    }
    while (strncmp(s2, "./", 2) == 0) {         /* current */
        s2 += 2;
    }
    if (s2[0] == '/') {
        strcpy(buf, s2);
        return;
    }
    if (s1[0] == 0 && s2[0] == 0) {
        strcpy(buf, ".");
        return;
    }
    if (s1[0] == 0)
        s1 = ".";
    strcpy(buf, s1);
    if (s1[strlen(s1)-1] != '/')
        strcat(buf, "/");
    strcat(buf, s2);
    Log(LogLevelDebug, "ConCatPaths to %s\n", buf);
}

char *FDName(int h)
{
    Log(LogLevelDebug, "FDName(%d) =", h);
    if (h >= 0 && h < MAXHANDLES && Handle[h].isopen) {
        Log(LogLevelDebug, "%s\n", Handle[h].name);
        return(Handle[h].name);
    }
    Log(LogLevelDebug, "??\n");
    return(".");
}

int DupHandle(int h)
{
    short n = -1;
    if (h >= 0 && h < MAXHANDLES && Handle[h].isopen)
        n = OpenHandle(".",Handle[h].name, Handle[h].remodes & ~O_RDWR);
    return(n);
}

int FDHandle(int h)
{
    int fd = -1;
    if (h >= 0 && h < MAXHANDLES && Handle[h].isopen) {
        fd = Handle[h].fd;
        if (fd < 0) {
            Handle[h].fd = fd = open(Handle[h].name, Handle[h].remodes, 0666);
            if (fd >= 0 && !(Handle[h].modes & O_APPEND))
                lseek(fd, Handle[h].pos, 0);
        }
    }
    return(fd);
}

char *TailPart(char *path)
{
    register char *ptr = path + strlen(path) - 1;

    while (ptr >= path && *ptr != '/')
        --ptr;
    ++ptr;
    Log(LogLevelDebug, "TAILPART '%s' -> %s\n", path, ptr);
    return(ptr);
}

void SetDate(STAMP *date, time_t mtime)
{
    struct tm *tm = localtime(&mtime);
    long years = tm->tm_year;   /* since 1900   */
    long days;

    years += 300;                       /* since 1600                   */
    days = (years / 400) * 146097;      /* # days every four cents      */

    years = years % 400;

    /*
     *    First assume a leap year every 4 years, then correct for centuries.
     *    never include the 'current' year in the calculations.  Thus, year 0
     *    (a leap year) is included only if years > 0.
     */

    days += years * 365 + ((years+3) / 4);
        
    if (years <= 100)
        ;
    else if (years <= 200)              /* no leap 3 of 4 cent. marks   */
        days -= 1;
    else if (years <= 300)
        days -= 2;
    else
        days -= 3;
    days -= 138062;                     /* 1600 -> 1978                 */

    date->ds_Days  = htonl(days + tm->tm_yday);
    date->ds_Minute= htonl(tm->tm_min + tm->tm_hour * 60);
    date->ds_Tick  = htonl(tm->tm_sec * 50);
}
