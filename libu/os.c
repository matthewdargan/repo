static String8list
osargs(Arena *a, int argc, char **argv)
{
	String8list list = {0};
	for (int i = 0; i < argc; i++)
	{
		String8 s = str8cstr(argv[i]);
		str8listpush(a, &list, s);
	}
	return list;
}

static String8
readfile(Arena *a, String8 path)
{
	u64 fd = openfd(a, path, O_RDONLY);
	Fprops props = osfstat(fd);
	String8 data = readfilerng(a, fd, rng1u64(0, props.size));
	closefd(fd);
	return data;
}

static b32
writefile(Arena *a, String8 path, String8 data)
{
	b32 ok = 0;
	u64 fd = openfd(a, path, O_WRONLY);
	if (fd != 0)
	{
		ok = 1;
		writerng(fd, rng1u64(0, data.len), data.str);
		closefd(fd);
	}
	return ok;
}

static b32
appendfile(Arena *a, String8 path, String8 data)
{
	b32 ok = 0;
	if (data.len != 0)
	{
		u64 fd = openfd(a, path, O_WRONLY | O_APPEND | O_CREAT);
		if (fd != 0)
		{
			ok = 1;
			u64 pos = osfstat(fd).size;
			writerng(fd, rng1u64(pos, pos + data.len), data.str);
			closefd(fd);
		}
	}
	return ok;
}

static String8
readfilerng(Arena *a, u64 fd, Rng1u64 r)
{
	u64 pre = arenapos(a);
	u64 len = dim1u64(r);
	String8 s = {
	    .str = pusharrnoz(a, u8, len),
	    .len = len,
	};
	u64 nread = readrng(fd, r, s.str);
	if (nread < s.len)
	{
		arenapopto(a, pre + nread);
		s.len = nread;
	}
	return s;
}

static Datetime
tmtodatetime(tm t, u32 msec)
{
	Datetime dt = {
	    .msec = msec,
	    .sec = t.tm_sec,
	    .min = t.tm_min,
	    .hour = t.tm_hour,
	    .day = t.tm_mday - 1,
	    .mon = t.tm_mon,
	    .year = t.tm_year + 1900,
	};
	return dt;
}

static tm
datetimetotm(Datetime dt)
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
datetimetotimespec(Datetime dt)
{
	tm t = datetimetotm(dt);
	time_t sec = timegm(&t);
	timespec ts = {0};
	ts.tv_sec = sec;
	return ts;
}

static u64
timespectodense(timespec ts)
{
	tm t = {0};
	gmtime_r(&ts.tv_sec, &t);
	Datetime dt = tmtodatetime(t, ts.tv_nsec / 1000000);
	return datetimetodense(dt);
}

static Fprops
stattoprops(struct stat *st)
{
	Fprops props = {0};
	props.size = st->st_size;
	props.modified = timespectodense(st->st_mtim);
	props.created = timespectodense(st->st_ctim);
	if (st->st_mode & S_IFDIR)
	{
		props.flags |= ISDIR;
	}
	return props;
}

static String8
cwd(Arena *a)
{
	char *cwd = getcwd(0, 0);
	String8 s = pushstr8cpy(a, str8cstr(cwd));
	free(cwd);
	return s;
}

