// clang-format off
#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
// clang-format on

////////////////////////////////
//~ Test Functions

internal b32
test_version(Client9P *client)
{
	return client != 0 && client->max_message_size > 0;
}

internal b32
test_stat_root(Arena *arena, Client9P *client)
{
	Dir9P dir = client9p_stat(arena, client, str8_zero());
	return dir.name.size > 0 || dir.qid.type == QidTypeFlag_Directory;
}

internal b32
test_create_file(Arena *arena, Client9P *client, String8 name)
{
	ClientFid9P *fid = client9p_create(arena, client, name, P9_OpenFlag_ReadWrite, 0666);
	if(fid == 0)
	{
		return 0;
	}
	client9p_fid_close(arena, fid);
	return 1;
}

internal ClientFid9P *
test_open_or_create(Arena *arena, Client9P *client, String8 name, u32 mode, u32 perm)
{
	ClientFid9P *fid = client9p_open(arena, client, name, mode);
	if(fid == 0)
	{
		fid = client9p_create(arena, client, name, mode, perm);
		if(fid != 0)
		{
			client9p_fid_close(arena, fid);
			fid = client9p_open(arena, client, name, mode);
		}
	}
	return fid;
}

internal b32
test_write_read(Arena *arena, Client9P *client, String8 name, String8 data)
{
	ClientFid9P *fid = test_open_or_create(arena, client, name, P9_OpenFlag_ReadWrite | P9_OpenFlag_Truncate, 0666);
	if(fid == 0)
	{
		return 0;
	}

	s64 written = client9p_fid_pwrite(arena, fid, data.str, data.size, 0);
	if(written != (s64)data.size)
	{
		client9p_fid_close(arena, fid);
		return 0;
	}

	u8 *read_buf = push_array(arena, u8, data.size);
	s64 read_count = client9p_fid_pread(arena, fid, read_buf, data.size, 0);
	if(read_count != (s64)data.size)
	{
		client9p_fid_close(arena, fid);
		return 0;
	}

	b32 match = MemoryMatch(data.str, read_buf, data.size);
	client9p_fid_close(arena, fid);
	return match;
}

internal b32
test_stat_file(Arena *arena, Client9P *client, String8 name, u64 expected_size)
{
	Dir9P dir = client9p_stat(arena, client, name);
	if(dir.name.size == 0)
	{
		return 0;
	}
	return dir.length == expected_size;
}

internal b32
test_wstat_chmod(Arena *arena, Client9P *client, String8 name, u32 mode)
{
	Dir9P dir = client9p_stat(arena, client, name);
	if(dir.name.size == 0)
	{
		return 0;
	}
	dir.mode = mode;
	b32 result = client9p_wstat(arena, client, name, dir);
	if(!result)
	{
		return 0;
	}
	Dir9P new_dir = client9p_stat(arena, client, name);
	return (new_dir.mode & 0777) == (mode & 0777);
}

internal b32
test_wstat_truncate(Arena *arena, Client9P *client, String8 name, u64 new_size)
{
	Dir9P dir = client9p_stat(arena, client, name);
	if(dir.name.size == 0)
	{
		return 0;
	}
	dir.length = new_size;
	b32 result = client9p_wstat(arena, client, name, dir);
	if(!result)
	{
		return 0;
	}
	Dir9P new_dir = client9p_stat(arena, client, name);
	return new_dir.length == new_size;
}

internal b32
test_wstat_rename(Arena *arena, Client9P *client, String8 old_name, String8 new_name)
{
	Dir9P dir = client9p_stat(arena, client, old_name);
	if(dir.name.size == 0)
	{
		return 0;
	}
	dir.name = new_name;
	b32 result = client9p_wstat(arena, client, old_name, dir);
	if(!result)
	{
		return 0;
	}
	Dir9P new_dir = client9p_stat(arena, client, new_name);
	return new_dir.name.size > 0;
}

internal b32
test_create_directory(Arena *arena, Client9P *client, String8 name)
{
	ClientFid9P *fid = client9p_create(arena, client, name, P9_OpenFlag_Read, P9_ModeFlag_Directory | 0755);
	if(fid == 0)
	{
		return 0;
	}
	client9p_fid_close(arena, fid);
	Dir9P dir = client9p_stat(arena, client, name);
	return dir.name.size > 0 && (dir.mode & P9_ModeFlag_Directory) != 0;
}

