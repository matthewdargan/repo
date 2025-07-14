#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
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

static char *
str8tocstr(Arena *a, String8 s)
{
	char *cstr;

	cstr = pusharr(a, char, s.len + 1);
	memcpy(cstr, s.str, s.len);
	cstr[s.len] = 0;
	return cstr;
}

static b32
str8startswith(String8 s, String8 prefix)
{
	return s.len >= prefix.len && memcmp(s.str, prefix.str, prefix.len) == 0;
}

static b32
isvalidarg(String8 s)
{
	u64 i;
	u8 c;

	for (i = 0; i < s.len; i++) {
		c = s.str[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
			return 0;
	}
	return 1;
}

static String8
resolve_host(Arena *a, String8 host)
{
	char *host_cstr;
	struct addrinfo *ai;
	int r;
	char ipstr[INET6_ADDRSTRLEN];

	host_cstr = str8tocstr(a, host);
	if ((r = getaddrinfo(host_cstr, 0, 0, &ai))) {
		fprintf(stderr, "9mount: getaddrinfo: %s\n", gai_strerror(r));
		exit(1);
	}
	r = getnameinfo(ai->ai_addr, ai->ai_addrlen, ipstr, sizeof(ipstr), 0, 0, NI_NUMERICHOST);
	if (r) {
		fprintf(stderr, "9mount: getnameinfo: %s\n", gai_strerror(r));
		exit(1);
	}
	freeaddrinfo(ai);
	return pushstr8f(a, "%s", ipstr);
}

static b32
str8alldigits(String8 s)
{
	u64 i;
	for (i = 0; i < s.len; i++) {
		if (s.str[i] < '0' || s.str[i] > '9')
			return 0;
	}
	return 1;
}

static u16
resolve_port(String8 port)
{
	char *port_cstr;
	struct servent *sv;

	if (str8alldigits(port)) {
		return (u16)str8tou64(port, 10);
	}
	port_cstr = str8tocstr(arena, port);
	if ((sv = getservbyname(port_cstr, "tcp"))) {
		endservent();
		return ntohs((uint16_t)sv->s_port);
	}
	fprintf(stderr, "9mount: unknown service %.*s\n", (int)port.len, port.str);
	exit(1);
}

int
main(int argc, char *argv[])
{
	Arenaparams ap;
	String8list args, optlist;
	Cmd parsed;
	String8 dial, mountpt, addr, aname, msize, user, opts, uidstr, gidstr;
	b32 dryrun, extend, dev, singleattach, exclusive;
	struct passwd *pw;
	struct stat st;
	Stringjoin join;
	char *addr_cstr, *mountpt_cstr, *opts_cstr;
	uid_t uid;
	gid_t gid;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	aname = cmdstr(&parsed, str8lit("a"));
	msize = cmdstr(&parsed, str8lit("m"));
	extend = cmdhasflag(&parsed, str8lit("e"));
	uidstr = cmdstr(&parsed, str8lit("u"));
	gidstr = cmdstr(&parsed, str8lit("g"));
	dev = cmdhasflag(&parsed, str8lit("v"));
	singleattach = cmdhasflag(&parsed, str8lit("s"));
	exclusive = cmdhasflag(&parsed, str8lit("x"));
	dryrun = cmdhasflag(&parsed, str8lit("n"));
	if (parsed.inputs.nnode != 2) {
		fprintf(stderr, "usage: 9mount [-evsxn] [-a spec] [-m msize] [-u uid] [-g gid] dial mountpt");
		return 1;
	}
	dial = parsed.inputs.start->str;
	mountpt = parsed.inputs.start->next->str;
	printf("dryrun=%d\n", dryrun);
	printf("extend=%d\n", extend);
	printf("dev=%d\n", dev);
	printf("singleattach=%d\n", singleattach);
	printf("exclusive=%d\n", exclusive);
	printf("aname=%.*s\n", (int)aname.len, aname.str);
	printf("msize=%.*s\n", (int)msize.len, msize.str);
	printf("dial=%.*s\n", (int)dial.len, dial.str);
	printf("mountpt=%.*s\n", (int)mountpt.len, mountpt.str);
	gid = getgid();
	uid = getuid();
	if (uidstr.len)
		uid = (uid_t)str8tou64(uidstr, 10);
	if (gidstr.len)
		gid = (gid_t)str8tou64(gidstr, 10);
	pw = getpwuid(uid);
	if (pw == NULL) {
		perror("9mount: getpwuid failed");
		return 1;
	}
	mountpt_cstr = str8tocstr(arena, mountpt);
	if (stat(mountpt_cstr, &st) || access(mountpt_cstr, W_OK)) {
		perror(mountpt_cstr);
		return 1;
	}
	if (st.st_mode & S_ISVTX) {
		fprintf(stderr, "9mount: refusing to mount over sticky directory %s\n", mountpt_cstr);
		return 1;
	}
	memset(&optlist, 0, sizeof optlist);
	if (str8cmp(dial, str8lit("-"), 0)) {
		addr = str8lit("nodev");
		str8listpush(arena, &optlist, str8lit("trans=fd,rfdno=0,wrfdno=1"));
	} else if (str8startswith(dial, str8lit("virtio:"))) {
		addr = str8skip(dial, 7);
		str8listpush(arena, &optlist, str8lit("trans=virtio"));
	} else if (str8index(dial, 0, str8lit("/"), 0) < dial.len ||
	           (stat(str8tocstr(arena, dial), &st) == 0 && S_ISSOCK(st.st_mode))) {
		addr = dial;
		str8listpush(arena, &optlist, str8lit("trans=unix"));
	} else {
		u64 colon;
		String8 host, portstr;
		u16 port;

		colon = str8index(dial, 0, str8lit(":"), 0);
		if (colon == dial.len) {
			fprintf(stderr, "9mount: invalid dial string %.*s\n", (int)dial.len, dial.str);
			return 1;
		}
		host = str8prefix(dial, colon);
		portstr = str8skip(dial, colon + 1);
		addr = resolve_host(arena, host);
		port = resolve_port(portstr);
		str8listpush(arena, &optlist, pushstr8f(arena, "trans=tcp,port=%u", port));
		if (aname.len) {
			if (!isvalidarg(aname)) {
				fprintf(stderr, "9mount: aname argument contains invalid characters: %.*s\n", (int)aname.len,
				        aname.str);
				return 1;
			}
			str8listpush(arena, &optlist, pushstr8f(arena, "aname=%.*s", (int)aname.len, aname.str));
		}
		if (msize.len) {
			u64 maxdata = str8tou64(msize, 10);
			if (maxdata <= 0) {
				fprintf(stderr, "9mount: invalid maxdata %.*s\n", (int)msize.len, msize.str);
				return 1;
			}
			str8listpush(arena, &optlist, pushstr8f(arena, "maxdata=%lu", maxdata));
		}
		user = str8cstr(getenv("USER"));
		if (user.len == 0)
			user = str8cstr(pw->pw_name);
		if (!isvalidarg(user)) {
			fprintf(stderr, "9mount: username contains invalid characters: %.*s\n", (int)user.len, user.str);
			return 1;
		}
		str8listpush(arena, &optlist, pushstr8f(arena, "uname=%.*s", (int)user.len, user.str));
		if (singleattach)
			str8listpush(arena, &optlist, str8lit("access=any"));
		else if (exclusive)
			str8listpush(arena, &optlist, pushstr8f(arena, "access=%d", uid));
		if (!extend)
			str8listpush(arena, &optlist, str8lit("noextend"));
		if (!dev)
			str8listpush(arena, &optlist, str8lit("nodevmap"));
		str8listpush(arena, &optlist, pushstr8f(arena, "dfltuid=%d,dfltgid=%d", uid, gid));
		join.pre = str8zero();
		join.sep = str8lit(",");
		join.post = str8zero();
		opts = str8listjoin(arena, &optlist, &join);
		addr_cstr = str8tocstr(arena, addr);
		opts_cstr = str8tocstr(arena, opts);
		if (dryrun) {
			fprintf(stderr, "mount -t 9p -o %s %s %s\n", opts_cstr, addr_cstr, mountpt_cstr);
		} else if (mount(addr_cstr, mountpt_cstr, "9p", 0, opts_cstr)) {
			perror("mount");
			return 1;
		}
		arenarelease(arena);
		return 0;
	}
}