static void *
osreserve(u64 size)
{
	void *p = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
	{
		p = NULL;
	}
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
	void *p = mmap(0, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
	if (p == MAP_FAILED)
	{
		p = NULL;
	}
	return p;
}

static u64
openfd(Arena *a, String8 path, int flags)
{
	Temp scratch = tempbegin(a);
	String8 p = pushstr8cpy(scratch.a, path);
	int fd = open((char *)p.str, flags, 0755);
	tempend(scratch);
	if (fd == -1)
	{
		return 0;
	}
	return (u64)fd;
}

static void
closefd(u64 fd)
{
	if (fd == 0)
	{
		return;
	}
	close(fd);
}

static u64
readrng(u64 fd, Rng1u64 r, void *out)
{
	if (fd == 0)
	{
		return 0;
	}
	u64 size = dim1u64(r);
	u64 nread = 0;
	u64 nleft = size;
	while (nleft > 0)
	{
		int n = pread(fd, (u8 *)out + nread, nleft, r.min + nread);
		if (n >= 0)
		{
			nread += n;
			nleft -= n;
		}
		else if (errno != EINTR)
		{
			break;
		}
	}
	return nread;
}

static u64
writerng(u64 fd, Rng1u64 r, void *data)
{
	if (fd == 0)
	{
		return 0;
	}
	u64 size = dim1u64(r);
	u64 nwrite = 0;
	u64 nleft = size;
	while (nleft > 0)
	{
		int n = pwrite(fd, (u8 *)data + nwrite, nleft, r.min + nwrite);
		if (n >= 0)
		{
			nwrite += n;
			nleft -= n;
		}
		else if (errno != EINTR)
		{
			break;
		}
	}
	return nwrite;
}

static b32
settimes(u64 fd, Datetime dt)
{
	if (fd == 0)
	{
		return 0;
	}
	timespec ts = datetimetotimespec(dt);
	timespec times[2] = {ts, ts};
	return futimens(fd, times) != -1;
}

static Fprops
osfstat(u64 fd)
{
	Fprops props = {0};
	if (fd == 0)
	{
		return props;
	}
	struct stat st = {0};
	if (fstat(fd, &st) != -1)
	{
		props = stattoprops(&st);
	}
	return props;
}

static b32
osremove(Arena *a, String8 path)
{
	Temp scratch = tempbegin(a);
	b32 ok = 0;
	String8 p = pushstr8cpy(scratch.a, path);
	if (remove((char *)p.str) != -1)
	{
		ok = 1;
	}
	tempend(scratch);
	return ok;
}

static String8
abspath(Arena *a, String8 path)
{
	Temp scratch = tempbegin(a);
	String8 p = pushstr8cpy(scratch.a, path);
	char buf[PATH_MAX];
	if (realpath((char *)p.str, buf) == NULL)
	{
		tempend(scratch);
		return str8zero();
	}
	String8 s = pushstr8cpy(scratch.a, str8cstr(buf));
	tempend(scratch);
	return s;
}

static b32
fileexists(Arena *a, String8 path)
{
	Temp scratch = tempbegin(a);
	String8 p = pushstr8cpy(scratch.a, path);
	b32 ok = 0;
	if (access((char *)p.str, F_OK) == 0)
	{
		ok = 1;
	}
	tempend(scratch);
	return ok;
}

static b32
direxists(Arena *a, String8 path)
{
	Temp scratch = tempbegin(a);
	String8 p = pushstr8cpy(scratch.a, path);
	b32 ok = 0;
	DIR *d = opendir((char *)p.str);
	if (d != NULL)
	{
		closedir(d);
		ok = 1;
	}
	tempend(scratch);
	return ok;
}

static Fprops
osstat(Arena *a, String8 path)
{
	Temp scratch = tempbegin(a);
	String8 p = pushstr8cpy(scratch.a, path);
	Fprops props = {0};
	struct stat st = {0};
	if (stat((char *)p.str, &st) != -1)
	{
		props = stattoprops(&st);
	}
	tempend(scratch);
	return props;
}

static b32
osmkdir(Arena *a, String8 path)
{
	Temp scratch = tempbegin(a);
	String8 p = pushstr8cpy(scratch.a, path);
	b32 ok = 0;
	if (mkdir((char *)p.str, 0755) != -1)
	{
		ok = 1;
	}
	tempend(scratch);
	return ok;
}

static u64
nowus(void)
{
	timespec ts = {0};
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
	time_t t = 0;
	time(&t);
	tm ut = {0};
	gmtime_r(&t, &ut);
	return tmtodatetime(ut, 0);
}

static Datetime
localtoutc(Datetime dt)
{
	tm lt = datetimetotm(dt);
	lt.tm_isdst = -1;
	time_t t = mktime(&lt);
	tm ut = {0};
	gmtime_r(&t, &ut);
	return tmtodatetime(ut, 0);
}

static Datetime
utctolocal(Datetime dt)
{
	tm ut = datetimetotm(dt);
	ut.tm_isdst = -1;
	time_t t = timegm(&ut);
	tm lt = {0};
	localtime_r(&t, &lt);
	return tmtodatetime(lt, 0);
}

static void
sleepms(u32 msec)
{
	usleep(msec * 1000);
}
