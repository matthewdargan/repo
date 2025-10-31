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

static String8list os_args(Arena *a, int argc, char **argv);
static String8 readfile(Arena *a, String8 path);
static b32 writefile(Arena *a, String8 path, String8 data);
static b32 appendfile(Arena *a, String8 path, String8 data);
static String8 read_file_rng(Arena *a, u64 fd, Rng1u64 r);
static Datetime tmtodatetime(tm t, u32 msec);
static tm datetimetotm(Datetime dt);
static timespec datetimetotimespec(Datetime dt);
static u64 timespectodense(timespec ts);
static Fprops stattoprops(struct stat *st);
static String8 cwd(Arena *a);
static void *os_reserve(u64 size);
static b32 os_commit(void *p, u64 size);
static void os_decommit(void *p, u64 size);
static void os_release(void *p, u64 size);
static void *os_reserve_large(u64 size);
static u64 open_fd(Arena *a, String8 path, int flags);
static void close_fd(u64 fd);
static u64 read_rng(u64 fd, Rng1u64 r, void *out);
static u64 write_rng(u64 fd, Rng1u64 r, void *data);
static b32 set_times(u64 fd, Datetime dt);
static Fprops os_fstat(u64 fd);
static b32 os_remove(Arena *a, String8 path);
static String8 abs_path(Arena *a, String8 path);
static b32 file_exists(Arena *a, String8 path);
static b32 dir_exists(Arena *a, String8 path);
static Fprops os_stat(Arena *a, String8 path);
static b32 os_mkdir(Arena *a, String8 path);
static u64 now_us(void);
static u32 now_unix(void);
static Datetime now_utc(void);
static Datetime local_to_utc(Datetime dt);
static Datetime utc_to_local(Datetime dt);
static void sleep_ms(u32 msec);

#endif  // OS_H