internal b32
test_readdir(Arena *arena, Client9P *client, String8 dir_name)
{
	ClientFid9P *fid = client9p_open(arena, client, dir_name, P9_OpenFlag_Read);
	if(fid == 0)
	{
		return 0;
	}
	DirList9P list = client9p_fid_read_dirs(arena, fid);
	b32 result = list.count >= 0;
	client9p_fid_close(arena, fid);
	return result;
}

internal b32
test_walk(Arena *arena, Client9P *client, String8 path)
{
	ClientFid9P *root = client->root;
	ClientFid9P *fid = client9p_fid_walk(arena, root, path);
	if(fid == 0)
	{
		return 0;
	}
	b32 result = fid->qid.path != 0;
	client9p_fid_close(arena, fid);
	return result;
}

internal b32
test_remove(Arena *arena, Client9P *client, String8 name)
{
	return client9p_remove(arena, client, name) != 0;
}

internal b32
test_empty_file(Arena *arena, Client9P *client)
{
	if(!test_create_file(arena, client, str8_lit("empty_file")))
	{
		return 0;
	}
	Dir9P dir = client9p_stat(arena, client, str8_lit("empty_file"));
	return dir.length == 0;
}

internal b32
test_large_file(Arena *arena, Client9P *client)
{
	Temp scratch = scratch_begin(&arena, 1);
	u64 large_size = MB(100);
	u8 *large_data = push_array(scratch.arena, u8, large_size);
	for(u64 i = 0; i < large_size; i += 1)
	{
		large_data[i] = (u8)(i & 0xff);
	}
	String8 large_str = str8(large_data, large_size);

	b32 result = test_write_read(arena, client, str8_lit("large_file"), large_str);
	scratch_end(scratch);
	return result;
}

internal b32
test_partial_read(Arena *arena, Client9P *client)
{
	String8 test_data = str8_lit("partial read test data");
	if(!test_write_read(arena, client, str8_lit("partial_file"), test_data))
	{
		return 0;
	}

	ClientFid9P *fid = client9p_open(arena, client, str8_lit("partial_file"), P9_OpenFlag_Read);
	if(fid == 0)
	{
		return 0;
	}

	u8 *buf = push_array(arena, u8, 7);
	s64 n = client9p_fid_pread(arena, fid, buf, 7, 0);
	b32 result = (n == 7 && MemoryMatch(buf, test_data.str, 7));

	client9p_fid_close(arena, fid);
	return result;
}

internal b32
test_seek_read(Arena *arena, Client9P *client)
{
	String8 test_data = str8_lit("seek test data");
	if(!test_write_read(arena, client, str8_lit("seek_file"), test_data))
	{
		return 0;
	}

	ClientFid9P *fid = client9p_open(arena, client, str8_lit("seek_file"), P9_OpenFlag_Read);
	if(fid == 0)
	{
		return 0;
	}

	u8 *buf = push_array(arena, u8, 4);
	s64 n = client9p_fid_pread(arena, fid, buf, 4, 5);
	b32 result = (n == 4 && MemoryMatch(buf, test_data.str + 5, 4));

	client9p_fid_close(arena, fid);
	return result;
}

internal b32
test_partial_write(Arena *arena, Client9P *client)
{
	ClientFid9P *fid =
	    test_open_or_create(arena, client, str8_lit("partial_write"), P9_OpenFlag_Write | P9_OpenFlag_Truncate, 0666);
	if(fid == 0)
	{
		return 0;
	}

	String8 data1 = str8_lit("first");
	String8 data2 = str8_lit("second");

	s64 n1 = client9p_fid_pwrite(arena, fid, data1.str, data1.size, 0);
	s64 n2 = client9p_fid_pwrite(arena, fid, data2.str, data2.size, data1.size);

	client9p_fid_close(arena, fid);

	if(n1 != (s64)data1.size || n2 != (s64)data2.size)
	{
		return 0;
	}

	fid = client9p_open(arena, client, str8_lit("partial_write"), P9_OpenFlag_Read);
	if(fid == 0)
	{
		return 0;
	}

	u8 *buf = push_array(arena, u8, data1.size + data2.size);
	s64 total = client9p_fid_pread(arena, fid, buf, data1.size + data2.size, 0);

	client9p_fid_close(arena, fid);

	if(total != (s64)(data1.size + data2.size))
	{
		return 0;
	}

	b32 match1 = MemoryMatch(buf, data1.str, data1.size);
	b32 match2 = MemoryMatch(buf + data1.size, data2.str, data2.size);
	return match1 && match2;
}

