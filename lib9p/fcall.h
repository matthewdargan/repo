#ifndef FCALL_H
#define FCALL_H

readonly static String8 version9p = str8litc("9P2000");

#define MAXWELEM 16

typedef struct Qid Qid;
struct Qid {
	u32 type;
	u32 vers;
	u64 path;
};

typedef struct Fcall Fcall;
struct Fcall {
	u32 type;
	u32 tag;
	u32 fid;
	u32 msize;               /* Tversion, Rversion */
	String8 version;         /* Tversion, Rversion */
	u32 oldtag;              /* Tflush */
	String8 ename;           /* Rerror */
	Qid qid;                 /* Rattach, Ropen, Rcreate */
	u32 iounit;              /* Ropen, Rcreate */
	Qid aqid;                /* Rauth */
	u32 afid;                /* Tauth, Tattach */
	String8 uname;           /* Tauth, Tattach */
	String8 aname;           /* Tauth, Tattach */
	u32 perm;                /* Tcreate */
	String8 name;            /* Tcreate */
	u32 mode;                /* Topen, Tcreate */
	u32 newfid;              /* Twalk */
	u32 nwname;              /* Twalk */
	String8 wname[MAXWELEM]; /* Twalk */
	u32 nwqid;               /* Rwalk */
	Qid wqid[MAXWELEM];      /* Rwalk */
	s64 offset;              /* Tread, Twrite */
	u32 count;               /* Tread, Rread, Twrite, Rwrite */
	String8 data;            /* Rread, Twrite */
	String8 stat;            /* Rstat, Twstat */
};

typedef struct Dir Dir;
struct Dir {
	u32 type; /* server type */
	u32 dev;  /* server subtype */
	Qid qid;
	u32 mode;
	u32 atime;
	u32 mtime;
	u64 len;
	String8 name;
	String8 uid;
	String8 gid;
	String8 muid; /* last modifier */
};

enum {
	Tversion = 100,
	Rversion = 101,
	Tauth = 102,
	Rauth = 103,
	Tattach = 104,
	Rattach = 105,
	Rerror = 107,
	Tflush = 108,
	Rflush = 109,
	Twalk = 110,
	Rwalk = 111,
	Topen = 112,
	Ropen = 113,
	Tcreate = 114,
	Rcreate = 115,
	Tread = 116,
	Rread = 117,
	Twrite = 118,
	Rwrite = 119,
	Tclunk = 120,
	Rclunk = 121,
	Tremove = 122,
	Rremove = 123,
	Tstat = 124,
	Rstat = 125,
	Twstat = 126,
	Rwstat = 127,
};

#define putb1(p, v) ((p)[0] = (u8)(v))
#define putb2(p, v) (*(u16 *)(p) = fromleu16(v))
#define putb4(p, v) (*(u32 *)(p) = fromleu32(v))
#define putb8(p, v) (*(u64 *)(p) = fromleu64(v))
#define getb1(p) ((u32)(p)[0])
#define getb2(p) ((u32)fromleu16(*(u16 *)(p)))
#define getb4(p) ((u32)fromleu32(*(u32 *)(p)))
#define getb8(p) ((u64)fromleu64(*(u64 *)(p)))
#define DIRFIXLEN (2 + 2 + 4 + 1 + 4 + 8 + 4 + 4 + 4 + 8)
#define NOTAG 0xffff
#define NOFID 0xffffffff
#define IOHDRSZ 24

static u32 fcallsize(Fcall fc);
static String8 fcallencode(Arena *a, Fcall fc);
static Fcall fcalldecode(String8 msg);
static u32 dirsize(Dir d);
static String8 direncode(Arena *a, Dir d);
static Dir dirdecode(String8 msg);
static String8 read9pmsg(Arena *a, u64 fd);

#endif /* FCALL_H */
