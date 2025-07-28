#ifndef FCALL_H
#define FCALL_H

readonly static String8 version9p = str8lit("9P2000");

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
	u64 offset;              /* Tread, Twrite */
	u32 count;               /* Tread, Rread, Twrite, Rwrite */
	String8 data;            /* Rread, Twrite */
	String8 stat;            /* Rstat, Twstat */
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

static u32 fcallsize(Fcall fc);
static String8 fcallencode(Arena *a, Fcall fc);
static b32 fcalldecode(Arena *a, String8 msg, Fcall *fc);

#endif /* FCALL_H */