internal b32
test_stat_nonexistent(Arena *arena, Client9P *client)
{
	Dir9P dir = client9p_stat(arena, client, str8_lit("nonexistent_file_xyz"));
	return dir.name.size == 0;
}

internal b32
test_open_nonexistent(Arena *arena, Client9P *client)
{
	ClientFid9P *fid = client9p_open(arena, client, str8_lit("nonexistent_file_xyz"), P9_OpenFlag_Read);
	return fid == 0;
}

internal b32
test_path_traversal(Arena *arena, Client9P *client)
{
	ClientFid9P *fid1 = client9p_open(arena, client, str8_lit("../"), P9_OpenFlag_Read);
	ClientFid9P *fid2 = client9p_open(arena, client, str8_lit(".."), P9_OpenFlag_Read);
	ClientFid9P *fid3 = client9p_open(arena, client, str8_lit("/etc/passwd"), P9_OpenFlag_Read);
	return fid1 == 0 && fid2 == 0 && fid3 == 0;
}

internal b32
test_long_filename(Arena *arena, Client9P *client)
{
	Temp scratch = scratch_begin(&arena, 1);
	u8 *long_name_buf = push_array(scratch.arena, u8, 256);
	for(u64 i = 0; i < 255; i += 1)
	{
		long_name_buf[i] = 'a' + (u8)(i % 26);
	}
	long_name_buf[255] = 0;
	String8 long_name = str8(long_name_buf, 255);

	b32 result = test_create_file(arena, client, long_name);
	scratch_end(scratch);
	return result;
}

internal b32
test_special_chars(Arena *arena, Client9P *client)
{
	b32 result = 1;
	result = result && test_create_file(arena, client, str8_lit("file-with-dashes"));
	result = result && test_create_file(arena, client, str8_lit("file_with_underscores"));
	result = result && test_create_file(arena, client, str8_lit("file.with.dots"));
	return result;
}

internal b32
test_deep_nesting(Arena *arena, Client9P *client)
{
	if(!test_create_directory(arena, client, str8_lit("deep1")))
	{
		return 0;
	}
	if(!test_create_directory(arena, client, str8_lit("deep1/deep2")))
	{
		return 0;
	}
	if(!test_create_directory(arena, client, str8_lit("deep1/deep2/deep3")))
	{
		return 0;
	}
	if(!test_create_file(arena, client, str8_lit("deep1/deep2/deep3/nested")))
	{
		return 0;
	}
	Dir9P dir = client9p_stat(arena, client, str8_lit("deep1/deep2/deep3/nested"));
	return dir.name.size > 0;
}

internal b32
test_many_files(Arena *arena, Client9P *client)
{
	if(!test_create_directory(arena, client, str8_lit("many_dir")))
	{
		return 0;
	}

	u32 created = 0;
	for(u32 i = 0; i < 100; i += 1)
	{
		Temp scratch = scratch_begin(&arena, 1);
		String8 name = str8f(scratch.arena, "many_dir/file_%u", i);
		if(test_create_file(arena, client, name))
		{
			created += 1;
		}
		scratch_end(scratch);
	}

	return created == 100;
}

internal b32
test_readdir_many(Arena *arena, Client9P *client)
{
	ClientFid9P *fid = client9p_open(arena, client, str8_lit("many_dir"), P9_OpenFlag_Read);
	if(fid == 0)
	{
		return 0;
	}

	DirList9P list = client9p_fid_read_dirs(arena, fid);
	b32 result = list.count >= 100;
	client9p_fid_close(arena, fid);
	return result;
}

