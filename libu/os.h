#ifndef OS_H
#define OS_H

typedef struct Sysinfo Sysinfo;
struct Sysinfo
{
	u32 nprocs;
	u64 pagesz;
	u64 lpagesz;
};

typedef struct tm tm;
typedef struct timespec timespec;

static Sysinfo sysinfo;

static String8list osargs(Arena *a, int argc, char **argv);
static String8 readfile(Arena *a, String8 path);
static b32 writefile(Arena *a, String8 path, String8 data);
static b32 appendfile(Arena *a, String8 path, String8 data);
static String8 readfilerng(Arena *a, u64 fd, Rng1u64 r);
static Datetime tmtodatetime(tm t, u32 msec);
static tm datetimetotm(Datetime dt);
static timespec datetimetotimespec(Datetime dt);
static u64 timespectodense(timespec ts);
static Fprops stattoprops(struct stat *st);
static String8 cwd(Arena *a);
static void *osreserve(u64 size);
static b32 oscommit(void *p, u64 size);
static void osdecommit(void *p, u64 size);
static void osrelease(void *p, u64 size);
static void *osreservelarge(u64 size);
static u64 openfd(Arena *a, String8 path, int flags);
static void closefd(u64 fd);
static u64 readrng(u64 fd, Rng1u64 r, void *out);
static u64 writerng(u64 fd, Rng1u64 r, void *data);
static b32 settimes(u64 fd, Datetime dt);
static Fprops osfstat(u64 fd);
static b32 osremove(Arena *a, String8 path);
static String8 abspath(Arena *a, String8 path);
static b32 fileexists(Arena *a, String8 path);
static b32 direxists(Arena *a, String8 path);
static Fprops osstat(Arena *a, String8 path);
static b32 osmkdir(Arena *a, String8 path);
static u64 nowus(void);
static u32 nowunix(void);
static Datetime nowutc(void);
static Datetime localtoutc(Datetime dt);
static Datetime utctolocal(Datetime dt);
static void sleepms(u32 msec);

#endif  // OS_H
