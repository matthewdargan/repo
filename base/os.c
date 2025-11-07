// Handle Type Functions
static OS_Handle
os_handle_zero(void)
{
	OS_Handle handle = {0};
	return handle;
}

static b32
os_handle_match(OS_Handle a, OS_Handle b)
{
	return a.u64[0] == b.u64[0];
}

// Filesystem Helpers
static String8
os_data_from_file_path(Arena *arena, String8 path)
{
	OS_Handle file          = os_file_open(OS_AccessFlag_Read | OS_AccessFlag_ShareRead, path);
	OS_FileProperties props = os_properties_from_file(file);
	String8 data            = os_string_from_file_range(arena, file, rng_1u64(0, props.size));
	os_file_close(file);
	return data;
}

static b32
os_write_data_to_file_path(String8 path, String8 data)
{
	b32 good       = 0;
	OS_Handle file = os_file_open(OS_AccessFlag_Write, path);
	if (!os_handle_match(file, os_handle_zero()))
	{
		u64 bytes_written = os_file_write(file, rng_1u64(0, data.size), data.str);
		good              = (bytes_written == data.size);
		os_file_close(file);
	}
	return good;
}

static b32
os_append_data_to_file_path(String8 path, String8 data)
{
	b32 good = 0;
	if (data.size != 0)
	{
		OS_Handle file = os_file_open(OS_AccessFlag_Write | OS_AccessFlag_Append, path);
		if (!os_handle_match(file, os_handle_zero()))
		{
			u64 pos           = os_properties_from_file(file).size;
			u64 bytes_written = os_file_write(file, rng_1u64(pos, pos + data.size), data.str);
			good              = (bytes_written == data.size);
			os_file_close(file);
		}
	}
	return good;
}

static String8
os_string_from_file_range(Arena *arena, OS_Handle file, Rng1U64 range)
{
	u64 pre_pos = arena_pos(arena);
	String8 result;
	result.size          = dim_1u64(range);
	result.str           = push_array_no_zero(arena, u8, result.size);
	u64 actual_read_size = os_file_read(file, range, result.str);
	if (actual_read_size < result.size)
	{
		arena_pop_to(arena, pre_pos + actual_read_size);
		result.size = actual_read_size;
	}
	return result;
}

// Helpers
static DateTime
os_date_time_from_tm(tm in, u32 msec)
{
	DateTime dt = {0};
	dt.sec      = in.tm_sec;
	dt.min      = in.tm_min;
	dt.hour     = in.tm_hour;
	dt.day      = in.tm_mday - 1;
	dt.mon      = in.tm_mon;
	dt.year     = in.tm_year + 1900;
	dt.msec     = msec;
	return dt;
}

static tm
os_tm_from_date_time(DateTime dt)
{
	tm result      = {0};
	result.tm_sec  = dt.sec;
	result.tm_min  = dt.min;
	result.tm_hour = dt.hour;
	result.tm_mday = dt.day + 1;
	result.tm_mon  = dt.mon;
	result.tm_year = dt.year - 1900;
	return result;
}

static timespec
os_timespec_from_date_time(DateTime dt)
{
	tm tm_val       = os_tm_from_date_time(dt);
	time_t seconds  = timegm(&tm_val);
	timespec result = {0};
	result.tv_sec   = seconds;
	return result;
}

static u64
os_dense_time_from_timespec(timespec in)
{
	u64 result = 0;
	{
		tm tm_time = {0};
		gmtime_r(&in.tv_sec, &tm_time);
		DateTime date_time = os_date_time_from_tm(tm_time, in.tv_nsec / Million(1));
		result             = dense_time_from_date_time(date_time);
	}
	return result;
}

static OS_FileProperties
os_file_properties_from_stat(struct stat *s)
{
	OS_FileProperties props = {0};
	props.size              = s->st_size;
	props.created           = os_dense_time_from_timespec(s->st_ctim);
	props.modified          = os_dense_time_from_timespec(s->st_mtim);
	if (s->st_mode & S_IFDIR)
	{
		props.flags |= OS_FilePropertyFlag_Directory;
	}
	return props;
}

// System Info
static OS_SystemInfo os_system_info = {0};

static OS_SystemInfo *
os_get_system_info(void)
{
	return &os_system_info;
}

static String8
os_get_current_path(Arena *arena)
{
	char *cwdir    = getcwd(0, 0);
	String8 string = str8_copy(arena, str8_cstring(cwdir));
	free(cwdir);
	return string;
}