internal b32
test_readdir_long_names(Arena *arena, Client9P *client)
{
	if(!test_create_directory(arena, client, str8_lit("long_names_dir")))
	{
		return 0;
	}

	u64 n = 50;
	u64 created = 0;
	for(u64 i = 0; i < n; i += 1)
	{
		Temp scratch = scratch_begin(&arena, 1);
		u8 *buf = push_array(scratch.arena, u8, 220);
		for(u64 j = 0; j < 200; j += 1)
		{
			buf[j] = 'a' + (u8)(j % 26);
		}
		buf[200] = '_';
		String8 suffix = str8f(scratch.arena, "%llu", (unsigned long long)i);
		MemoryCopy(buf + 201, suffix.str, suffix.size);
		String8 name = str8(buf, 201 + suffix.size);

		String8 path = str8f(scratch.arena, "long_names_dir/%S", name);
		if(test_create_file(arena, client, path))
		{
			created += 1;
		}
		scratch_end(scratch);
	}

	if(created != n)
	{
		return 0;
	}

	ClientFid9P *fid = client9p_open(arena, client, str8_lit("long_names_dir"), P9_OpenFlag_Read);
	if(fid == 0)
	{
		return 0;
	}

	DirList9P list = client9p_fid_read_dirs(arena, fid);
	client9p_fid_close(arena, fid);

	return list.count >= n;
}

internal b32
test_multiple_fids(Arena *arena, Client9P *client)
{
	ClientFid9P *fid1 = client9p_open(arena, client, str8_lit("partial_write"), P9_OpenFlag_Read);
	ClientFid9P *fid2 = client9p_open(arena, client, str8_lit("partial_write"), P9_OpenFlag_Read);
	ClientFid9P *fid3 = client9p_open(arena, client, str8_lit("partial_write"), P9_OpenFlag_Read);

	b32 result = (fid1 != 0 && fid2 != 0 && fid3 != 0);

	client9p_fid_close(arena, fid1);
	client9p_fid_close(arena, fid2);
	client9p_fid_close(arena, fid3);

	return result;
}

internal b32
test_walk_partial(Arena *arena, Client9P *client)
{
	ClientFid9P *root = client->root;
	ClientFid9P *fid = client9p_fid_walk(arena, root, str8_lit("nonexistent/path"));
	return fid == 0;
}

internal b32
test_walk_multiple(Arena *arena, Client9P *client)
{
	if(!test_create_directory(arena, client, str8_lit("walk_test")))
	{
		return 0;
	}
	if(!test_create_file(arena, client, str8_lit("walk_test/file1")))
	{
		return 0;
	}
	if(!test_create_file(arena, client, str8_lit("walk_test/file2")))
	{
		return 0;
	}

	ClientFid9P *root = client->root;
	ClientFid9P *fid = client9p_fid_walk(arena, root, str8_lit("walk_test/file1"));
	b32 result = (fid != 0);
	client9p_fid_close(arena, fid);
	return result;
}

internal b32
test_permissions(Arena *arena, Client9P *client)
{
	if(!test_create_file(arena, client, str8_lit("perm_test")))
	{
		return 0;
	}

	b32 result = 1;
	result = result && test_wstat_chmod(arena, client, str8_lit("perm_test"), 0755);
	result = result && test_wstat_chmod(arena, client, str8_lit("perm_test"), 0644);
	result = result && test_wstat_chmod(arena, client, str8_lit("perm_test"), 0600);
	return result;
}

internal b32
test_qid_consistency(Arena *arena, Client9P *client)
{
	Dir9P dir1 = client9p_stat(arena, client, str8_lit("partial_write"));
	if(dir1.name.size == 0)
	{
		return 0;
	}

	Dir9P dir2 = client9p_stat(arena, client, str8_lit("partial_write"));
	if(dir2.name.size == 0)
	{
		return 0;
	}

	return dir1.qid.path == dir2.qid.path && dir1.qid.type == dir2.qid.type;
}

internal b32
test_truncate_grow(Arena *arena, Client9P *client)
{
	if(!test_wstat_truncate(arena, client, str8_lit("partial_write"), 0))
	{
		return 0;
	}

	String8 new_data = str8_lit("new longer content");
	if(!test_write_read(arena, client, str8_lit("partial_write"), new_data))
	{
		return 0;
	}

	return test_stat_file(arena, client, str8_lit("partial_write"), new_data.size);
}

