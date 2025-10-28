#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* clang-format off */
#include "libu/u.h"
#include "libu/arena.h"
#include "libu/string.h"
#include "libu/cmd.h"
#include "libu/os.h"
#include "libu/socket.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/cmd.c"
#include "libu/os.c"
#include "libu/socket.c"
/* clang-format on */

static String8
resolvehost(Arena *a, String8 host)
{
	char hostbuf[1024] = {0};
	if (host.len >= sizeof hostbuf)
	{
		return str8zero();
	}
	memcpy(hostbuf, host.str, host.len);
	hostbuf[host.len] = 0;
	struct addrinfo *ai = NULL;
	int ret = getaddrinfo(hostbuf, NULL, NULL, &ai);
	if (ret != 0)
	{
		fprintf(stderr, "9mount: getaddrinfo %.*s: %s\n", str8varg(host), gai_strerror(ret));
		return str8zero();
	}
	char ipbuf[INET6_ADDRSTRLEN] = {0};
	ret = getnameinfo(ai->ai_addr, ai->ai_addrlen, ipbuf, sizeof ipbuf, NULL, 0, NI_NUMERICHOST);
	if (ret != 0)
	{
		fprintf(stderr, "9mount: getnameinfo: %s\n", gai_strerror(ret));
		freeaddrinfo(ai);
		return str8zero();
	}
	String8 s = pushstr8cpy(a, str8cstr(ipbuf));
	freeaddrinfo(ai);
	return s;
}

int
main(int argc, char *argv[])
{
	sysinfo = (Sysinfo){
	    .nprocs = sysconf(_SC_NPROCESSORS_ONLN),
	    .pagesz = sysconf(_SC_PAGESIZE),
	    .lpagesz = 0x200000,
	};
	Arena *arena = arena_alloc();
	String8list args = os_args(arena, argc, argv);
	Cmd parsed = cmdparse(arena, args);
	b32 dryrun = cmdhasflag(&parsed, str8lit("n"));
	b32 singleattach = cmdhasflag(&parsed, str8lit("s"));
	b32 exclusive = cmdhasflag(&parsed, str8lit("x"));
	String8 aname = cmdstr(&parsed, str8lit("a"));
	String8 msizestr = cmdstr(&parsed, str8lit("m"));
	String8 uidstr = cmdstr(&parsed, str8lit("u"));
	String8 gidstr = cmdstr(&parsed, str8lit("g"));
	if (parsed.inputs.nnode != 2)
	{
		fprintf(stderr, "usage: 9mount [-nsx] [-a spec] [-m msize] [-u uid] [-g gid] dial mtpt\n");
		return 1;
	}
	String8 dial = parsed.inputs.start->str;
	String8 mtpt = parsed.inputs.start->next->str;
	uid_t uid = uidstr.len > 0 ? (uid_t)str8tou64(uidstr, 10) : getuid();
	gid_t gid = gidstr.len > 0 ? (gid_t)str8tou64(gidstr, 10) : getgid();
	struct passwd *pw = getpwuid(uid);
	if (pw == NULL)
	{
		fprintf(stderr, "9mount: unknown uid %d\n", uid);
		return 1;
	}
	struct stat st = {0};
	if (stat((char *)mtpt.str, &st) || access((char *)mtpt.str, W_OK))
	{
		fprintf(stderr, "9mount: %.*s: %s\n", str8varg(mtpt), strerror(errno));
		return 1;
	}
	if (st.st_mode & S_ISVTX)
	{
		fprintf(stderr, "9mount: refusing to mount over sticky directory %.*s\n", str8varg(mtpt));
		return 1;
	}
	String8list opts = {0};
	String8 addr = str8zero();
	if (str8cmp(dial, str8lit("-"), 0))
	{
		addr = str8lit("nodev");
		str8listpush(arena, &opts, str8lit("trans=fd,rfdno=0,wrfdno=1"));
	}
	else
	{
		Netaddr na = netaddr(arena, dial, str8lit("tcp"), str8lit("9pfs"));
		if (na.net.len == 0)
		{
			fprintf(stderr, "9mount: invalid dial string %.*s\n", str8varg(dial));
			return 1;
		}
		if (na.isunix)
		{
			addr = na.host;
			if (str8cmp(na.net, str8lit("virtio"), 0))
			{
				str8listpush(arena, &opts, str8lit("trans=virtio"));
			}
			else
			{
				str8listpush(arena, &opts, str8lit("trans=unix"));
			}
		}
		else if (str8cmp(na.net, str8lit("tcp"), 0))
		{
			addr = resolvehost(arena, na.host);
			if (addr.len == 0)
			{
				return 1;
			}
			if (na.port == 0)
			{
				fprintf(stderr, "9mount: port resolution failed\n");
				return 1;
			}
			str8listpush(arena, &opts, pushstr8f(arena, "trans=tcp,port=%lu", na.port));
		}
		else
		{
			fprintf(stderr, "9mount: unsupported network type %.*s\n", str8varg(na.net));
			return 1;
		}
	}
	String8 user = str8cstr(pw->pw_name);
	str8listpush(arena, &opts, pushstr8f(arena, "uname=%.*s", (int)user.len, user.str));
	if (aname.len > 0)
	{
		str8listpush(arena, &opts, pushstr8f(arena, "aname=%.*s", (int)aname.len, aname.str));
	}
	if (msizestr.len > 0)
	{
		u64 msize = str8tou64(msizestr, 10);
		if (msize == 0)
		{
			fprintf(stderr, "9mount: invalid msize %lu\n", msize);
			return 1;
		}
		str8listpush(arena, &opts, pushstr8f(arena, "msize=%lu", msize));
	}
	str8listpush(arena, &opts, str8lit("noextend"));
	str8listpush(arena, &opts, pushstr8f(arena, "dfltuid=%d,dfltgid=%d", uid, gid));
	if (singleattach)
	{
		str8listpush(arena, &opts, str8lit("access=any"));
	}
	else if (exclusive)
	{
		str8listpush(arena, &opts, pushstr8f(arena, "access=%d", uid));
	}
	Stringjoin join = {
	    .pre = str8zero(),
	    .sep = str8lit(","),
	    .post = str8zero(),
	};
	String8 optstr = str8listjoin(arena, &opts, &join);
	if (dryrun)
	{
		fprintf(stderr, "mount -t 9p -o %.*s %.*s %.*s\n", str8varg(optstr), str8varg(addr), str8varg(mtpt));
	}
	else if (mount((char *)addr.str, (char *)mtpt.str, "9p", 0, (char *)optstr.str))
	{
		fprintf(stderr, "9mount: mount failed: %s\n", strerror(errno));
		return 1;
	}
	arena_release(arena);
	return 0;
}
