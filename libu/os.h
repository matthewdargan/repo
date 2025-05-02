#ifndef OS_CORE_H
#define OS_CORE_H

typedef struct SystemInfo SystemInfo;
struct SystemInfo {
	u32 nprocs;
	u64 page_size;
	u64 large_page_size;
};

typedef struct tm tm;
typedef struct timespec timespec;

static SystemInfo sys_info = {0};
static Arena *arena = {0};

static String8List os_args(Arena *a, int argc, char **argv);
static String8 os_read_file(Arena *a, String8 path);
static b32 os_write_file(String8 path, String8 data);
static b32 os_append_file(String8 path, String8 data);
static String8 os_read_file_range(Arena *a, u64 fd, Rng1U64 r);
static DateTime os_tm_to_datetime(tm in, u32 msec);
static tm os_datetime_to_tm(DateTime dt);
static timespec os_datetime_to_timespec(DateTime dt);
static DenseTime os_timespec_to_densetime(timespec ts);
static FileProperties os_stat_to_props(struct stat *st);
static String8 os_cwd(Arena *a);
static void *os_reserve(u64 size);
static b32 os_commit(void *p, u64 size);
static void os_decommit(void *p, u64 size);
static void os_release(void *p, u64 size);
static void *os_reserve_large(u64 size);
static u64 os_open(String8 path, int flags);
static void os_close(u64 fd);
static u64 os_read(u64 fd, Rng1U64 r, void *out);
static u64 os_write(u64 fd, Rng1U64 r, void *data);
static b32 os_set_times(u64 fd, DateTime dt);
static FileProperties os_fstat(u64 fd);
static b32 os_remove(String8 path);
static String8 os_abspath(String8 path);
static b32 os_file_exists(String8 path);
static b32 os_dir_exists(String8 path);
static FileProperties os_stat(String8 path);
static b32 os_mkdir(String8 path);
static u64 os_now_us(void);
static u32 os_now_unix(void);
static DateTime os_now_utc(void);
static DateTime os_local_to_utc(DateTime dt);
static DateTime os_utc_to_local(DateTime dt);
static void os_sleep_ms(u32 msec);

#endif  // OS_CORE_H