// Memory Allocation
static void *
os_reserve(u64 size)
{
	void *result = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (result == MAP_FAILED)
	{
		result = 0;
	}
	return result;
}

static b32
os_commit(void *ptr, u64 size)
{
	mprotect(ptr, size, PROT_READ | PROT_WRITE);
	return 1;
}

static void
os_decommit(void *ptr, u64 size)
{
	madvise(ptr, size, MADV_DONTNEED);
	mprotect(ptr, size, PROT_NONE);
}

static void
os_release(void *ptr, u64 size)
{
	munmap(ptr, size);
}

static void *
os_reserve_large(u64 size)
{
	void *result = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (result == MAP_FAILED)
	{
		result = 0;
	}
	return result;
}

// File System
static OS_Handle
os_file_open(OS_AccessFlags flags, String8 path)
{
	Temp scratch      = scratch_begin(0, 0);
	String8 path_copy = str8_copy(scratch.arena, path);
	int lnx_flags     = 0;
	if (flags & OS_AccessFlag_Read && flags & OS_AccessFlag_Write)
	{
		lnx_flags = O_RDWR;
	}
	else if (flags & OS_AccessFlag_Write)
	{
		lnx_flags = O_WRONLY;
	}
	else if (flags & OS_AccessFlag_Read)
	{
		lnx_flags = O_RDONLY;
	}
	if (flags & OS_AccessFlag_Append)
	{
		lnx_flags |= O_APPEND;
	}
	if (flags & (OS_AccessFlag_Write | OS_AccessFlag_Append))
	{
		lnx_flags |= O_CREAT;
	}
	int fd           = open((char *)path_copy.str, lnx_flags, 0755);
	OS_Handle handle = {0};
	if (fd != -1)
	{
		handle.u64[0] = fd;
	}
	scratch_end(scratch);
	return handle;
}

static void
os_file_close(OS_Handle file)
{
	if (os_handle_match(file, os_handle_zero()))
	{
		return;
	}
	int fd = (int)file.u64[0];
	close(fd);
}

static u64
os_file_read(OS_Handle file, Rng1U64 rng, void *out_data)
{
	if (os_handle_match(file, os_handle_zero()))
	{
		return 0;
	}
	int fd                           = (int)file.u64[0];
	u64 total_num_bytes_to_read      = dim_1u64(rng);
	u64 total_num_bytes_read         = 0;
	u64 total_num_bytes_left_to_read = total_num_bytes_to_read;
	while (total_num_bytes_left_to_read > 0)
	{
		int read_result =
		    pread(fd, (u8 *)out_data + total_num_bytes_read, total_num_bytes_left_to_read, rng.min + total_num_bytes_read);
		if (read_result >= 0)
		{
			total_num_bytes_read += read_result;
			total_num_bytes_left_to_read -= read_result;
		}
		else if (errno != EINTR)
		{
			break;
		}
	}
	return total_num_bytes_read;
}

static u64
os_file_write(OS_Handle file, Rng1U64 rng, void *data)
{
	if (os_handle_match(file, os_handle_zero()))
	{
		return 0;
	}
	int fd                            = (int)file.u64[0];
	u64 total_num_bytes_to_write      = dim_1u64(rng);
	u64 total_num_bytes_written       = 0;
	u64 total_num_bytes_left_to_write = total_num_bytes_to_write;
	while (total_num_bytes_left_to_write > 0)
	{
		int write_result = pwrite(fd, (u8 *)data + total_num_bytes_written, total_num_bytes_left_to_write,
		                          rng.min + total_num_bytes_written);
		if (write_result >= 0)
		{
			total_num_bytes_written += write_result;
			total_num_bytes_left_to_write -= write_result;
		}
		else if (errno != EINTR)
		{
			break;
		}
	}
	return total_num_bytes_written;
}

static b32
os_file_set_times(OS_Handle file, DateTime date_time)
{
	if (os_handle_match(file, os_handle_zero()))
	{
		return 0;
	}
	int fd              = (int)file.u64[0];
	timespec time       = os_timespec_from_date_time(date_time);
	timespec times[2]   = {time, time};
	int futimens_result = futimens(fd, times);
	b32 good            = (futimens_result != -1);
	return good;
}

static OS_FileProperties
os_properties_from_file(OS_Handle file)
{
	if (os_handle_match(file, os_handle_zero()))
	{
		return (OS_FileProperties){0};
	}
	int fd                  = (int)file.u64[0];
	struct stat fd_stat     = {0};
	int fstat_result        = fstat(fd, &fd_stat);
	OS_FileProperties props = {0};
	if (fstat_result != -1)
	{
		props = os_file_properties_from_stat(&fd_stat);
	}
	return props;
}

