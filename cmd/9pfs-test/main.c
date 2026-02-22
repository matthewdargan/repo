#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"

////////////////////////////////
//~ Helper Functions

internal b32
test_create_file(Arena *arena, Client9P *client, String8 name)
{
  ClientFid9P *fid = client9p_create(arena, client, name, P9_OpenFlag_ReadWrite, 0666);
  if(fid == 0) { return 0; }
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
  if(fid == 0) { return 0; }

  s64 written = client9p_fid_pwrite(arena, fid, data.str, data.size, 0);
  if(written != (s64)data.size)
  {
    client9p_fid_close(arena, fid);
    return 0;
  }

  u8 *read_buf   = push_array(arena, u8, data.size);
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
  if(dir.name.size == 0) { return 0; }
  return dir.length == expected_size;
}

internal b32
test_wstat_chmod(Arena *arena, Client9P *client, String8 name, u32 mode)
{
  ClientFid9P *fid = client9p_fid_walk(arena, client->root, name);
  if(fid == 0) { return 0; }

  Dir9P dir = client9p_fid_stat(arena, fid);
  if(dir.name.size == 0)
  {
    client9p_fid_close(arena, fid);
    return 0;
  }

  dir.mode   = mode;
  b32 result = client9p_fid_wstat(arena, fid, dir);
  if(!result)
  {
    client9p_fid_close(arena, fid);
    return 0;
  }

  Dir9P new_dir = client9p_fid_stat(arena, fid);
  client9p_fid_close(arena, fid);
  return (new_dir.mode & 0777) == (mode & 0777);
}

internal b32
test_wstat_truncate(Arena *arena, Client9P *client, String8 name, u64 new_size)
{
  ClientFid9P *fid = client9p_fid_walk(arena, client->root, name);
  if(fid == 0) { return 0; }

  Dir9P dir = client9p_fid_stat(arena, fid);
  if(dir.name.size == 0)
  {
    client9p_fid_close(arena, fid);
    return 0;
  }

  dir.length = new_size;
  b32 result = client9p_fid_wstat(arena, fid, dir);
  if(!result)
  {
    client9p_fid_close(arena, fid);
    return 0;
  }

  Dir9P new_dir = client9p_fid_stat(arena, fid);
  client9p_fid_close(arena, fid);
  return new_dir.length == new_size;
}

internal b32
test_wstat_rename(Arena *arena, Client9P *client, String8 old_name, String8 new_name)
{
  ClientFid9P *fid = client9p_fid_walk(arena, client->root, old_name);
  if(fid == 0) { return 0; }

  Dir9P dir = client9p_fid_stat(arena, fid);
  if(dir.name.size == 0) { client9p_fid_close(arena, fid); return 0; }

  dir.name   = new_name;
  b32 result = client9p_fid_wstat(arena, fid, dir);
  client9p_fid_close(arena, fid);
  if(!result) { return 0; }

  ClientFid9P *new_fid = client9p_fid_walk(arena, client->root, new_name);
  b32 exists           = new_fid != 0;
  if(exists) { client9p_fid_close(arena, new_fid); }
  return exists;
}

internal b32
test_create_directory(Arena *arena, Client9P *client, String8 name)
{
  ClientFid9P *fid = client9p_create(arena, client, name, P9_OpenFlag_Read, P9_ModeFlag_Directory | 0755);
  if(fid == 0) { return 0; }

  client9p_fid_close(arena, fid);

  Dir9P dir = client9p_stat(arena, client, name);
  return dir.name.size > 0 && (dir.mode & P9_ModeFlag_Directory) != 0;
}

internal b32
test_readdir(Arena *arena, Client9P *client, String8 dir_name)
{
  ClientFid9P *fid = client9p_open(arena, client, dir_name, P9_OpenFlag_Read);
  if(fid == 0) { return 0; }

  DirList9P list = client9p_fid_read_dirs(arena, fid);
  b32 result     = list.count >= 0;

  client9p_fid_close(arena, fid);
  return result;
}

