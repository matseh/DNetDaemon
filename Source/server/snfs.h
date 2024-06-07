#ifndef SNFS_H
#define SNFS_H

#include <stdint.h>

typedef struct {
    int32_t    ds_Days;
    int32_t    ds_Minute;
    int32_t    ds_Tick;
} __attribute__((packed)) STAMP;

typedef struct {
    int32_t    DirHandle;		/*  relative to directory (0=root)  */
    uint16_t   Modes;		/*  open modes			    */
} __attribute__((packed)) OpOpen;

typedef struct {
    int32_t    Handle;
    uint32_t   Prot;
    int32_t    Type;
    int32_t    Size;
    STAMP   Date;
} __attribute__((packed)) RtOpen;


typedef struct {
    int32_t    Handle; 	/*  file handle to read from	    */
    int32_t    Bytes;		/*  # of bytes to read		    */
} __attribute__((packed)) OpRead;

typedef struct {
    int32_t    Bytes;		/*  < 0 == error		    */
} __attribute__((packed)) RtRead;

typedef struct {
    int32_t   Handle; 	/*  file handle to read from	    */
    int32_t    Bytes;		/*  # of bytes to read		    */
} __attribute__((packed)) OpWrite;

typedef struct {
    int32_t    Bytes;		/*  < 0 == error		    */
} __attribute__((packed)) RtWrite;

typedef struct {
    int32_t    Handle;
} __attribute__((packed)) OpClose;

typedef struct {
    int32_t    Handle;
    int32_t    Offset;
    int32_t    How;
} __attribute__((packed)) OpSeek;

typedef struct {
    int32_t    OldOffset;
    int32_t    NewOffset;	    /*	-1 = error  */
} __attribute__((packed)) RtSeek;

typedef struct {
    int32_t    Handle;
} __attribute__((packed)) OpParent;

typedef RtOpen RtParent;

typedef struct {
    int32_t    DirHandle;
} __attribute__((packed)) OpDelete;

typedef struct {
    int32_t    Error;
} __attribute__((packed)) RtDelete;

typedef OpDelete OpCreateDir;
typedef RtParent RtCreateDir;

typedef struct {
    int32_t    Handle;
    int32_t    Index;
} __attribute__((packed)) OpNextDir;

typedef RtOpen RtNextDir;

typedef struct {
    int32_t    Handle;
} __attribute__((packed)) OpDup;

typedef RtOpen	RtDup;

typedef struct {
    int32_t    DirHandle1;
    int32_t    DirHandle2;
} __attribute__((packed)) OpRename;

typedef struct {
    int32_t    Error;
} __attribute__((packed)) RtRename;

#endif
