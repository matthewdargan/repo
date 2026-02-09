#include "base/inc.h"
#include "9p/inc.h"
#include "auth/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
#include "auth/inc.c"

////////////////////////////////
//~ Test Context

typedef struct AuthTestContext AuthTestContext;
struct AuthTestContext
{
  Arena *arena;
  String8 auth_addr;
  String8 fs_addr;
  String8 proto;
};

////////////////////////////////
//~ Test Helpers

internal Client9P *
auth_mount(Arena *arena, String8 addr, String8 user)
{
  OS_Handle socket = dial9p_connect(arena, addr, str8_lit("unix"), str8_lit("564"));
  if(os_handle_match(socket, os_handle_zero()))
  {
    return 0;
  }

  u64 fd = socket.u64[0];
  Client9P *client = client9p_init(arena, fd);
  if(client == 0)
  {
    os_file_close(socket);
    return 0;
  }

  ClientFid9P *root = client9p_attach(arena, client, P9_FID_NONE, user, str8_lit("/"));
  if(root == 0)
  {
    os_file_close(socket);
    return 0;
  }
  client->root = root;
  return client;
}

internal Client9P *
fs_mount_auth(Arena *arena, String8 fs_addr, String8 auth_addr, String8 proto, String8 user, String8 server_hostname, String8 aname)
{
  OS_Handle fs_socket = dial9p_connect(arena, fs_addr, str8_lit("unix"), str8_lit("564"));
  if(os_handle_match(fs_socket, os_handle_zero()))
  {
    return 0;
  }

  u64 fs_fd = fs_socket.u64[0];
  Client9P *client = client9p_init(arena, fs_fd);
  if(client == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }

  ClientFid9P *auth_fid = client9p_auth(arena, client, auth_addr, proto, user, aname, server_hostname);
  if(auth_fid == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }

  ClientFid9P *root = client9p_attach(arena, client, auth_fid->fid, user, aname);
  if(root == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }
  client->root = root;
  return client;
}

////////////////////////////////
//~ Test Functions

internal b32
test_register(AuthTestContext *ctx)
{
  String8 user = get_user_name(ctx->arena);
  Client9P *client = auth_mount(ctx->arena, ctx->auth_addr, user);
  if(client == 0)
  {
    return 0;
  }

  ClientFid9P *ctl_fid = client9p_open(ctx->arena, client, str8_lit("ctl"), P9_OpenFlag_Write);
  if(ctl_fid == 0)
  {
    return 0;
  }

  if(str8_match(ctx->proto, str8_lit("fido2"), 0))
  {
    fprintf(stdout, "9auth-test: touch FIDO2 token\n");
    fflush(stdout);
  }

  Temp scratch = scratch_begin(&ctx->arena, 1);
  String8 register_cmd = str8f(scratch.arena, "register user=e2etest server=e2etest.local proto=%S", ctx->proto);
  s64 written = client9p_fid_pwrite(ctx->arena, ctl_fid, (void *)register_cmd.str, register_cmd.size, 0);
  scratch_end(scratch);

  client9p_fid_close(ctx->arena, ctl_fid);
  return written == (s64)register_cmd.size;
}

internal b32
test_authenticated_mount(AuthTestContext *ctx)
{
  String8 user = str8_lit("e2etest");
  String8 server_hostname = str8_lit("e2etest.local");
  String8 aname = str8_lit("/");

  Client9P *client = fs_mount_auth(ctx->arena, ctx->fs_addr, ctx->auth_addr, ctx->proto, user, server_hostname, aname);
  if(client == 0)
  {
    return 0;
  }

  ClientFid9P *dir_fid = client9p_open(ctx->arena, client, str8_lit("."), P9_OpenFlag_Read);
  if(dir_fid == 0)
  {
    return 0;
  }

  DirList9P dirs = client9p_fid_read_dirs(ctx->arena, dir_fid);
  client9p_fid_close(ctx->arena, dir_fid);

  return dirs.first != 0;
}

////////////////////////////////
//~ Test Runner

typedef struct AuthTestCase AuthTestCase;
struct AuthTestCase
{
  String8 name;
  String8 proto;
  b32 (*func)(AuthTestContext *);
};

internal void
run_auth_tests(Arena *arena, String8 auth_addr, String8 fs_addr, b32 run_fido2)
{
  AuthTestCase tests[] = {
    {str8_lit("ed25519_register"),            str8_lit("ed25519"), test_register},
    {str8_lit("ed25519_authenticated_mount"), str8_lit("ed25519"), test_authenticated_mount},
    {str8_lit("fido2_register"),              str8_lit("fido2"),   test_register},
    {str8_lit("fido2_authenticated_mount"),   str8_lit("fido2"),   test_authenticated_mount},
  };

  u64 test_count = ArrayCount(tests);
  u64 passed = 0;
  u64 failed = 0;
  u64 skipped = 0;

  for(u64 i = 0; i < test_count; i += 1)
  {
    AuthTestCase *test = &tests[i];
    b32 is_fido2 = str8_match(test->proto, str8_lit("fido2"), 0);

    if(is_fido2 && !run_fido2)
    {
      skipped += 1;
      continue;
    }

    if(is_fido2)
    {
      fprintf(stdout, "%.*s - touch token when prompted\n", (int)test->name.size, test->name.str);
      fflush(stdout);
    }

    Temp scratch = scratch_begin(&arena, 1);
    AuthTestContext ctx = {0};
    ctx.arena = scratch.arena;
    ctx.auth_addr = auth_addr;
    ctx.fs_addr = fs_addr;
    ctx.proto = test->proto;

    b32 result = test->func(&ctx);
    scratch_end(scratch);

    if(result)
    {
      log_infof("PASS: %S\n", test->name);
      passed += 1;
    }
    else
    {
      log_errorf("FAIL: %S\n", test->name);
      failed += 1;
    }
  }

  log_infof("\ntest: %llu passed, %llu failed, %llu skipped\n", passed, failed, skipped);
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

  if(cmd_line->inputs.node_count < 2)
  {
    log_error(str8_lit("usage: 9auth-test [options] <9auth-addr> <9pfs-addr>\n"
                       "options:\n"
                       "  --fido2               Run FIDO2 tests (requires hardware token)\n"
                       "arguments:\n"
                       "  <9auth-addr>          Auth server dial string (e.g., unix!/run/9auth/socket)\n"
                       "  <9pfs-addr>           File server dial string (e.g., unix!/tmp/9pfs)\n"));
    log_scope_flush(scratch.arena);
    scratch_end(scratch);
    return;
  }

  b32 run_fido2 = cmd_line_has_flag(cmd_line, str8_lit("fido2"));
  String8 auth_addr = cmd_line->inputs.first->string;
  String8 fs_addr = cmd_line->inputs.first->next->string;

  run_auth_tests(scratch.arena, auth_addr, fs_addr, run_fido2);

  log_scope_flush(scratch.arena);
  scratch_end(scratch);
}