static b32
os_delete_file_at_path(String8 path)
{
	Temp scratch      = scratch_begin(0, 0);
	b32 result        = 0;
	String8 path_copy = str8_copy(scratch.arena, path);
	if (remove((char *)path_copy.str) != -1)
	{
		result = 1;
	}
	scratch_end(scratch);
	return result;
}

static String8
os_full_path_from_path(Arena *arena, String8 path)
{
	Temp scratch          = scratch_begin(&arena, 1);
	String8 path_copy     = str8_copy(scratch.arena, path);
	char buffer[PATH_MAX] = {0};
	realpath((char *)path_copy.str, buffer);
	String8 result = str8_copy(arena, str8_cstring(buffer));
	scratch_end(scratch);
	return result;
}

static b32
os_file_path_exists(String8 path)
{
	Temp scratch      = scratch_begin(0, 0);
	String8 path_copy = str8_copy(scratch.arena, path);
	int access_result = access((char *)path_copy.str, F_OK);
	b32 result        = 0;
	if (access_result == 0)
	{
		result = 1;
	}
	scratch_end(scratch);
	return result;
}

static b32
os_directory_path_exists(String8 path)
{
	Temp scratch      = scratch_begin(0, 0);
	b32 exists        = 0;
	String8 path_copy = str8_copy(scratch.arena, path);
	DIR *handle       = opendir((char *)path_copy.str);
	if (handle)
	{
		closedir(handle);
		exists = 1;
	}
	scratch_end(scratch);
	return exists;
}

static OS_FileProperties
os_properties_from_file_path(String8 path)
{
	Temp scratch            = scratch_begin(0, 0);
	String8 path_copy       = str8_copy(scratch.arena, path);
	struct stat f_stat      = {0};
	int stat_result         = stat((char *)path_copy.str, &f_stat);
	OS_FileProperties props = {0};
	if (stat_result != -1)
	{
		props = os_file_properties_from_stat(&f_stat);
	}
	scratch_end(scratch);
	return props;
}

static b32
os_make_directory(String8 path)
{
	Temp scratch      = scratch_begin(0, 0);
	b32 result        = 0;
	String8 path_copy = str8_copy(scratch.arena, path);
	if (mkdir((char *)path_copy.str, 0755) != -1)
	{
		result = 1;
	}
	scratch_end(scratch);
	return result;
}

// Time
static u64
os_now_microseconds(void)
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	u64 result = t.tv_sec * Million(1) + (t.tv_nsec / Thousand(1));
	return result;
}

static u32
os_now_unix(void)
{
	time_t t = time(0);
	return (u32)t;
}

static DateTime
os_now_universal_time(void)
{
	time_t t = 0;
	time(&t);
	tm universal_tm = {0};
	gmtime_r(&t, &universal_tm);
	DateTime result = os_date_time_from_tm(universal_tm, 0);
	return result;
}

static DateTime
os_universal_time_from_local(DateTime *date_time)
{
	tm local_tm        = os_tm_from_date_time(*date_time);
	local_tm.tm_isdst  = -1;
	time_t universal_t = mktime(&local_tm);

	tm universal_tm = {0};
	gmtime_r(&universal_t, &universal_tm);
	DateTime result = os_date_time_from_tm(universal_tm, 0);
	return result;
}

static DateTime
os_local_time_from_universal(DateTime *date_time)
{
	tm universal_tm       = os_tm_from_date_time(*date_time);
	universal_tm.tm_isdst = -1;
	time_t universal_t    = timegm(&universal_tm);
	tm local_tm           = {0};
	localtime_r(&universal_t, &local_tm);

	DateTime result = os_date_time_from_tm(local_tm, 0);
	return result;
}

static void
os_sleep_milliseconds(u32 msec)
{
	usleep(msec * Thousand(1));
}

// Entry Point
int
main(int argc, char **argv)
{
	// set up OS layer
	{
		OS_SystemInfo *info           = os_get_system_info();
		info->logical_processor_count = sysconf(_SC_NPROCESSORS_ONLN);
		info->page_size               = sysconf(_SC_PAGESIZE);
		info->large_page_size         = MB(2);
	}

	// set up thread context
	TCTX *tctx = tctx_alloc();
	tctx_select(tctx);

	// call into base entry point
	main_thread_base_entry_point(argc, argv);
}
