static String8List
os_args(Arena *a, int argc, char **argv)
{
	String8List list = {0};
	for (int i = 0; i < argc; ++i) {
		String8 s = str8_cstr(argv[i]);
		str8_list_push(a, &list, s);
	}
	return list;
}

static String8
os_read_file(Arena *a, String8 path)
{
	u64 fd = os_open(path, O_RDONLY);
	FileProperties props = os_fstat(fd);
	String8 data = os_read_file_range(a, fd, rng1u64(0, props.size));
	os_close(fd);
	return data;
}

static b32
os_write_file(String8 path, String8 data)
{
	b32 ok = 0;
	u64 fd = os_open(path, O_WRONLY);
	if (fd != 0) {
		ok = 1;
		os_write(fd, rng1u64(0, data.len), data.str);
		os_close(fd);
	}
	return ok;
}

static b32
os_append_file(String8 path, String8 data)
{
	b32 ok = 0;
	if (data.len != 0) {
		u64 fd = os_open(path, O_WRONLY | O_APPEND | O_CREAT);
		if (fd != 0) {
			ok = 1;
			u64 pos = os_fstat(fd).size;
			os_write(fd, rng1u64(pos, pos + data.len), data.str);
			os_close(fd);
		}
	}
	return ok;
}

static String8
os_read_file_range(Arena *a, u64 fd, Rng1U64 r)
{
	u64 pre_pos = arena_pos(a);
	String8 s = str8_zero();
	s.len = dim1u64(r);
	s.str = push_array_no_zero(a, u8, s.len);
	u64 actual_read_size = os_read(fd, r, s.str);
	if (actual_read_size < s.len) {
		arena_pop_to(a, pre_pos + actual_read_size);
		s.len = actual_read_size;
	}
	return s;
}

static DateTime
os_tm_to_datetime(tm in, u32 msec)
{
	DateTime dt = {0};
	dt.sec = in.tm_sec;
	dt.min = in.tm_min;
	dt.hour = in.tm_hour;
	dt.day = in.tm_mday - 1;
	dt.mon = in.tm_mon;
	dt.year = in.tm_year + 1900;
	dt.msec = msec;
	return dt;
}

static tm
os_datetime_to_tm(DateTime dt)
{
	tm t = {0};
	t.tm_sec = dt.sec;
	t.tm_min = dt.min;
	t.tm_hour = dt.hour;
	t.tm_mday = dt.day + 1;
	t.tm_mon = dt.mon;
	t.tm_year = dt.year - 1900;
	return t;
}

static timespec
os_datetime_to_timespec(DateTime dt)
{
	tm t = os_datetime_to_tm(dt);
	time_t sec = timegm(&t);
	timespec ts = {0};
	ts.tv_sec = sec;
	return ts;
}

static DenseTime
os_timespec_to_densetime(timespec ts)
{
	tm t = {0};
	gmtime_r(&ts.tv_sec, &t);
	DateTime dt = os_tm_to_datetime(t, ts.tv_nsec / MILLION(1));
	return date_time_to_dense_time(dt);
}

static FileProperties
os_stat_to_props(struct stat *st)
{
	FileProperties props = {0};
	props.size = st->st_size;
	props.created = os_timespec_to_densetime(st->st_ctim);
	props.modified = os_timespec_to_densetime(st->st_mtim);
	if (st->st_mode & S_IFDIR) {
		props.flags |= FILE_PROPERTY_FLAG_IS_DIR;
	}
	return props;
}

static String8
os_cwd(Arena *a)
{
	char *cwd = getcwd(0, 0);
	String8 s = push_str8_copy(a, str8_cstr(cwd));
	free(cwd);
	return s;
}

static void *
os_reserve(u64 size)
{
	void *p = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		p = 0;
	}
	return p;
}

static b32
os_commit(void *p, u64 size)
{
	mprotect(p, size, PROT_READ | PROT_WRITE);
	return 1;
}

static void
os_decommit(void *p, u64 size)
{
	madvise(p, size, MADV_DONTNEED);
	mprotect(p, size, PROT_NONE);
}

static void
os_release(void *p, u64 size)
{
	munmap(p, size);
}

static void *
os_reserve_large(u64 size)
{
	void *p = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (p == MAP_FAILED) {
		p = 0;
	}
	return p;
}

static u64
os_open(String8 path, int flags)
{
	Temp scratch = temp_begin(arena);
	String8 p = push_str8_copy(scratch.a, path);
	int fd = open((char *)p.str, flags, 0755);
	u64 handle = 0;
	if (fd != -1) {
		handle = fd;
	}
	temp_end(scratch);
	return handle;
}

static void
os_close(u64 fd)
{
	if (fd == 0) {
		return;
	}
	close(fd);
}

