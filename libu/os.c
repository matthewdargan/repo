static String8list
osargs(Arena *a, int argc, char **argv)
{
	String8list list;
	int i;
	String8 s;

	memset(&list, 0, sizeof list);
	for (i = 0; i < argc; i++) {
		s = str8cstr(argv[i]);
		str8listpush(a, &list, s);
	}
	return list;
}

static String8
readfile(Arena *a, String8 path)
{
	u64 fd;
	Fprops props;
	String8 data;

	fd = openfd(path, O_RDONLY);
	props = osfstat(fd);
	data = readfilerng(a, fd, rng1u64(0, props.size));
	closefd(fd);
	return data;
}

static b32
writefile(String8 path, String8 data)
{
	b32 ok;
	u64 fd;

	ok = 0;
	fd = openfd(path, O_WRONLY);
	if (fd != 0) {
		ok = 1;
		writerng(fd, rng1u64(0, data.len), data.str);
		closefd(fd);
	}
	return ok;
}

static b32
appendfile(String8 path, String8 data)
{
	b32 ok;
	u64 fd, pos;

	ok = 0;
	if (data.len != 0) {
		fd = openfd(path, O_WRONLY | O_APPEND | O_CREAT);
		if (fd != 0) {
			ok = 1;
			pos = osfstat(fd).size;
			writerng(fd, rng1u64(pos, pos + data.len), data.str);
			closefd(fd);
		}
	}
	return ok;
}

static String8
readfilerng(Arena *a, u64 fd, Rng1u64 r)
{
	u64 pre, nread;
	String8 s;

	pre = arenapos(a);
	s.len = dim1u64(r);
	s.str = pusharrnoz(a, u8, s.len);
	nread = readrng(fd, r, s.str);
	if (nread < s.len) {
		arenapopto(a, pre + nread);
		s.len = nread;
	}
	return s;
}

static Datetime
tmtodatetime(tm t, u32 msec)
{
	Datetime dt;

	dt.msec = msec;
	dt.sec = t.tm_sec;
	dt.min = t.tm_min;
	dt.hour = t.tm_hour;
	dt.day = t.tm_mday - 1;
	dt.mon = t.tm_mon;
	dt.year = t.tm_year + 1900;
	return dt;
}

static tm
datetimetotm(Datetime dt)
{
	tm t;

	memset(&t, 0, sizeof t);
	t.tm_sec = dt.sec;
	t.tm_min = dt.min;
	t.tm_hour = dt.hour;
	t.tm_mday = dt.day + 1;
	t.tm_mon = dt.mon;
	t.tm_year = dt.year - 1900;
	return t;
}

static timespec
datetimetotimespec(Datetime dt)
{
	tm t;
	time_t sec;
	timespec ts;

	t = datetimetotm(dt);
	sec = timegm(&t);
	memset(&ts, 0, sizeof ts);
	ts.tv_sec = sec;
	return ts;
}

static u64
timespectodense(timespec ts)
{
	tm t;
	Datetime dt;

	gmtime_r(&ts.tv_sec, &t);
	dt = tmtodatetime(t, ts.tv_nsec / 1000000);
	return datetimetodense(dt);
}

static Fprops
stattoprops(struct stat *st)
{
	Fprops props;

	memset(&props, 0, sizeof props);
	props.size = st->st_size;
	props.modified = timespectodense(st->st_mtim);
	props.created = timespectodense(st->st_ctim);
	if (st->st_mode & S_IFDIR)
		props.flags |= ISDIR;
	return props;
}

static String8
cwd(Arena *a)
{
	char *cwd;
	String8 s;

	cwd = getcwd(0, 0);
	s = pushstr8cpy(a, str8cstr(cwd));
	free(cwd);
	return s;
}

static void *
osreserve(u64 size)
{
	void *p;

	p = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		p = NULL;
	return p;
}

static b32
oscommit(void *p, u64 size)
{
	mprotect(p, size, PROT_READ | PROT_WRITE);
	return 1;
}

static void
osdecommit(void *p, u64 size)
{
	madvise(p, size, MADV_DONTNEED);
	mprotect(p, size, PROT_NONE);
}

static void
osrelease(void *p, u64 size)
{
	munmap(p, size);
}

static void *
osreservelarge(u64 size)
{
	void *p;

	p = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (p == MAP_FAILED)
		p = NULL;
	return p;
}

static u64
openfd(String8 path, int flags)
{
	Temp scratch;
	String8 p;
	int fd;

	scratch = tempbegin(arena);
	p = pushstr8cpy(scratch.a, path);
	fd = open((char *)p.str, flags, 0755);
	tempend(scratch);
	if (fd == -1)
		return 0;
	return (u64)fd;
}

static void
closefd(u64 fd)
{
	if (fd == 0)
		return;
	close(fd);
}

