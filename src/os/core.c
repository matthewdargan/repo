internal string8list os_string_list_from_argcv(arena *a, int argc, char **argv) {
    string8list result = {0};
    for (int i = 0; i < argc; ++i) {
        string8 str = str8_cstring(argv[i]);
        str8_list_push(a, &result, str);
    }
    return result;
}

internal string8 os_data_from_file_path(arena *a, string8 path) {
    os_handle file = os_file_open(O_RDONLY, path);
    file_properties props = os_properties_from_file(file);
    string8 data = os_string_from_file_range(a, file, rng_1u64(0, props.size));
    os_file_close(file);
    return data;
}

internal b32 os_write_data_to_file_path(string8 path, string8 data) {
    b32 good = 0;
    os_handle file = os_file_open(O_WRONLY, path);
    if (file != 0) {
        good = 1;
        os_file_write(file, rng_1u64(0, data.size), data.str);
        os_file_close(file);
    }
    return good;
}

internal b32 os_append_data_to_file_path(string8 path, string8 data) {
    b32 good = 0;
    if (data.size != 0) {
        os_handle file = os_file_open(O_WRONLY | O_APPEND | O_CREAT, path);
        if (file != 0) {
            good = 1;
            u64 pos = os_properties_from_file(file).size;
            os_file_write(file, rng_1u64(pos, pos + data.size), data.str);
            os_file_close(file);
        }
    }
    return good;
}

internal string8 os_string_from_file_range(arena *a, os_handle file, rng1u64 range) {
    u64 pre_pos = arena_pos(a);
    string8 result;
    result.size = dim_1u64(range);
    result.str = push_array_no_zero(a, u8, result.size);
    u64 actual_read_size = os_file_read(file, range, result.str);
    if (actual_read_size < result.size) {
        arena_pop_to(a, pre_pos + actual_read_size);
        result.size = actual_read_size;
    }
    return result;
}

internal date_time os_date_time_from_tm(tm in, u32 msec) {
    date_time dt = {0};
    dt.sec = in.tm_sec;
    dt.min = in.tm_min;
    dt.hour = in.tm_hour;
    dt.day = in.tm_mday - 1;
    dt.mon = in.tm_mon;
    dt.year = in.tm_year + 1900;
    dt.msec = msec;
    return dt;
}

internal tm os_tm_from_date_time(date_time dt) {
    tm result = {0};
    result.tm_sec = dt.sec;
    result.tm_min = dt.min;
    result.tm_hour = dt.hour;
    result.tm_mday = dt.day + 1;
    result.tm_mon = dt.mon;
    result.tm_year = dt.year - 1900;
    return result;
}

internal timespec os_timespec_from_date_time(date_time dt) {
    tm tm_val = os_tm_from_date_time(dt);
    time_t seconds = timegm(&tm_val);
    timespec result = {0};
    result.tv_sec = seconds;
    return result;
}

internal dense_time os_dense_time_from_timespec(timespec in) {
    tm tm_val = {0};
    gmtime_r(&in.tv_sec, &tm_val);
    date_time dt = os_date_time_from_tm(tm_val, in.tv_nsec / MILLION(1));
    return dense_time_from_date_time(dt);
}

internal file_properties os_file_properties_from_stat(struct stat *s) {
    file_properties props = {0};
    props.size = s->st_size;
    props.created = os_dense_time_from_timespec(s->st_ctim);
    props.modified = os_dense_time_from_timespec(s->st_mtim);
    if (s->st_mode & S_IFDIR) {
        props.flags |= FILE_PROPERTY_FLAG_IS_FOLDER;
    }
    return props;
}

internal string8 os_get_current_path(arena *a) {
    char *cwdir = getcwd(0, 0);
    string8 string = push_str8_copy(a, str8_cstring(cwdir));
    free(cwdir);
    return string;
}

internal void *os_reserve(u64 size) {
    void *result = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) {
        result = 0;
    }
    return result;
}

internal b32 os_commit(void *ptr, u64 size) {
    mprotect(ptr, size, PROT_READ | PROT_WRITE);
    return 1;
}

internal void os_decommit(void *ptr, u64 size) {
    madvise(ptr, size, MADV_DONTNEED);
    mprotect(ptr, size, PROT_NONE);
}

internal void os_release(void *ptr, u64 size) {
    munmap(ptr, size);
}

internal void *os_reserve_large(u64 size) {
    void *result = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (result == MAP_FAILED) {
        result = 0;
    }
    return result;
}

internal os_handle os_file_open(int flags, string8 path) {
    temp scratch = temp_begin(g_arena);
    string8 path_copy = push_str8_copy(scratch.a, path);
    int fd = open((char *)path_copy.str, flags, 0755);
    os_handle handle = 0;
    if (fd != -1) {
        handle = fd;
    }
    temp_end(scratch);
    return handle;
}

internal void os_file_close(os_handle file) {
    if (file == 0) {
        return;
    }
    close(file);
}

internal u64 os_file_read(os_handle file, rng1u64 rng, void *out_data) {
    if (file == 0) {
        return 0;
    }
    u64 total_num_bytes_to_read = dim_1u64(rng);
    u64 total_num_bytes_read = 0;
    u64 total_num_bytes_left_to_read = total_num_bytes_to_read;
    for (; total_num_bytes_left_to_read > 0;) {
        int read_result = pread(file, (u8 *)out_data + total_num_bytes_read, total_num_bytes_left_to_read,
                                rng.min + total_num_bytes_read);
        if (read_result >= 0) {
            total_num_bytes_read += read_result;
            total_num_bytes_left_to_read -= read_result;
        } else if (errno != EINTR) {
            break;
        }
    }
    return total_num_bytes_read;
}