internal b32
test_walk(Arena *arena, Client9P *client, String8 path)
{
  ClientFid9P *root = client->root;
  ClientFid9P *fid  = client9p_fid_walk(arena, root, path);
  if(fid == 0) { return 0; }

  b32 result = fid->qid.path != 0;

  client9p_fid_close(arena, fid);
  return result;
}

internal b32 test_remove(Arena *arena, Client9P *client, String8 name) { return client9p_remove(arena, client, name) != 0; }

////////////////////////////////
//~ Test Functions

internal b32
test_version(Arena *arena, Client9P *client)
{
  (void)arena;
  return client != 0 && client->max_message_size > 0;
}

internal b32
test_stat_root(Arena *arena, Client9P *client)
{
  Dir9P dir = client9p_stat(arena, client, str8_zero());
  return dir.name.size > 0 || dir.qid.type == QidTypeFlag_Directory;
}

internal b32 test_basic_file(Arena *arena, Client9P *client) { return test_create_file(arena, client, str8_lit("test_file")); }

internal b32
test_basic_write_read(Arena *arena, Client9P *client)
{
  String8 test_data = str8_lit("hello, 9pfs!");
  return test_write_read(arena, client, str8_lit("test_file"), test_data);
}

internal b32
test_basic_stat(Arena *arena, Client9P *client)
{
  String8 test_data = str8_lit("hello, 9pfs!");
  return test_stat_file(arena, client, str8_lit("test_file"), test_data.size);
}

internal b32 test_chmod_test_file(Arena *arena, Client9P *client) { return test_wstat_chmod(arena, client, str8_lit("test_file"), 0644); }
internal b32 test_truncate_test_file(Arena *arena, Client9P *client) { return test_wstat_truncate(arena, client, str8_lit("test_file"), 5); }

internal b32 test_rename_test_file(Arena *arena, Client9P *client)
{
  return test_wstat_rename(arena, client, str8_lit("test_file"), str8_lit("test_file_renamed"));
}

internal b32 test_basic_directory(Arena *arena, Client9P *client) { return test_create_directory(arena, client, str8_lit("test_dir")); }
internal b32 test_basic_readdir(Arena *arena, Client9P *client) { return test_readdir(arena, client, str8_lit("test_dir")); }
internal b32 test_basic_walk(Arena *arena, Client9P *client) { return test_walk(arena, client, str8_lit("test_dir")); }
internal b32 test_nested_file(Arena *arena, Client9P *client) { return test_create_file(arena, client, str8_lit("test_dir/nested_file")); }
internal b32 test_remove_renamed_file(Arena *arena, Client9P *client) { return test_remove(arena, client, str8_lit("test_file_renamed")); }
internal b32 test_remove_nested_file(Arena *arena, Client9P *client) { return test_remove(arena, client, str8_lit("test_dir/nested_file")); }
internal b32 test_remove_test_dir(Arena *arena, Client9P *client) { return test_remove(arena, client, str8_lit("test_dir")); }

internal b32
test_empty_file(Arena *arena, Client9P *client)
{
  if(!test_create_file(arena, client, str8_lit("empty_file"))) { return 0; }
  Dir9P dir = client9p_stat(arena, client, str8_lit("empty_file"));
  return dir.length == 0;
}

internal b32
test_large_file(Arena *arena, Client9P *client)
{
  Temp scratch   = scratch_begin(&arena, 1);
  u64 large_size = MB(100);
  u8 *large_data = push_array(scratch.arena, u8, large_size);
  for(u64 i = 0; i < large_size; i += 1) { large_data[i] = (u8)(i & 0xff); }

  String8 large_str = str8(large_data, large_size);

  b32 result = test_write_read(arena, client, str8_lit("large_file"), large_str);
  scratch_end(scratch);
  return result;
}

internal b32
test_partial_read(Arena *arena, Client9P *client)
{
  String8 test_data = str8_lit("partial read test data");
  if(!test_write_read(arena, client, str8_lit("partial_file"), test_data)) { return 0; }

  ClientFid9P *fid = client9p_open(arena, client, str8_lit("partial_file"), P9_OpenFlag_Read);
  if(fid == 0) { return 0; }

  u8 *buf    = push_array(arena, u8, 7);
  s64 n      = client9p_fid_pread(arena, fid, buf, 7, 0);
  b32 result = (n == 7 && MemoryMatch(buf, test_data.str, 7));

  client9p_fid_close(arena, fid);
  return result;
}

