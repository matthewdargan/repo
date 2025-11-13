#include <mntent.h>
#include <pwd.h>
#include <sys/mount.h>

// clang-format off
#include "base/inc.h"
#include "base/inc.c"
// clang-format on

static b32
is_mounted_by_user(Arena *arena, String8 mount_options, String8 username)
{
	String8List options = str8_split(arena, mount_options, (u8 *)",", 1, 0);
	for(String8Node *node = options.first; node != 0; node = node->next)
	{
		String8 option = node->string;
		if(str8_find_needle(option, 0, str8_lit("uname="), 0) == 0)
		{
			String8 option_username = str8_skip(option, 6);
			return str8_match(option_username, username, 0);
		}
	}
	return 0;
}

static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	if(cmd_line->inputs.node_count == 0)
	{
		log_error(str8_lit("usage: 9umount <mtpt>...\n"));
	}
	else
	{
		uid_t user_id = getuid();
		struct passwd *user_info = getpwuid(user_id);
		if(user_info == 0)
		{
			log_errorf("9umount: unknown uid %d\n", user_id);
		}
		else
		{
			for(String8Node *node = cmd_line->inputs.first; node != 0; node = node->next)
			{
				String8 mount_point = node->string;
				String8 path = os_full_path_from_path(scratch.arena, mount_point);
				if(path.size == 0)
				{
					log_errorf("9umount: %S: %s\n", mount_point, strerror(errno));
					continue;
				}
				FILE *mounts_file = setmntent("/proc/mounts", "r");
				if(mounts_file == 0)
				{
					log_errorf("9umount: could not open /proc/mounts: %s\n", strerror(errno));
					break;
				}
				b32 found_mount = 0;
				for(;;)
				{
					struct mntent *mount_entry = getmntent(mounts_file);
					if(mount_entry == 0)
					{
						break;
					}
					String8 mount_directory = str8_cstring(mount_entry->mnt_dir);
					if(str8_match(path, mount_directory, 0))
					{
						found_mount = 1;
						String8 mount_type = str8_cstring(mount_entry->mnt_type);
						String8 mount_options = str8_cstring(mount_entry->mnt_opts);
						String8 home_directory = str8_cstring(user_info->pw_dir);
						String8 username = str8_cstring(user_info->pw_name);
						b32 in_home_directory = (str8_find_needle(mount_directory, 0, home_directory, 0) == 0);
						if(!in_home_directory && !str8_match(mount_type, str8_lit("9p"), 0))
						{
							log_errorf("9umount: %S: refusing to unmount non-9p fs\n", path);
						}
						else if(!in_home_directory && !is_mounted_by_user(scratch.arena, mount_options, username))
						{
							log_errorf("9umount: %S: not mounted by you\n", path);
						}
						else if(umount(mount_entry->mnt_dir))
						{
							log_errorf("9umount: umount %S: %s\n", mount_directory, strerror(errno));
						}
						break;
					}
				}
				endmntent(mounts_file);
				if(!found_mount)
				{
					log_errorf("9umount: %S not found in /proc/mounts\n", path);
				}
			}
		}
	}

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}
	scratch_end(scratch);
}
