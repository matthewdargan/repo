#ifndef _9PCLIENT_H
#define _9PCLIENT_H

typedef struct Cfsys Cfsys;
typedef struct Cfid Cfid;
struct Cfsys {
	u64 fd;
	u32 msize;
	u32 nexttag;
	u32 nextfid;
	Cfid *root;
};

struct Cfid {
	u32 fid;
	u32 mode;
	Qid qid;
	u64 offset;
	Cfsys *fs;
};

readonly static u32 omodetab[8] = {0, OEXEC, OWRITE, ORDWR, OREAD, OEXEC, ORDWR, ORDWR};

static Cfsys *fsinit(Arena *a, u64 fd);
static Cfsys *fs9mount(Arena *a, u64 fd, String8 aname);
static void fs9unmount(Arena *a, Cfsys *fs);
static b32 fsversion(Arena *a, Cfsys *fs, u32 msize);
static Cfid *fsauth(Arena *a, Cfsys *fs, String8 uname, String8 aname);
static Cfid *fsattach(Arena *a, Cfsys *fs, u32 afid, String8 uname, String8 aname);
static void fsclose(Arena *a, Cfid *fid);
static Cfid *fswalk(Arena *a, Cfid *fid, String8 path);
static b32 fsfcreate(Arena *a, Cfid *fid, String8 name, u32 mode, u32 perm);
static Cfid *fscreate(Arena *a, Cfsys *fs, String8 name, u32 mode, u32 perm);
static b32 fsfremove(Arena *a, Cfid *fid);
static b32 fsremove(Arena *a, Cfsys *fs, String8 name);
static b32 fsfopen(Arena *a, Cfid *fid, u32 mode);
static Cfid *fs9open(Arena *a, Cfsys *fs, String8 name, u32 mode);
static s64 fspread(Arena *a, Cfid *fid, void *buf, u64 n, s64 offset);
static s64 fsread(Arena *a, Cfid *fid, void *buf, u64 n);
static s64 fsreadn(Arena *a, Cfid *fid, void *buf, u64 n);
static s64 fspwrite(Arena *a, Cfid *fid, void *buf, u64 n, s64 offset);
static s64 fswrite(Arena *a, Cfid *fid, void *buf, u64 n);
static s64 fsdirread(Arena *a, Cfid *fid, Dirlist *list);
static s64 fsdirreadall(Arena *a, Cfid *fid, Dirlist *list);
static Dir fsdirfstat(Arena *a, Cfid *fid);
static Dir fsdirstat(Arena *a, Cfsys *fs, String8 name);
static b32 fsdirfwstat(Arena *a, Cfid *fid, Dir d);
static b32 fsdirwstat(Arena *a, Cfsys *fs, String8 name, Dir d);
static b32 fsaccess(Arena *a, Cfsys *fs, String8 name, u32 mode);
static s64 fsseek(Arena *a, Cfid *fid, s64 offset, u32 type);

#endif /* _9PCLIENT_H */