internal b32
test_seek_read(Arena *arena, Client9P *client)
{
  String8 test_data = str8_lit("seek test data");
  if(!test_write_read(arena, client, str8_lit("seek_file"), test_data)) { return 0; }

  ClientFid9P *fid = client9p_open(arena, client, str8_lit("seek_file"), P9_OpenFlag_Read);
  if(fid == 0) { return 0; }

  u8 *buf    = push_array(arena, u8, 4);
  s64 n      = client9p_fid_pread(arena, fid, buf, 4, 5);
  b32 result = (n == 4 && MemoryMatch(buf, test_data.str + 5, 4));

  client9p_fid_close(arena, fid);
  return result;
}

internal b32
test_partial_write(Arena *arena, Client9P *client)
{
  ClientFid9P *fid = test_open_or_create(arena, client, str8_lit("partial_write"), P9_OpenFlag_Write | P9_OpenFlag_Truncate, 0666);
  if(fid == 0) { return 0; }

  String8 data1 = str8_lit("first");
  String8 data2 = str8_lit("second");
  s64 n1        = client9p_fid_pwrite(arena, fid, data1.str, data1.size, 0);
  s64 n2        = client9p_fid_pwrite(arena, fid, data2.str, data2.size, data1.size);

  client9p_fid_close(arena, fid);
  if(n1 != (s64)data1.size || n2 != (s64)data2.size) { return 0; }

  fid = client9p_open(arena, client, str8_lit("partial_write"), P9_OpenFlag_Read);
  if(fid == 0) { return 0; }

  u8 *buf   = push_array(arena, u8, data1.size + data2.size);
  s64 total = client9p_fid_pread(arena, fid, buf, data1.size + data2.size, 0);

  client9p_fid_close(arena, fid);
  if(total != (s64)(data1.size + data2.size)) { return 0; }

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
  Temp scratch      = scratch_begin(&arena, 1);
  u8 *long_name_buf = push_array(scratch.arena, u8, 256);
  for(u64 i = 0; i < 255; i += 1) { long_name_buf[i] = 'a' + (u8)(i % 26); }
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
  result     = result && test_create_file(arena, client, str8_lit("file-with-dashes"));
  result     = result && test_create_file(arena, client, str8_lit("file_with_underscores"));
  result     = result && test_create_file(arena, client, str8_lit("file.with.dots"));
  return result;
}

internal b32
test_deep_nesting(Arena *arena, Client9P *client)
{
  if(!test_create_directory(arena, client, str8_lit("deep1")))               { return 0; }
  if(!test_create_directory(arena, client, str8_lit("deep1/deep2")))         { return 0; }
  if(!test_create_directory(arena, client, str8_lit("deep1/deep2/deep3")))   { return 0; }
  if(!test_create_file(arena, client, str8_lit("deep1/deep2/deep3/nested"))) { return 0; }

  Dir9P dir = client9p_stat(arena, client, str8_lit("deep1/deep2/deep3/nested"));
  return dir.name.size > 0;
}

internal b32
test_many_files(Arena *arena, Client9P *client)
{
  if(!test_create_directory(arena, client, str8_lit("many_dir"))) { return 0; }

  u32 created = 0;
  for(u32 i = 0; i < 100; i += 1)
  {
    Temp scratch = scratch_begin(&arena, 1);
    String8 name = str8f(scratch.arena, "many_dir/file_%u", i);
    if(test_create_file(arena, client, name)) { created += 1; }
    scratch_end(scratch);
  }

  return created == 100;
}

internal b32
test_readdir_many(Arena *arena, Client9P *client)
{
  ClientFid9P *fid = client9p_open(arena, client, str8_lit("many_dir"), P9_OpenFlag_Read);
  if(fid == 0) { return 0; }

  DirList9P list = client9p_fid_read_dirs(arena, fid);
  b32 result     = list.count >= 100;

  client9p_fid_close(arena, fid);
  return result;
}