internal b32
test_remove_nonexistent(Arena *arena, Client9P *client)
{
	return client9p_remove(arena, client, str8_lit("nonexistent_remove_xyz")) == 0;
}

internal b32
test_create_existing(Arena *arena, Client9P *client)
{
	if(!test_create_file(arena, client, str8_lit("existing_file")))
	{
		return 0;
	}

	ClientFid9P *fid = client9p_create(arena, client, str8_lit("existing_file"), P9_OpenFlag_ReadWrite, 0666);
	return fid == 0;
}

////////////////////////////////
//~ Test Runner

internal void
run_tests(Arena *arena, String8 address)
{
	OS_Handle socket = dial9p_connect(arena, address, str8_lit("tcp"), str8_lit("9pfs"));
	if(os_handle_match(socket, os_handle_zero()))
	{
		log_errorf("test: failed to connect to '%S'\n", address);
		return;
	}

	u64 fd = socket.u64[0];
	Client9P *client = client9p_mount(arena, fd, str8_zero());
	if(client == 0)
	{
		os_file_close(socket);
		log_error(str8_lit("test: mount failed\n"));
		return;
	}

	u32 passed = 0;
	u32 failed = 0;

	if(test_version(client))
	{
		log_info(str8_lit("PASS: version\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: version\n"));
		failed += 1;
	}

	if(test_stat_root(arena, client))
	{
		log_info(str8_lit("PASS: stat_root\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: stat_root\n"));
		failed += 1;
	}

	if(test_create_file(arena, client, str8_lit("test_file")))
	{
		log_info(str8_lit("PASS: create_file\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: create_file\n"));
		failed += 1;
	}

	String8 test_data = str8_lit("hello, 9pfs!");
	if(test_write_read(arena, client, str8_lit("test_file"), test_data))
	{
		log_info(str8_lit("PASS: write_read\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: write_read\n"));
		failed += 1;
	}

	if(test_stat_file(arena, client, str8_lit("test_file"), test_data.size))
	{
		log_info(str8_lit("PASS: stat_file\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: stat_file\n"));
		failed += 1;
	}

	if(test_wstat_chmod(arena, client, str8_lit("test_file"), 0644))
	{
		log_info(str8_lit("PASS: wstat_chmod\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: wstat_chmod\n"));
		failed += 1;
	}

	if(test_wstat_truncate(arena, client, str8_lit("test_file"), 5))
	{
		log_info(str8_lit("PASS: wstat_truncate\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: wstat_truncate\n"));
		failed += 1;
	}

	if(test_wstat_rename(arena, client, str8_lit("test_file"), str8_lit("test_file_renamed")))
	{
		log_info(str8_lit("PASS: wstat_rename\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: wstat_rename\n"));
		failed += 1;
	}

	if(test_create_directory(arena, client, str8_lit("test_dir")))
	{
		log_info(str8_lit("PASS: create_directory\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: create_directory\n"));
		failed += 1;
	}

	if(test_readdir(arena, client, str8_lit("test_dir")))
	{
		log_info(str8_lit("PASS: readdir\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: readdir\n"));
		failed += 1;
	}

	if(test_walk(arena, client, str8_lit("test_dir")))
	{
		log_info(str8_lit("PASS: walk\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: walk\n"));
		failed += 1;
	}

	if(test_create_file(arena, client, str8_lit("test_dir/nested_file")))
	{
		log_info(str8_lit("PASS: create_nested_file\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: create_nested_file\n"));
		failed += 1;
	}

	if(test_remove(arena, client, str8_lit("test_file_renamed")))
	{
		log_info(str8_lit("PASS: remove_file\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: remove_file\n"));
		failed += 1;
	}

	if(test_remove(arena, client, str8_lit("test_dir/nested_file")))
	{
		log_info(str8_lit("PASS: remove_nested_file\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: remove_nested_file\n"));
		failed += 1;
	}

	if(test_remove(arena, client, str8_lit("test_dir")))
	{
		log_info(str8_lit("PASS: remove_directory\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: remove_directory\n"));
		failed += 1;
	}

	if(test_empty_file(arena, client))
	{
		log_info(str8_lit("PASS: empty_file\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: empty_file\n"));
		failed += 1;
	}

	if(test_large_file(arena, client))
	{
		log_info(str8_lit("PASS: large_file\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: large_file\n"));
		failed += 1;
	}

	if(test_partial_read(arena, client))
	{
		log_info(str8_lit("PASS: partial_read\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: partial_read\n"));
		failed += 1;
	}

	if(test_seek_read(arena, client))
	{
		log_info(str8_lit("PASS: seek_read\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: seek_read\n"));
		failed += 1;
	}

	if(test_partial_write(arena, client))
	{
		log_info(str8_lit("PASS: partial_write\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: partial_write\n"));
		failed += 1;
	}

	if(test_stat_nonexistent(arena, client))
	{
		log_info(str8_lit("PASS: stat_nonexistent\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: stat_nonexistent\n"));
		failed += 1;
	}

	if(test_open_nonexistent(arena, client))
	{
		log_info(str8_lit("PASS: open_nonexistent\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: open_nonexistent\n"));
		failed += 1;
	}

	if(test_path_traversal(arena, client))
	{
		log_info(str8_lit("PASS: path_traversal\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: path_traversal\n"));
		failed += 1;
	}

	if(test_long_filename(arena, client))
	{
		log_info(str8_lit("PASS: long_filename\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: long_filename\n"));
		failed += 1;
	}

	if(test_special_chars(arena, client))
	{
		log_info(str8_lit("PASS: special_chars\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: special_chars\n"));
		failed += 1;
	}

	if(test_deep_nesting(arena, client))
	{
		log_info(str8_lit("PASS: deep_nesting\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: deep_nesting\n"));
		failed += 1;
	}

	if(test_many_files(arena, client))
	{
		log_info(str8_lit("PASS: many_files\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: many_files\n"));
		failed += 1;
	}

	if(test_readdir_many(arena, client))
	{
		log_info(str8_lit("PASS: readdir_many\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: readdir_many\n"));
		failed += 1;
	}

	if(test_readdir_long_names(arena, client))
	{
		log_info(str8_lit("PASS: readdir_long_names\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: readdir_long_names\n"));
		failed += 1;
	}

	if(test_multiple_fids(arena, client))
	{
		log_info(str8_lit("PASS: multiple_fids\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: multiple_fids\n"));
		failed += 1;
	}

	if(test_walk_partial(arena, client))
	{
		log_info(str8_lit("PASS: walk_partial\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: walk_partial\n"));
		failed += 1;
	}

	if(test_walk_multiple(arena, client))
	{
		log_info(str8_lit("PASS: walk_multiple\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: walk_multiple\n"));
		failed += 1;
	}

	if(test_permissions(arena, client))
	{
		log_info(str8_lit("PASS: permissions\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: permissions\n"));
		failed += 1;
	}

	if(test_qid_consistency(arena, client))
	{
		log_info(str8_lit("PASS: qid_consistency\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: qid_consistency\n"));
		failed += 1;
	}

	if(test_truncate_grow(arena, client))
	{
		log_info(str8_lit("PASS: truncate_grow\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: truncate_grow\n"));
		failed += 1;
	}

	if(test_remove_nonexistent(arena, client))
	{
		log_info(str8_lit("PASS: remove_nonexistent\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: remove_nonexistent\n"));
		failed += 1;
	}

	if(test_create_existing(arena, client))
	{
		log_info(str8_lit("PASS: create_existing\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: create_existing\n"));
		failed += 1;
	}

	client9p_unmount(arena, client);
	os_file_close(socket);

	log_infof("test: %u passed, %u failed\n", passed, failed);
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

	String8 address = str8_zero();

	if(cmd_line->inputs.node_count > 0)
	{
		address = cmd_line->inputs.first->string;
	}

	if(address.size == 0)
	{
		log_error(str8_lit("usage: 9pfs-test <address>\n"
		                   "  <address>  Dial string (e.g., tcp!localhost!5640)\n"));
	}
	else
	{
		run_tests(scratch.arena, address);
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
