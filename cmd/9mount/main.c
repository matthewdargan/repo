#include <pwd.h>
#include <sys/mount.h>

// clang-format off
#include "base/inc.h"
#include "base/inc.c"
// clang-format on

static String8
resolvehost(Arena *a, String8 host)
{
	char hostbuf[1024] = {0};
	if (host.size >= sizeof hostbuf)
	{
		return str8_zero();
	}
	MemoryCopy(hostbuf, host.str, host.size);
	hostbuf[host.size]  = 0;
	struct addrinfo *ai = 0;
	int ret             = getaddrinfo(hostbuf, 0, 0, &ai);
	if (ret != 0)
	{
		fprintf(stderr, "9mount: getaddrinfo %.*s: %s\n", str8_varg(host), gai_strerror(ret));
		return str8_zero();
	}
	char ipbuf[INET6_ADDRSTRLEN] = {0};
	ret                          = getnameinfo(ai->ai_addr, ai->ai_addrlen, ipbuf, sizeof ipbuf, 0, 0, NI_NUMERICHOST);
	if (ret != 0)
	{
		fprintf(stderr, "9mount: getnameinfo: %s\n", gai_strerror(ret));
		freeaddrinfo(ai);
		return str8_zero();
	}
	String8 s = str8_copy(a, str8_cstring(ipbuf));
	freeaddrinfo(ai);
	return s;
}

static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch     = scratch_begin(0, 0);
	b32 dryrun       = cmd_line_has_flag(cmd_line, str8_lit("n"));
	b32 singleattach = cmd_line_has_flag(cmd_line, str8_lit("s"));
	b32 exclusive    = cmd_line_has_flag(cmd_line, str8_lit("x"));
	String8 aname    = cmd_line_string(cmd_line, str8_lit("a"));
	String8 msizestr = cmd_line_string(cmd_line, str8_lit("m"));
	String8 uidstr   = cmd_line_string(cmd_line, str8_lit("u"));
	String8 gidstr   = cmd_line_string(cmd_line, str8_lit("g"));
	if (cmd_line->inputs.node_count != 2)
	{
		fprintf(stderr, "usage: 9mount [-nsx] [-a spec] [-m msize] [-u uid] [-g gid] dial mtpt\n");
		return;
	}
	String8 dial      = cmd_line->inputs.first->string;
	String8 mtpt      = cmd_line->inputs.first->next->string;
	uid_t uid         = uidstr.size > 0 ? (uid_t)u64_from_str8(uidstr, 10) : getuid();
	gid_t gid         = gidstr.size > 0 ? (gid_t)u64_from_str8(gidstr, 10) : getgid();
	struct passwd *pw = getpwuid(uid);
	if (pw == 0)
	{
		fprintf(stderr, "9mount: unknown uid %d\n", uid);
		return;
	}
	struct stat st = {0};
	if (stat((char *)mtpt.str, &st) || access((char *)mtpt.str, W_OK))
	{
		fprintf(stderr, "9mount: %.*s: %s\n", str8_varg(mtpt), strerror(errno));
		return;
	}
	if (st.st_mode & S_ISVTX)
	{
		fprintf(stderr, "9mount: refusing to mount over sticky directory %.*s\n", str8_varg(mtpt));
		return;
	}
	String8List opts = {0};
	String8 addr     = str8_zero();
	if (str8_match(dial, str8_lit("-"), 0))
	{
		addr = str8_lit("nodev");
		str8_list_push(scratch.arena, &opts, str8_lit("trans=fd,rfdno=0,wrfdno=1"));
	}
	else
	{
		Netaddr na = netaddr(scratch.arena, dial, str8_lit("tcp"), str8_lit("9pfs"));
		if (na.net.size == 0)
		{
			fprintf(stderr, "9mount: invalid dial string %.*s\n", str8_varg(dial));
			return;
		}
		if (na.isunix)
		{
			addr = na.host;
			if (str8_match(na.net, str8_lit("virtio"), 0))
			{
				str8_list_push(scratch.arena, &opts, str8_lit("trans=virtio"));
			}
			else
			{
				str8_list_push(scratch.arena, &opts, str8_lit("trans=unix"));
			}
		}
		else if (str8_match(na.net, str8_lit("tcp"), 0))
		{
			addr = resolvehost(scratch.arena, na.host);
			if (addr.size == 0)
			{
				return;
			}
			if (na.port == 0)
			{
				fprintf(stderr, "9mount: port resolution failed\n");
				return;
			}
			str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "trans=tcp,port=%lu", na.port));
		}
		else
		{
			fprintf(stderr, "9mount: unsupported network type %.*s\n", str8_varg(na.net));
			return;
		}
	}
	String8 user = str8_cstring(pw->pw_name);
	str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "uname=%S", user));
	if (aname.size > 0)
	{
		str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "aname=%S", aname));
	}
	if (msizestr.size > 0)
	{
		u64 msize = u64_from_str8(msizestr, 10);
		if (msize == 0)
		{
			fprintf(stderr, "9mount: invalid msize %lu\n", msize);
			return;
		}
		str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "msize=%lu", msize));
	}
	str8_list_push(scratch.arena, &opts, str8_lit("noextend"));
	str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "dfltuid=%d,dfltgid=%d", uid, gid));
	if (singleattach)
	{
		str8_list_push(scratch.arena, &opts, str8_lit("access=any"));
	}
	else if (exclusive)
	{
		str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "access=%d", uid));
	}
	StringJoin join = {
	    .pre  = str8_zero(),
	    .sep  = str8_lit(","),
	    .post = str8_zero(),
	};
	String8 optstr = str8_list_join(scratch.arena, &opts, &join);
	if (dryrun)
	{
		fprintf(stderr, "mount -t 9p -o %.*s %.*s %.*s\n", str8_varg(optstr), str8_varg(addr), str8_varg(mtpt));
	}
	else if (mount((char *)addr.str, (char *)mtpt.str, "9p", 0, (char *)optstr.str))
	{
		fprintf(stderr, "9mount: mount failed: %s\n", strerror(errno));
		return;
	}
	scratch_end(scratch);
}