static u64
os_read(u64 fd, Rng1U64 r, void *out)
{
	if (fd == 0) {
		return 0;
	}
	u64 size = dim1u64(r);
	u64 nread = 0;
	u64 nleft = size;
	for (; nleft > 0;) {
		int n = pread(fd, (u8 *)out + nread, nleft, r.min + nread);
		if (n >= 0) {
			nread += n;
			nleft -= n;
		} else if (errno != EINTR) {
			break;
		}
	}
	return nread;
}

static u64
os_write(u64 fd, Rng1U64 r, void *data)
{
	if (fd == 0) {
		return 0;
	}
	u64 size = dim1u64(r);
	u64 nwrite = 0;
	u64 nleft = size;
	for (; nleft > 0;) {
		int n = pwrite(fd, (u8 *)data + nwrite, nleft, r.min + nwrite);
		if (n >= 0) {
			nwrite += n;
			nleft -= n;
		} else if (errno != EINTR) {
			break;
		}
	}
	return nwrite;
}

static b32
os_set_times(u64 fd, DateTime dt)
{
	if (fd == 0) {
		return 0;
	}
	timespec ts = os_datetime_to_timespec(dt);
	timespec times[2] = {ts, ts};
	return futimens(fd, times) != -1;
}

static FileProperties
os_fstat(u64 fd)
{
	if (fd == 0) {
		return (FileProperties){0};
	}
	struct stat st = {0};
	FileProperties props = {0};
	if (fstat(fd, &st) != -1) {
		props = os_stat_to_props(&st);
	}
	return props;
}

static b32
os_remove(String8 path)
{
	Temp scratch = temp_begin(arena);
	b32 ok = 0;
	String8 p = push_str8_copy(scratch.a, path);
	if (remove((char *)p.str) != -1) {
		ok = 1;
	}
	temp_end(scratch);
	return ok;
}

static String8
os_abspath(String8 path)
{
	Temp scratch = temp_begin(arena);
	String8 p = push_str8_copy(scratch.a, path);
	char buf[PATH_MAX] = {0};
	if (realpath((char *)p.str, buf) == NULL) {
		temp_end(scratch);
		return str8_zero();
	}
	String8 s = push_str8_copy(scratch.a, str8_cstr(buf));
	temp_end(scratch);
	return s;
}

static b32
os_file_exists(String8 path)
{
	Temp scratch = temp_begin(arena);
	String8 p = push_str8_copy(scratch.a, path);
	b32 ok = 0;
	if (access((char *)p.str, F_OK) == 0) {
		ok = 1;
	}
	temp_end(scratch);
	return ok;
}

static b32
os_dir_exists(String8 path)
{
	Temp scratch = temp_begin(arena);
	b32 ok = 0;
	String8 p = push_str8_copy(scratch.a, path);
	DIR *d = opendir((char *)p.str);
	if (d != NULL) {
		closedir(d);
		ok = 1;
	}
	temp_end(scratch);
	return ok;
}

static FileProperties
os_stat(String8 path)
{
	Temp scratch = temp_begin(arena);
	String8 p = push_str8_copy(scratch.a, path);
	struct stat st = {0};
	FileProperties props = {0};
	if (stat((char *)p.str, &st) != -1) {
		props = os_stat_to_props(&st);
	}
	temp_end(scratch);
	return props;
}

static b32
os_mkdir(String8 path)
{
	Temp scratch = temp_begin(arena);
	String8 p = push_str8_copy(scratch.a, path);
	b32 ok = 0;
	if (mkdir((char *)p.str, 0755) != -1) {
		ok = 1;
	}
	temp_end(scratch);
	return ok;
}

static u64
os_now_us(void)
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * MILLION(1) + (ts.tv_nsec / THOUSAND(1));
}

static u32
os_now_unix(void)
{
	return (u32)time(0);
}

static DateTime
os_now_utc(void)
{
	time_t t = 0;
	time(&t);
	tm ut = {0};
	gmtime_r(&t, &ut);
	return os_tm_to_datetime(ut, 0);
}

static DateTime
os_local_to_utc(DateTime dt)
{
	tm lt = os_datetime_to_tm(dt);
	lt.tm_isdst = -1;
	time_t t = mktime(&lt);
	tm ut = {0};
	gmtime_r(&t, &ut);
	return os_tm_to_datetime(ut, 0);
}

static DateTime
os_utc_to_local(DateTime dt)
{
	tm ut = os_datetime_to_tm(dt);
	ut.tm_isdst = -1;
	time_t t = timegm(&ut);
	tm lt = {0};
	localtime_r(&t, &lt);
	return os_tm_to_datetime(lt, 0);
}

static void
os_sleep_ms(u32 msec)
{
	usleep(msec * THOUSAND(1));
}
