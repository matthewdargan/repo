#ifndef OS_H
#define OS_H

// Includes
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

typedef struct tm tm;
typedef struct timespec timespec;

// System Info
typedef struct OS_SystemInfo OS_SystemInfo;
struct OS_SystemInfo
{
	u32 logical_processor_count;
	u64 page_size;
	u64 large_page_size;
};

// Access Flags
typedef u32 OS_AccessFlags;
enum
{
	OS_AccessFlag_Read = (1 << 0),
	OS_AccessFlag_Write = (1 << 1),
	OS_AccessFlag_Append = (1 << 2),
	OS_AccessFlag_ShareRead = (1 << 3),
	OS_AccessFlag_ShareWrite = (1 << 4),
};

// File Types
typedef u32 OS_FilePropertyFlags;
enum
{
	OS_FilePropertyFlag_Directory = (1 << 0),
};

typedef struct OS_FileProperties OS_FileProperties;
struct OS_FileProperties
{
	u64 size;
	DenseTime created;
	DenseTime modified;
	OS_FilePropertyFlags flags;
};

// Handle Type
typedef struct OS_Handle OS_Handle;
struct OS_Handle
{
	u64 u64[1];
};

// Handle Type Functions
static OS_Handle os_handle_zero(void);
static b32 os_handle_match(OS_Handle a, OS_Handle b);

// Filesystem Helpers
static String8 os_data_from_file_path(Arena *arena, String8 path);
static b32 os_write_data_to_file_path(String8 path, String8 data);
static b32 os_append_data_to_file_path(String8 path, String8 data);
static String8 os_string_from_file_range(Arena *arena, OS_Handle file, Rng1U64 range);

// System Info
static OS_SystemInfo *os_get_system_info(void);
static String8 os_get_current_path(Arena *arena);

// Memory Allocation
static void *os_reserve(u64 size);
static b32 os_commit(void *ptr, u64 size);
static void os_decommit(void *ptr, u64 size);
static void os_release(void *ptr, u64 size);
static void *os_reserve_large(u64 size);

// File System
static OS_Handle os_file_open(OS_AccessFlags flags, String8 path);
static void os_file_close(OS_Handle file);
static u64 os_file_read(OS_Handle file, Rng1U64 rng, void *out_data);
static u64 os_file_write(OS_Handle file, Rng1U64 rng, void *data);
static b32 os_file_set_times(OS_Handle file, DateTime time);
static OS_FileProperties os_properties_from_file(OS_Handle file);
static b32 os_delete_file_at_path(String8 path);
static String8 os_full_path_from_path(Arena *arena, String8 path);
static b32 os_file_path_exists(String8 path);
static b32 os_directory_path_exists(String8 path);
static OS_FileProperties os_properties_from_file_path(String8 path);
static b32 os_make_directory(String8 path);

// Time
static u64 os_now_microseconds(void);
static u32 os_now_unix(void);
static DateTime os_now_universal_time(void);
static DateTime os_universal_time_from_local(DateTime *local_time);
static DateTime os_local_time_from_universal(DateTime *universal_time);
static void os_sleep_milliseconds(u32 msec);

#endif // OS_H