internal b32
test_readdir_long_names(Arena *arena, Client9P *client)
{
  if(!test_create_directory(arena, client, str8_lit("long_names_dir"))) { return 0; }

  u64 n       = 50;
  u64 created = 0;
  for(u64 i = 0; i < n; i += 1)
  {
    Temp scratch   = scratch_begin(&arena, 1);
    u8 *buf        = push_array(scratch.arena, u8, 220);
    for(u64 j = 0; j < 200; j += 1) { buf[j] = 'a' + (u8)(j % 26); }
    buf[200]       = '_';
    String8 suffix = str8_from_u64(scratch.arena, i, 10, 0, 0);
    MemoryCopy(buf + 201, suffix.str, suffix.size);
    String8 name   = str8(buf, 201 + suffix.size);

    String8 path = str8f(scratch.arena, "long_names_dir/%S", name);
    if(test_create_file(arena, client, path)) { created += 1; }
    scratch_end(scratch);
  }

  if(created != n) { return 0; }

  ClientFid9P *fid = client9p_open(arena, client, str8_lit("long_names_dir"), P9_OpenFlag_Read);
  if(fid == 0) { return 0; }

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
  ClientFid9P *fid  = client9p_fid_walk(arena, root, str8_lit("nonexistent/path"));
  return fid == 0;
}

internal b32
test_walk_multiple(Arena *arena, Client9P *client)
{
  if(!test_create_directory(arena, client, str8_lit("walk_test"))) { return 0; }
  if(!test_create_file(arena, client, str8_lit("walk_test/file1"))) { return 0; }
  if(!test_create_file(arena, client, str8_lit("walk_test/file2"))) { return 0; }

  ClientFid9P *root = client->root;
  ClientFid9P *fid  = client9p_fid_walk(arena, root, str8_lit("walk_test/file1"));
  b32 result        = (fid != 0);
  client9p_fid_close(arena, fid);
  return result;
}

internal b32
test_permissions(Arena *arena, Client9P *client)
{
  if(!test_create_file(arena, client, str8_lit("perm_test"))) { return 0; }

  b32 result = 1;
  result     = result && test_wstat_chmod(arena, client, str8_lit("perm_test"), 0755);
  result     = result && test_wstat_chmod(arena, client, str8_lit("perm_test"), 0644);
  result     = result && test_wstat_chmod(arena, client, str8_lit("perm_test"), 0600);
  return result;
}

internal b32
test_qid_consistency(Arena *arena, Client9P *client)
{
  Dir9P dir1 = client9p_stat(arena, client, str8_lit("partial_write"));
  if(dir1.name.size == 0) { return 0; }

  Dir9P dir2 = client9p_stat(arena, client, str8_lit("partial_write"));
  if(dir2.name.size == 0) { return 0; }

  return dir1.qid.path == dir2.qid.path && dir1.qid.type == dir2.qid.type;
}

internal b32
test_truncate_grow(Arena *arena, Client9P *client)
{
  if(!test_wstat_truncate(arena, client, str8_lit("partial_write"), 0))    { return 0; }

  String8 new_data = str8_lit("new longer content");
  if(!test_write_read(arena, client, str8_lit("partial_write"), new_data)) { return 0; }

  return test_stat_file(arena, client, str8_lit("partial_write"), new_data.size);
}

internal b32 test_remove_nonexistent(Arena *arena, Client9P *client) { return client9p_remove(arena, client, str8_lit("nonexistent_remove_xyz")) == 0; }

internal b32
test_create_existing(Arena *arena, Client9P *client)
{
  if(!test_create_file(arena, client, str8_lit("existing_file"))) { return 0; }

  ClientFid9P *fid = client9p_create(arena, client, str8_lit("existing_file"), P9_OpenFlag_ReadWrite, 0666);
  return fid == 0;
}

////////////////////////////////
//~ Test Runner

typedef struct TestCase TestCase;
struct TestCase
{
  String8 name;
  b32 (*func)(Arena *, Client9P *);
};

