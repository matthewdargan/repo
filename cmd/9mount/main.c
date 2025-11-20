#include <sys/mount.h>

// clang-format off
#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
// clang-format on

////////////////////////////////
//~ Helper Functions

internal String8
resolve_host(Arena *arena, String8 host)
{
	String8 host_copy = str8_copy(arena, host);
	struct addrinfo *addr_info = 0;
	int getaddr_result = getaddrinfo((char *)host_copy.str, 0, 0, &addr_info);
	if(getaddr_result != 0)
	{
		return str8_zero();
	}
	char ip_buffer[INET6_ADDRSTRLEN] = {0};
	int getname_result =
	    getnameinfo(addr_info->ai_addr, addr_info->ai_addrlen, ip_buffer, sizeof(ip_buffer), 0, 0, NI_NUMERICHOST);
	if(getname_result != 0)
	{
		freeaddrinfo(addr_info);
		return str8_zero();
	}
	String8 result = str8_copy(arena, str8_cstring(ip_buffer));
	freeaddrinfo(addr_info);
	return result;
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();
	b32 dry_run = cmd_line_has_flag(cmd_line, str8_lit("dry-run"));
	String8 attach_name = cmd_line_string(cmd_line, str8_lit("aname"));
	String8 max_message_size_string = cmd_line_string(cmd_line, str8_lit("msize"));
	String8 uid_string = cmd_line_string(cmd_line, str8_lit("uid"));
	String8 gid_string = cmd_line_string(cmd_line, str8_lit("gid"));

	if(cmd_line->inputs.node_count != 2)
	{
		log_error(str8_lit("usage: 9mount [options] <dial> <mtpt>\n"
		                   "options:\n"
		                   "  --dry-run               Print mount command without executing\n"
		                   "  --aname=<path>          Remote path to attach (default: root)\n"
		                   "  --msize=<bytes>         Maximum 9P message size\n"
		                   "  --uid=<uid>             User ID for mount (default: current user)\n"
		                   "  --gid=<gid>             Group ID for mount (default: current group)\n"
		                   "arguments:\n"
		                   "  <dial>                  Dial string (e.g., tcp!host!port)\n"
		                   "  <mtpt>                  Local mount point directory\n"));
	}
	else
	{
		String8 dial = cmd_line->inputs.first->string;
		String8 mount_point = cmd_line->inputs.first->next->string;
		String8 mount_point_copy = str8_copy(scratch.arena, mount_point);
		uid_t uid = uid_string.size > 0 ? (uid_t)u64_from_str8(uid_string, 10) : getuid();
		gid_t gid = gid_string.size > 0 ? (gid_t)u64_from_str8(gid_string, 10) : getgid();

		struct passwd *passwd_entry = getpwuid(uid);
		if(passwd_entry == 0)
		{
			log_errorf("9mount: unknown uid %d\n", uid);
		}
		else
		{
			struct stat stat_result = {0};
			if(stat((char *)mount_point_copy.str, &stat_result) || access((char *)mount_point_copy.str, W_OK))
			{
				log_errorf("9mount: %S: %s\n", mount_point, strerror(errno));
			}
			else if(stat_result.st_mode & S_ISVTX)
			{
				log_errorf("9mount: refusing to mount over sticky directory %S\n", mount_point);
			}
			else
			{
				String8List mount_options = {0};
				String8 device_address = str8_zero();
				b32 address_valid = 1;

				Dial9PAddress address = dial9p_parse(scratch.arena, dial, str8_lit("tcp"), str8_lit("9pfs"));
				if(address.host.size == 0)
				{
					log_errorf("9mount: invalid dial string %S\n", dial);
					address_valid = 0;
				}
				else if(address.protocol == Dial9PProtocol_Unix)
				{
					device_address = address.host;
					str8_list_push(scratch.arena, &mount_options, str8_lit("trans=unix"));
				}
				else if(address.protocol == Dial9PProtocol_TCP)
				{
					device_address = resolve_host(scratch.arena, address.host);
					if(device_address.size == 0)
					{
						address_valid = 0;
					}
					else if(address.port == 0)
					{
						log_error(str8_lit("9mount: port resolution failed\n"));
						address_valid = 0;
					}
					else
					{
						str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "trans=tcp,port=%llu", address.port));
					}
				}
				else
				{
					log_error(str8_lit("9mount: unsupported protocol\n"));
					address_valid = 0;
				}
				if(address_valid)
				{
					String8 user = str8_cstring(passwd_entry->pw_name);
					str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "uname=%S", user));
					if(attach_name.size > 0)
					{
						str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "aname=%S", attach_name));
					}
					if(max_message_size_string.size > 0)
					{
						u64 max_message_size = u64_from_str8(max_message_size_string, 10);
						if(max_message_size == 0)
						{
							log_errorf("9mount: invalid msize %llu\n", max_message_size);
						}
						else
						{
							str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "msize=%llu", max_message_size));
						}
					}

					str8_list_push(scratch.arena, &mount_options, str8_lit("noextend"));
					str8_list_push(scratch.arena, &mount_options, str8f(scratch.arena, "dfltuid=%d,dfltgid=%d", uid, gid));
					str8_list_push(scratch.arena, &mount_options, str8_lit("access=any"));

					StringJoin join = {0};
					join.sep = str8_lit(",");
					String8 option_string = str8_list_join(scratch.arena, &mount_options, &join);
					String8 device_address_copy = str8_copy(scratch.arena, device_address);

					if(dry_run)
					{
						log_infof("mount -t 9p -o %S %S %S\n", option_string, device_address, mount_point);
					}
					else if(mount((char *)device_address_copy.str, (char *)mount_point_copy.str, "9p", 0,
					              (char *)option_string.str))
					{
						log_errorf("9mount: mount failed: %s\n", strerror(errno));
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
