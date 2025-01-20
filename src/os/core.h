#ifndef OS_CORE_H
#define OS_CORE_H

typedef struct os_system_info os_system_info;
struct os_system_info {
    u32 logical_processor_count;
    u64 page_size;
    u64 large_page_size;
    u64 allocation_granularity;
};

typedef u64 os_handle;

typedef struct tm tm;
typedef struct timespec timespec;

global os_system_info os_info = {0};
global arena *g_arena = {0};

internal string8list os_string_list_from_argcv(arena *a, int argc, char **argv);
internal string8 os_data_from_file_path(arena *a, string8 path);
internal b32 os_write_data_to_file_path(string8 path, string8 data);
internal b32 os_append_data_to_file_path(string8 path, string8 data);
internal string8 os_string_from_file_range(arena *a, os_handle file, rng1u64 range);
internal date_time os_date_time_from_tm(tm in, u32 msec);
internal tm os_tm_from_date_time(date_time dt);
internal timespec os_timespec_from_date_time(date_time dt);
internal dense_time os_dense_time_from_timespec(timespec in);
internal file_properties os_file_properties_from_stat(struct stat *s);
internal string8 os_get_current_path(arena *a);
internal void *os_reserve(u64 size);
internal b32 os_commit(void *ptr, u64 size);
internal void os_decommit(void *ptr, u64 size);
internal void os_release(void *ptr, u64 size);
internal void *os_reserve_large(u64 size);
internal os_handle os_file_open(int flags, string8 path);
internal void os_file_close(os_handle file);
internal u64 os_file_read(os_handle file, rng1u64 rng, void *out_data);
internal u64 os_file_write(os_handle file, rng1u64 rng, void *data);
internal b32 os_file_set_times(os_handle file, date_time dt);
internal file_properties os_properties_from_file(os_handle file);
internal b32 os_delete_file_at_path(string8 path);
internal string8 os_full_path_from_path(string8 path);
internal b32 os_file_path_exists(string8 path);
internal b32 os_folder_path_exists(string8 path);
internal file_properties os_properties_from_file_path(string8 path);
internal b32 os_make_directory(string8 path);
internal u64 os_now_microseconds(void);
internal u32 os_now_unix(void);
internal date_time os_now_universal_time(void);
internal date_time os_universal_time_from_local(date_time dt);
internal date_time os_local_time_from_universal(date_time dt);
internal void os_sleep_milliseconds(u32 msec);

internal int entry_point(cmd_line *cmd_line);

#endif  // OS_CORE_H
