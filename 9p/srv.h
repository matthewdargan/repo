#ifndef SRV_H
#define SRV_H

typedef struct Fid Fid;
typedef struct Srv Srv;
struct Fid
{
	u32 fid;
	u32 omode;
	Qid qid;
	String8 uid;
	u64 offset;
	void *aux;
	Srv *srv;
};

typedef struct Req Req;
struct Req
{
	u32 tag;
	u32 responded;
	Fcall ifcall;
	Fcall ofcall;
	Fid *fid;
	Fid *newfid;
	Fid *afid;
	Req *oldreq;
	Req **flush;
	u32 nflush;
	String8 error;
	u8 *buf;
	u8 *rbuf;
	void *aux;
	Srv *srv;
};

struct Srv
{
	Arena *arena;
	u64 infd;
	u64 outfd;
	u32 msize;
	u8 *rbuf;
	u8 *wbuf;
	Fid **fidtab;
	u32 nfid;
	u32 maxfid;
	Req **reqtab;
	u32 nreq;
	u32 maxreq;
	u32 nexttag;
	void *aux;
	void (*start)(Srv *);
	void (*end)(Srv *);
	void (*auth)(Req *);
	void (*attach)(Req *);
	void (*flush)(Req *);
	void (*walk)(Req *);
	void (*open)(Req *);
	void (*create)(Req *);
	void (*read)(Req *);
	void (*write)(Req *);
	void (*stat)(Req *);
	void (*wstat)(Req *);
	void (*remove)(Req *);
	void (*destroyfid)(Fid *);
	void (*destroyreq)(Req *);
};

enum
{
	QTDIR = 0x80,       // type bit for directories
	QTAPPEND = 0x40,    // type bit for append only files
	QTEXCL = 0x20,      // type bit for exclusive use files
	QTMOUNT = 0x10,     // type bit for mounted channel
	QTAUTH = 0x08,      // type bit for authentication file
	QTTMP = 0x04,       // type bit for non-backed-up file
	QTSYMLINK = 0x02,   // type bit for symbolic link
	QTFILE = 0x00,      // type bits for plain file
	MAXERRORLEN = 256,  // maximum error string length
};

read_only static b32 debug9psrv = 1;
read_only static String8 Ebadoffset = str8litc("bad offset");
read_only static String8 Ebotch = str8litc("9P protocol botch");
read_only static String8 Ecreatenondir = str8litc("create in non-directory");
read_only static String8 Edupfid = str8litc("duplicate fid");
read_only static String8 Eduptag = str8litc("duplicate tag");
read_only static String8 Eisdir = str8litc("is a directory");
read_only static String8 Enocreate = str8litc("create prohibited");
read_only static String8 Enoremove = str8litc("remove prohibited");
read_only static String8 Enostat = str8litc("stat prohibited");
read_only static String8 Enotfound = str8litc("file not found");
read_only static String8 Enowstat = str8litc("wstat prohibited");
read_only static String8 Eperm = str8litc("permission denied");
read_only static String8 Eunknownfid = str8litc("unknown fid");
read_only static String8 Ewalknodir = str8litc("walk in non-directory");

static Srv *srvalloc(Arena *a, u64 infd, u64 outfd);
static void srvfree(Srv *srv);
static void srvrun(Srv *srv);
static void respond(Req *r, String8 err);
static Fid *allocfid(Srv *srv, u32 fid);
static Fid *lookupfid(Srv *srv, u32 fid);
static Fid *removefid(Srv *srv, u32 fid);
static void closefid(Fid *fid);
static Req *allocreq(Srv *srv, u32 tag);
static Req *lookupreq(Srv *srv, u32 tag);
static Req *removereq(Srv *srv, u32 tag);
static void closereq(Req *req);

#endif  // SRV_H