internal u64 os_file_write(os_handle file, rng1u64 rng, void *data) {
    if (file == 0) {
        return 0;
    }
    u64 total_num_bytes_to_write = dim_1u64(rng);
    u64 total_num_bytes_written = 0;
    u64 total_num_bytes_left_to_write = total_num_bytes_to_write;
    for (; total_num_bytes_left_to_write > 0;) {
        int write_result = pwrite(file, (u8 *)data + total_num_bytes_written, total_num_bytes_left_to_write,
                                  rng.min + total_num_bytes_written);
        if (write_result >= 0) {
            total_num_bytes_written += write_result;
            total_num_bytes_left_to_write -= write_result;
        } else if (errno != EINTR) {
            break;
        }
    }
    return total_num_bytes_written;
}

internal b32 os_file_set_times(os_handle file, date_time dt) {
    if (file == 0) {
        return 0;
    }
    timespec time = os_timespec_from_date_time(dt);
    timespec times[2] = {time, time};
    return futimens(file, times) != -1;
}

internal file_properties os_properties_from_file(os_handle file) {
    if (file == 0) {
        return (file_properties){0};
    }
    struct stat f_stat = {0};
    int fstat_result = fstat(file, &f_stat);
    file_properties props = {0};
    if (fstat_result != -1) {
        props = os_file_properties_from_stat(&f_stat);
    }
    return props;
}

internal b32 os_delete_file_at_path(string8 path) {
    temp scratch = temp_begin(g_arena);
    b32 result = 0;
    string8 path_copy = push_str8_copy(scratch.a, path);
    if (remove((char *)path_copy.str) != -1) {
        result = 1;
    }
    temp_end(scratch);
    return result;
}

internal string8 os_full_path_from_path(string8 path) {
    temp scratch = temp_begin(g_arena);
    string8 path_copy = push_str8_copy(scratch.a, path);
    char buffer[PATH_MAX] = {0};
    realpath((char *)path_copy.str, buffer);
    string8 result = push_str8_copy(scratch.a, str8_cstring(buffer));
    temp_end(scratch);
    return result;
}

internal b32 os_file_path_exists(string8 path) {
    temp scratch = temp_begin(g_arena);
    string8 path_copy = push_str8_copy(scratch.a, path);
    int access_result = access((char *)path_copy.str, F_OK);
    b32 result = 0;
    if (access_result == 0) {
        result = 1;
    }
    temp_end(scratch);
    return result;
}

internal b32 os_folder_path_exists(string8 path) {
    temp scratch = temp_begin(g_arena);
    b32 exists = 0;
    string8 path_copy = push_str8_copy(scratch.a, path);
    DIR *handle = opendir((char *)path_copy.str);
    if (handle) {
        closedir(handle);
        exists = 1;
    }
    temp_end(scratch);
    return exists;
}

internal file_properties os_properties_from_file_path(string8 path) {
    temp scratch = temp_begin(g_arena);
    string8 path_copy = push_str8_copy(scratch.a, path);
    struct stat f_stat = {0};
    int stat_result = stat((char *)path_copy.str, &f_stat);
    file_properties props = {0};
    if (stat_result != -1) {
        props = os_file_properties_from_stat(&f_stat);
    }
    temp_end(scratch);
    return props;
}

internal b32 os_make_directory(string8 path) {
    temp scratch = temp_begin(g_arena);
    string8 path_copy = push_str8_copy(scratch.a, path);
    b32 result = 0;
    if (mkdir((char *)path_copy.str, 0755) != -1) {
        result = 1;
    }
    temp_end(scratch);
    return result;
}

internal u64 os_now_microseconds(void) {
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * MILLION(1) + (t.tv_nsec / THOUSAND(1));
}

internal u32 os_now_unix(void) {
    return (u32)time(0);
}

internal date_time os_now_universal_time(void) {
    time_t t = 0;
    time(&t);
    tm universal_tm = {0};
    gmtime_r(&t, &universal_tm);
    return os_date_time_from_tm(universal_tm, 0);
}

internal date_time os_universal_time_from_local(date_time dt) {
    // local date_time -> universal time_t
    tm local_tm = os_tm_from_date_time(dt);
    local_tm.tm_isdst = -1;
    time_t universal_t = mktime(&local_tm);
    // universal time_t -> date_time
    tm universal_tm = {0};
    gmtime_r(&universal_t, &universal_tm);
    return os_date_time_from_tm(universal_tm, 0);
}

internal date_time os_local_time_from_universal(date_time dt) {
    // universal date_time -> local time_t
    tm universal_tm = os_tm_from_date_time(dt);
    universal_tm.tm_isdst = -1;
    time_t universal_t = timegm(&universal_tm);
    tm local_tm = {0};
    localtime_r(&universal_t, &local_tm);
    // local tm -> date_time
    return os_date_time_from_tm(local_tm, 0);
}

internal void os_sleep_milliseconds(u32 msec) {
    usleep(msec * THOUSAND(1));
}

int main(int argc, char **argv) {
    {
        os_info.logical_processor_count = (u32)sysconf(_SC_NPROCESSORS_ONLN);
        os_info.page_size = (u64)sysconf(_SC_PAGESIZE);
        os_info.large_page_size = MB(2);
        os_info.allocation_granularity = MB(2);
    }
    g_arena = arena_alloc((arena_params){.flags = arena_default_flags,
                                         .reserve_size = arena_default_reserve_size,
                                         .commit_size = arena_default_commit_size});
    string8list command_line_argument_strings = os_string_list_from_argcv(g_arena, argc, argv);
    cmd_line parsed = cmd_line_from_string_list(g_arena, command_line_argument_strings);
    int result = entry_point(&parsed);
    arena_release(g_arena);
    return result;
}