internal void
run_tests(Arena *arena, String8 address)
{
  OS_Handle socket = dial9p_connect(arena, address, str8_lit("tcp"), str8_lit("9pfs"));
  if(os_handle_match(socket, os_handle_zero()))
  {
    log_errorf("test: failed to connect to '%S'\n", address);
    return;
  }

  u64 fd           = socket.u64[0];
  Client9P *client = client9p_mount(arena, fd, str8_zero(), str8_zero(), str8_zero(), 0);
  if(client == 0)
  {
    os_file_close(socket);
    log_error(str8_lit("test: mount failed\n"));
    return;
  }

  TestCase tests[] = {
    {str8_lit("version"),            test_version},
    {str8_lit("stat_root"),          test_stat_root},
    {str8_lit("create_file"),        test_basic_file},
    {str8_lit("write_read"),         test_basic_write_read},
    {str8_lit("stat_file"),          test_basic_stat},
    {str8_lit("wstat_chmod"),        test_chmod_test_file},
    {str8_lit("wstat_truncate"),     test_truncate_test_file},
    {str8_lit("wstat_rename"),       test_rename_test_file},
    {str8_lit("create_directory"),   test_basic_directory},
    {str8_lit("readdir"),            test_basic_readdir},
    {str8_lit("walk"),               test_basic_walk},
    {str8_lit("create_nested_file"), test_nested_file},
    {str8_lit("remove_file"),        test_remove_renamed_file},
    {str8_lit("remove_nested_file"), test_remove_nested_file},
    {str8_lit("remove_directory"),   test_remove_test_dir},
    {str8_lit("empty_file"),         test_empty_file},
    {str8_lit("large_file"),         test_large_file},
    {str8_lit("partial_read"),       test_partial_read},
    {str8_lit("seek_read"),          test_seek_read},
    {str8_lit("partial_write"),      test_partial_write},
    {str8_lit("stat_nonexistent"),   test_stat_nonexistent},
    {str8_lit("open_nonexistent"),   test_open_nonexistent},
    {str8_lit("path_traversal"),     test_path_traversal},
    {str8_lit("long_filename"),      test_long_filename},
    {str8_lit("special_chars"),      test_special_chars},
    {str8_lit("deep_nesting"),       test_deep_nesting},
    {str8_lit("many_files"),         test_many_files},
    {str8_lit("readdir_many"),       test_readdir_many},
    {str8_lit("readdir_long_names"), test_readdir_long_names},
    {str8_lit("multiple_fids"),      test_multiple_fids},
    {str8_lit("walk_partial"),       test_walk_partial},
    {str8_lit("walk_multiple"),      test_walk_multiple},
    {str8_lit("permissions"),        test_permissions},
    {str8_lit("qid_consistency"),    test_qid_consistency},
    {str8_lit("truncate_grow"),      test_truncate_grow},
    {str8_lit("remove_nonexistent"), test_remove_nonexistent},
    {str8_lit("create_existing"),    test_create_existing},
  };

  u64 test_count = ArrayCount(tests);
  u64 passed     = 0;
  u64 failed     = 0;

  for(u64 i = 0; i < test_count; i += 1)
  {
    Temp scratch = scratch_begin(&arena, 1);
    b32 result = tests[i].func(scratch.arena, client);
    scratch_end(scratch);

    if(result)
    {
      log_infof("PASS: %S\n", tests[i].name);
      passed += 1;
    }
    else
    {
      log_errorf("FAIL: %S\n", tests[i].name);
      failed += 1;
    }
  }

  client9p_unmount(arena, client);
  os_file_close(socket);

  log_infof("test: %llu passed, %llu failed\n", passed, failed);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  Temp scratch = scratch_begin(0, 0);
  Log *log     = log_alloc();
  log_select(log);
  log_scope_begin();

  String8 address = (cmd_line->inputs.node_count > 0) ? cmd_line->inputs.first->string : str8_zero();

  if(address.size == 0)
  {
    log_error(str8_lit("usage: 9pfs-test <address>\n"
                       "  <address>  Dial string (e.g., tcp!localhost!5640)\n"));
  }
  else { run_tests(scratch.arena, address); }

  log_scope_flush(scratch.arena);
  scratch_end(scratch);
}
