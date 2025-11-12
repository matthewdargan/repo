#include <pwd.h>
#include <sys/mount.h>

// clang-format off
#include "base/inc.h"
#include "base/inc.c"
// clang-format on

static String8
resolvehost(Arena *a, String8 host)
{
	String8 host_copy = str8_copy(a, host);
	struct addrinfo *ai = 0;
	int ret = getaddrinfo((char *)host_copy.str, 0, 0, &ai);
	if(ret != 0)
	{
		log_errorf("9mount: getaddrinfo %S: %s\n", host, gai_strerror(ret));
		return str8_zero();
	}
	char ipbuf[INET6_ADDRSTRLEN] = {0};
	ret = getnameinfo(ai->ai_addr, ai->ai_addrlen, ipbuf, sizeof ipbuf, 0, 0, NI_NUMERICHOST);
	if(ret != 0)
	{
		log_errorf("9mount: getnameinfo: %s\n", gai_strerror(ret));
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
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();
	b32 dryrun = cmd_line_has_flag(cmd_line, str8_lit("n"));
	b32 singleattach = cmd_line_has_flag(cmd_line, str8_lit("s"));
	b32 exclusive = cmd_line_has_flag(cmd_line, str8_lit("x"));
	String8 aname = cmd_line_string(cmd_line, str8_lit("a"));
	String8 msizestr = cmd_line_string(cmd_line, str8_lit("m"));
	String8 uidstr = cmd_line_string(cmd_line, str8_lit("u"));
	String8 gidstr = cmd_line_string(cmd_line, str8_lit("g"));

	if(cmd_line->inputs.node_count != 2)
	{
		log_error(str8_lit("usage: 9mount [-nsx] [-a=<spec>] [-m=<msize>] [-u=<uid>] [-g=<gid>] <dial> <mtpt>\n"));
	}
	else
	{
		String8 dial = cmd_line->inputs.first->string;
		String8 mtpt = cmd_line->inputs.first->next->string;
		String8 mtpt_copy = str8_copy(scratch.arena, mtpt);
		uid_t uid = uidstr.size > 0 ? (uid_t)u64_from_str8(uidstr, 10) : getuid();
		gid_t gid = gidstr.size > 0 ? (gid_t)u64_from_str8(gidstr, 10) : getgid();
		struct passwd *pw = getpwuid(uid);
		if(pw == 0)
		{
			log_errorf("9mount: unknown uid %d\n", uid);
		}
		else
		{
			struct stat st = {0};
			if(stat((char *)mtpt_copy.str, &st) || access((char *)mtpt_copy.str, W_OK))
			{
				log_errorf("9mount: %S: %s\n", mtpt, strerror(errno));
			}
			else if(st.st_mode & S_ISVTX)
			{
				log_errorf("9mount: refusing to mount over sticky directory %S\n", mtpt);
			}
			else
			{
				String8List opts = {0};
				String8 addr = str8_zero();
				b32 addr_ok = 1;
				if(str8_match(dial, str8_lit("-"), 0))
				{
					addr = str8_lit("nodev");
					str8_list_push(scratch.arena, &opts, str8_lit("trans=fd,rfdno=0,wrfdno=1"));
				}
				else
				{
					Netaddr na = netaddr(scratch.arena, dial, str8_lit("tcp"), str8_lit("9pfs"));
					if(na.net.size == 0)
					{
						log_errorf("9mount: invalid dial string %S\n", dial);
						addr_ok = 0;
					}
					else if(na.isunix)
					{
						addr = na.host;
						if(str8_match(na.net, str8_lit("virtio"), 0))
						{
							str8_list_push(scratch.arena, &opts, str8_lit("trans=virtio"));
						}
						else
						{
							str8_list_push(scratch.arena, &opts, str8_lit("trans=unix"));
						}
					}
					else if(str8_match(na.net, str8_lit("tcp"), 0))
					{
						addr = resolvehost(scratch.arena, na.host);
						if(addr.size == 0)
						{
							addr_ok = 0;
						}
						else if(na.port == 0)
						{
							log_error(str8_lit("9mount: port resolution failed\n"));
							addr_ok = 0;
						}
						else
						{
							str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "trans=tcp,port=%llu", na.port));
						}
					}
					else
					{
						log_errorf("9mount: unsupported network type %S\n", na.net);
						addr_ok = 0;
					}
				}
				if(addr_ok)
				{
					String8 user = str8_cstring(pw->pw_name);
					str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "uname=%S", user));
					if(aname.size > 0)
					{
						str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "aname=%S", aname));
					}
					if(msizestr.size > 0)
					{
						u64 msize = u64_from_str8(msizestr, 10);
						if(msize == 0)
						{
							log_errorf("9mount: invalid msize %llu\n", msize);
						}
						else
						{
							str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "msize=%llu", msize));
						}
					}
					if(msizestr.size == 0 || u64_from_str8(msizestr, 10) != 0)
					{
						str8_list_push(scratch.arena, &opts, str8_lit("noextend"));
						str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "dfltuid=%d,dfltgid=%d", uid, gid));
						if(singleattach)
						{
							str8_list_push(scratch.arena, &opts, str8_lit("access=any"));
						}
						else if(exclusive)
						{
							str8_list_push(scratch.arena, &opts, str8f(scratch.arena, "access=%d", uid));
						}
						StringJoin join = {0};
						join.pre = str8_zero();
						join.sep = str8_lit(",");
						join.post = str8_zero();
						String8 optstr = str8_list_join(scratch.arena, &opts, &join);
						String8 addr_copy = str8_copy(scratch.arena, addr);
						if(dryrun)
						{
							log_infof("mount -t 9p -o %S %S %S\n", optstr, addr, mtpt);
						}
						else if(mount((char *)addr_copy.str, (char *)mtpt_copy.str, "9p", 0, (char *)optstr.str))
						{
							log_errorf("9mount: mount failed: %s\n", strerror(errno));
						}
					}
				}
			}
		}
	}

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}
	if(result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}
	scratch_end(scratch);
}