static u64
readrng(u64 fd, Rng1u64 r, void *out)
{
	u64 size, nread, nleft;
	int n;

	if (fd == 0)
		return 0;
	size = dim1u64(r);
	nread = 0;
	nleft = size;
	while (nleft > 0) {
		n = pread(fd, (u8 *)out + nread, nleft, r.min + nread);
		if (n >= 0) {
			nread += n;
			nleft -= n;
		} else if (errno != EINTR)
			break;
	}
	return nread;
}

static u64
writerng(u64 fd, Rng1u64 r, void *data)
{
	u64 size, nwrite, nleft;
	int n;

	if (fd == 0)
		return 0;
	size = dim1u64(r);
	nwrite = 0;
	nleft = size;
	while (nleft > 0) {
		n = pwrite(fd, (u8 *)data + nwrite, nleft, r.min + nwrite);
		if (n >= 0) {
			nwrite += n;
			nleft -= n;
		} else if (errno != EINTR)
			break;
	}
	return nwrite;
}

static b32
settimes(u64 fd, Datetime dt)
{
	timespec ts, times[2];

	if (fd == 0)
		return 0;
	ts = datetimetotimespec(dt);
	times[0] = ts;
	times[1] = ts;
	return futimens(fd, times) != -1;
}

static Fprops
osfstat(u64 fd)
{
	struct stat st;
	Fprops props;

	memset(&props, 0, sizeof props);
	if (fd == 0)
		return props;
	if (fstat(fd, &st) != -1)
		props = stattoprops(&st);
	return props;
}

static b32
osremove(String8 path)
{
	Temp scratch;
	b32 ok;
	String8 p;

	scratch = tempbegin(arena);
	ok = 0;
	p = pushstr8cpy(scratch.a, path);
	if (remove((char *)p.str) != -1)
		ok = 1;
	tempend(scratch);
	return ok;
}

static String8
abspath(String8 path)
{
	Temp scratch;
	String8 p, s;
	char buf[PATH_MAX];

	scratch = tempbegin(arena);
	p = pushstr8cpy(scratch.a, path);
	if (realpath((char *)p.str, buf) == NULL) {
		tempend(scratch);
		return str8zero();
	}
	s = pushstr8cpy(scratch.a, str8cstr(buf));
	tempend(scratch);
	return s;
}

static b32
fileexists(String8 path)
{
	Temp scratch;
	String8 p;
	b32 ok;

	scratch = tempbegin(arena);
	p = pushstr8cpy(scratch.a, path);
	ok = 0;
	if (access((char *)p.str, F_OK) == 0)
		ok = 1;
	tempend(scratch);
	return ok;
}

static b32
direxists(String8 path)
{
	Temp scratch;
	String8 p;
	b32 ok;
	DIR *d;

	scratch = tempbegin(arena);
	p = pushstr8cpy(scratch.a, path);
	ok = 0;
	d = opendir((char *)p.str);
	if (d != NULL) {
		closedir(d);
		ok = 1;
	}
	tempend(scratch);
	return ok;
}

static Fprops
osstat(String8 path)
{
	Temp scratch;
	String8 p;
	struct stat st;
	Fprops props;

	scratch = tempbegin(arena);
	p = pushstr8cpy(scratch.a, path);
	memset(&props, 0, sizeof props);
	if (stat((char *)p.str, &st) != -1)
		props = stattoprops(&st);
	tempend(scratch);
	return props;
}

static b32
osmkdir(String8 path)
{
	Temp scratch;
	String8 p;
	b32 ok;

	scratch = tempbegin(arena);
	p = pushstr8cpy(scratch.a, path);
	ok = 0;
	if (mkdir((char *)p.str, 0755) != -1)
		ok = 1;
	tempend(scratch);
	return ok;
}

static u64
nowus(void)
{
	timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000 + (ts.tv_nsec / 1000);
}

static u32
nowunix(void)
{
	return time(0);
}

static Datetime
nowutc(void)
{
	time_t t;
	tm ut;

	t = 0;
	time(&t);
	gmtime_r(&t, &ut);
	return tmtodatetime(ut, 0);
}

static Datetime
localtoutc(Datetime dt)
{
	tm lt, ut;
	time_t t;

	lt = datetimetotm(dt);
	lt.tm_isdst = -1;
	t = mktime(&lt);
	gmtime_r(&t, &ut);
	return tmtodatetime(ut, 0);
}

static Datetime
utctolocal(Datetime dt)
{
	tm ut, lt;
	time_t t;

	ut = datetimetotm(dt);
	ut.tm_isdst = -1;
	t = timegm(&ut);
	localtime_r(&t, &lt);
	return tmtodatetime(lt, 0);
}

static void
sleepms(u32 msec)
{
	usleep(msec * 1000);
}
