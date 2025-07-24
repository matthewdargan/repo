#include <dirent.h>
#include <errno.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* clang-format off */
#include "libu/u.h"
#include "libu/arena.h"
#include "libu/string.h"
#include "libu/cmd.h"
#include "libu/os.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/cmd.c"
#include "libu/os.c"
/* clang-format on */

static String8
resolvehost(Arena *a, String8 host)
{
	char hostbuf[1024], ipbuf[INET6_ADDRSTRLEN];
	struct addrinfo *ai;
	int ret;
	String8 s;

	if (host.len >= sizeof hostbuf)
		return str8zero();
	memcpy(hostbuf, host.str, host.len);
	hostbuf[host.len] = 0;
	ret = getaddrinfo(hostbuf, NULL, NULL, &ai);
	if (ret != 0) {
		fprintf(stderr, "9mount: getaddrinfo %.*s: %s\n", (int)host.len, host.str, gai_strerror(ret));
		return str8zero();
	}
	ret = getnameinfo(ai->ai_addr, ai->ai_addrlen, ipbuf, sizeof ipbuf, NULL, 0, NI_NUMERICHOST);
	if (ret != 0) {
		fprintf(stderr, "9mount: getnameinfo: %s\n", gai_strerror(ret));
		freeaddrinfo(ai);
		return str8zero();
	}
	s = pushstr8cpy(a, str8cstr(ipbuf));
	freeaddrinfo(ai);
	return s;
}

static u64
resolveport(String8 port)
{
	char portbuf[1024];
	struct servent *sv;

	if (str8isint(port, 10))
		return str8tou64(port, 10);
	if (port.len >= sizeof portbuf) {
		fprintf(stderr, "9mount: service name too long\n");
		return 0;
	}
	memcpy(portbuf, port.str, port.len);
	portbuf[port.len] = 0;
	sv = getservbyname(portbuf, "tcp");
	if (sv != NULL) {
		endservent();
		return ntohs((uint16_t)sv->s_port);
	}
	fprintf(stderr, "9mount: unknown service %.*s\n", (int)port.len, port.str);
	return 0;
}

int
main(int argc, char *argv[])
{
	Arenaparams ap;
	String8list args, opts;
	Cmd parsed;
	b32 dryrun, singleattach, exclusive;
	String8 aname, msizestr, uidstr, gidstr, dial, mtpt, host, portstr, addr, user, optstr;
	uid_t uid;
	gid_t gid;
	struct passwd *pw;
	struct stat st;
	Stringjoin join;
	u64 bang, port, msize;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	dryrun = cmdhasflag(&parsed, str8lit("n"));
	singleattach = cmdhasflag(&parsed, str8lit("s"));
	exclusive = cmdhasflag(&parsed, str8lit("x"));
	aname = cmdstr(&parsed, str8lit("a"));
	msizestr = cmdstr(&parsed, str8lit("m"));
	uidstr = cmdstr(&parsed, str8lit("u"));
	gidstr = cmdstr(&parsed, str8lit("g"));
	if (parsed.inputs.nnode != 2) {
		fprintf(stderr, "usage: 9mount [-nsx] [-a spec] [-m msize] [-u uid] [-g gid] dial mtpt\n");
		return 1;
	}
	dial = parsed.inputs.start->str;
	mtpt = parsed.inputs.start->next->str;
	if (uidstr.len > 0)
		uid = (uid_t)str8tou64(uidstr, 10);
	else
		uid = getuid();
	if (gidstr.len > 0)
		gid = (gid_t)str8tou64(gidstr, 10);
	else
		gid = getgid();
	pw = getpwuid(uid);
	if (pw == NULL) {
		fprintf(stderr, "9mount: unknown uid %d\n", uid);
		return 1;
	}
	if (stat((char *)mtpt.str, &st) || access((char *)mtpt.str, W_OK)) {
		fprintf(stderr, "9mount: %.*s: %s\n", (int)mtpt.len, mtpt.str, strerror(errno));
		return 1;
	}
	if (st.st_mode & S_ISVTX) {
		fprintf(stderr, "9mount: refusing to mount over sticky directory %.*s\n", (int)mtpt.len, mtpt.str);
		return 1;
	}
	memset(&opts, 0, sizeof opts);
	if (str8cmp(dial, str8lit("-"), 0)) {
		addr = str8lit("nodev");
		str8listpush(arena, &opts, str8lit("trans=fd,rfdno=0,wrfdno=1"));
	} else if (str8index(dial, 0, str8lit("unix!"), 0) == 0) {
		addr = str8skip(dial, 5);
		str8listpush(arena, &opts, str8lit("trans=unix"));
	} else if (str8index(dial, 0, str8lit("virtio!"), 0) == 0) {
		addr = str8skip(dial, 7);
		str8listpush(arena, &opts, str8lit("trans=virtio"));
	} else if (str8index(dial, 0, str8lit("tcp!"), 0) == 0) {
		dial = str8skip(dial, 4);
		bang = str8index(dial, 0, str8lit("!"), 0);
		if (bang < dial.len) {
			host = str8prefix(dial, bang);
			portstr = str8skip(dial, bang + 1);
			if (portstr.len == 0) {
				fprintf(stderr, "9mount: invalid dial string tcp!%.*s\n", (int)dial.len, dial.str);
				return 1;
			}
		} else {
			host = dial;
			portstr = str8lit("564");
		}
		addr = resolvehost(arena, host);
		if (addr.len == 0)
			return 1;
		port = resolveport(portstr);
		if (port == 0)
			return 1;
		str8listpush(arena, &opts, pushstr8f(arena, "trans=tcp,port=%lu", port));
	} else {
		fprintf(stderr, "9mount: invalid dial string %.*s\n", (int)dial.len, dial.str);
		return 1;
	}
	user = str8cstr(pw->pw_name);
	str8listpush(arena, &opts, pushstr8f(arena, "uname=%.*s", (int)user.len, user.str));
	if (aname.len > 0)
		str8listpush(arena, &opts, pushstr8f(arena, "aname=%.*s", (int)aname.len, aname.str));
	if (msizestr.len > 0) {
		msize = str8tou64(msizestr, 10);
		if (msize == 0) {
			fprintf(stderr, "9mount: invalid msize %lu\n", msize);
			return 1;
		}
		str8listpush(arena, &opts, pushstr8f(arena, "msize=%lu", msize));
	}
	str8listpush(arena, &opts, str8lit("noextend"));
	str8listpush(arena, &opts, pushstr8f(arena, "dfltuid=%d,dfltgid=%d", uid, gid));
	if (singleattach)
		str8listpush(arena, &opts, str8lit("access=any"));
	else if (exclusive)
		str8listpush(arena, &opts, pushstr8f(arena, "access=%d", uid));
	join.pre = str8zero();
	join.sep = str8lit(",");
	join.post = str8zero();
	optstr = str8listjoin(arena, &opts, &join);
	if (dryrun)
		fprintf(stderr, "mount -t 9p -o %.*s %.*s %.*s\n", (int)optstr.len, optstr.str, (int)addr.len, addr.str,
		        (int)mtpt.len, mtpt.str);
	else if (mount((char *)addr.str, (char *)mtpt.str, "9p", 0, (char *)optstr.str)) {
		fprintf(stderr, "9mount: mount failed: %s\n", strerror(errno));
		return 1;
	}
	arenarelease(arena);
	return 0;
}
