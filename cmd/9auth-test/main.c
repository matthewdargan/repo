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
  String8 auth_daemon;
  String8 fs_addr;
  String8 proto;
};

////////////////////////////////
//~ Test Helpers

internal Client9P *
auth_mount(Arena *arena, String8 addr, String8 user)
{
  OS_Handle socket = dial9p_connect(arena, addr, str8_lit("unix"), str8_lit("564"));
  if(os_handle_match(socket, os_handle_zero())) { return 0; }

  u64 fd           = socket.u64[0];
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
fs_mount_auth(Arena *arena, String8 fs_addr, String8 auth_daemon, String8 auth_id, String8 proto, String8 user, String8 aname)
{
  OS_Handle fs_socket = dial9p_connect(arena, fs_addr, str8_lit("unix"), str8_lit("564"));
  if(os_handle_match(fs_socket, os_handle_zero())) { return 0; }

  u64 fs_fd        = fs_socket.u64[0];
  Client9P *client = client9p_init(arena, fs_fd);
  if(client == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }

  ClientFid9P *auth_fid = client9p_auth(arena, client, auth_daemon, auth_id, proto, user, aname);
  if(auth_fid == 0)
  {
    client9p_unmount(arena, client);
    return 0;
  }

  ClientFid9P *root = client9p_attach(arena, client, auth_fid->fid, user, aname);
  if(root == 0)
  {
    client9p_fid_close(arena, auth_fid);
    client9p_unmount(arena, client);
    return 0;
  }
  client->root = root;
  client->auth_fid = auth_fid;
  return client;
}

////////////////////////////////
//~ Test Functions

internal b32
test_register(AuthTestContext *ctx)
{
  String8 user     = get_user_name(ctx->arena);
  Client9P *client = auth_mount(ctx->arena, ctx->auth_daemon, user);
  if(client == 0) { return 0; }

  ClientFid9P *ctl_fid = client9p_open(ctx->arena, client, str8_lit("ctl"), P9_OpenFlag_Write);
  if(ctl_fid == 0) { return 0; }

  if(str8_match(ctx->proto, str8_lit("fido2"), 0))
  {
    fprintf(stdout, "9auth-test: touch FIDO2 token\n");
    fflush(stdout);
  }

  Temp scratch         = scratch_begin(&ctx->arena, 1);
  String8 register_cmd = str8f(scratch.arena, "register user=e2etest auth-id=e2etest.local proto=%S", ctx->proto);
  s64 written          = client9p_fid_pwrite(ctx->arena, ctl_fid, (void *)register_cmd.str, register_cmd.size, 0);
  scratch_end(scratch);

  client9p_fid_close(ctx->arena, ctl_fid);
  return written == (s64)register_cmd.size;
}

internal b32
test_authenticated_mount(AuthTestContext *ctx)
{
  String8 user    = str8_lit("e2etest");
  String8 auth_id = str8_lit("e2etest.local");
  String8 aname   = str8_lit("/");

  Client9P *client = fs_mount_auth(ctx->arena, ctx->fs_addr, ctx->auth_daemon, auth_id, ctx->proto, user, aname);
  if(client == 0) { return 0; }

  ClientFid9P *dir_fid = client9p_open(ctx->arena, client, str8_lit("."), P9_OpenFlag_Read);
  if(dir_fid == 0) { return 0; }

  DirList9P dirs = client9p_fid_read_dirs(ctx->arena, dir_fid);
  client9p_fid_close(ctx->arena, dir_fid);

  return dirs.first != 0;
}

internal b32
test_reject_bad_signature(AuthTestContext *ctx)
{
  String8 user    = str8_lit("e2etest");
  String8 auth_id = str8_lit("e2etest.local");
  String8 aname   = str8_lit("/");

  OS_Handle fs_socket = dial9p_connect(ctx->arena, ctx->fs_addr, str8_lit("unix"), str8_lit("564"));
  if(os_handle_match(fs_socket, os_handle_zero())) { return 0; }

  u64 fs_fd        = fs_socket.u64[0];
  Client9P *client = client9p_init(ctx->arena, fs_fd);
  if(client == 0) { os_file_close(fs_socket); return 0; }

  OS_Handle auth_handle = dial9p_connect(ctx->arena, ctx->auth_daemon, str8_lit("unix"), str8_lit("9auth"));
  if(os_handle_match(auth_handle, os_handle_zero())) { os_file_close(fs_socket); return 0; }

  u64 auth_fd           = auth_handle.u64[0];
  Client9P *auth_client = client9p_init(ctx->arena, auth_fd);
  if(auth_client == 0) { os_file_close(auth_handle); os_file_close(fs_socket); return 0; }

  ClientFid9P *auth_root = client9p_attach(ctx->arena, auth_client, P9_FID_NONE, user, str8_lit("/"));
  if(auth_root == 0) { os_file_close(auth_handle); os_file_close(fs_socket); return 0; }

  ClientFid9P *rpc_fid = client9p_fid_walk(ctx->arena, auth_root, str8_lit("rpc"));
  if(rpc_fid == 0) { os_file_close(auth_handle); os_file_close(fs_socket); return 0; }

  if(!client9p_fid_open(ctx->arena, rpc_fid, P9_OpenFlag_ReadWrite))
  {
    os_file_close(auth_handle);
    os_file_close(fs_socket);
    return 0;
  }

  ClientFid9P *server_auth_fid = client9p_tauth(ctx->arena, client, user, aname);
  if(server_auth_fid == 0) { os_file_close(auth_handle); os_file_close(fs_socket); return 0; }

  String8 start_cmd = str8f(ctx->arena, "start proto=%S role=client user=%S auth-id=%S", ctx->proto, user, auth_id);
  s64 write_result  = client9p_fid_pwrite(ctx->arena, rpc_fid, (void *)start_cmd.str, start_cmd.size, 0);
  if(write_result != (s64)start_cmd.size) { os_file_close(auth_handle); os_file_close(fs_socket); return 0; }

  u8 challenge[32];
  s64 challenge_len = client9p_fid_pread(ctx->arena, server_auth_fid, challenge, sizeof(challenge), 0);
  if(challenge_len != sizeof(challenge)) { os_file_close(auth_handle); os_file_close(fs_socket); return 0; }

  write_result = client9p_fid_pwrite(ctx->arena, rpc_fid, challenge, sizeof(challenge), 0);
  if(write_result != sizeof(challenge)) { os_file_close(auth_handle); os_file_close(fs_socket); return 0; }

  u8 auth_response[512];
  s64 auth_response_len = client9p_fid_pread(ctx->arena, rpc_fid, auth_response, sizeof(auth_response), 0);
  if(auth_response_len <= 0) { os_file_close(auth_handle); os_file_close(fs_socket); return 0; }

  // Corrupt the signature by flipping a bit
  if(auth_response_len > 40) { auth_response[40] ^= 0x01; }

  write_result = client9p_fid_pwrite(ctx->arena, server_auth_fid, auth_response, auth_response_len, 0);

  u8 done_response[16];
  s64 done_len = client9p_fid_pread(ctx->arena, server_auth_fid, done_response, sizeof(done_response), 0);

  client9p_fid_close(ctx->arena, rpc_fid);
  os_file_close(auth_handle);

  // Should get an error or short read (not "done")
  b32 rejected = (done_len <= 0 || done_len != 4);

  ClientFid9P *root = client9p_attach(ctx->arena, client, server_auth_fid->fid, user, aname);
  b32 attach_failed = (root == 0);

  os_file_close(fs_socket);

  return rejected || attach_failed;
}

internal b32
test_reject_wrong_user(AuthTestContext *ctx)
{
  String8 registered_user = str8_lit("e2etest");
  String8 wrong_user      = str8_lit("attacker");
  String8 auth_id         = str8_lit("e2etest.local");
  String8 aname           = str8_lit("/");

  Client9P *client = fs_mount_auth(ctx->arena, ctx->fs_addr, ctx->auth_daemon, auth_id, ctx->proto, wrong_user, aname);

  b32 rejected = (client == 0);
  if(client != 0) { close(client->fd); }

  return rejected;
}

internal b32
test_rate_limiting(AuthTestContext *ctx)
{

  String8 user    = str8_lit("badactor");
  String8 auth_id = str8_lit("e2etest.local");
  String8 aname   = str8_lit("/");

  log_infof("rate_limiting: starting 5 auth attempts\n");

  // Attempt 5 failed authentications with wrong user
  for(u64 i = 0; i < 5; i += 1)
  {

    Temp temp = scratch_begin(&ctx->arena, 1);
    Client9P *client = fs_mount_auth(temp.arena, ctx->fs_addr, ctx->auth_daemon, auth_id, ctx->proto, user, aname);
    log_infof("rate_limiting: attempt %llu %s\n", i+1, client ? "success" : "failed");


    if(client != 0) { close(client->fd); }
    scratch_end(temp);
  }

  log_infof("rate_limiting: checking 6th attempt for rate limit\n");

  // 6th attempt should be rate limited (locked out for 5 seconds)
  Temp temp        = scratch_begin(&ctx->arena, 1);
  u64 start_us     = os_now_microseconds();
  Client9P *client = fs_mount_auth(temp.arena, ctx->fs_addr, ctx->auth_daemon, auth_id, ctx->proto, user, aname);
  u64 elapsed_us   = os_now_microseconds() - start_us;

  b32 was_rate_limited = (client == 0 && elapsed_us < Million(1));
  log_infof("rate_limiting: 6th attempt took %llu us, rate_limited=%d\n", elapsed_us, was_rate_limited);


  if(client != 0) { close(client->fd); }
  scratch_end(temp);

  return was_rate_limited;
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
run_auth_tests(Arena *arena, String8 auth_daemon, String8 fs_addr, b32 run_fido2)
{
  AuthTestCase tests[] = {
    {str8_lit("ed25519_register"),             str8_lit("ed25519"), test_register},
    {str8_lit("ed25519_authenticated_mount"),  str8_lit("ed25519"), test_authenticated_mount},
    {str8_lit("ed25519_reject_bad_signature"), str8_lit("ed25519"), test_reject_bad_signature},
    {str8_lit("ed25519_reject_wrong_user"),    str8_lit("ed25519"), test_reject_wrong_user},
    {str8_lit("ed25519_rate_limiting"),        str8_lit("ed25519"), test_rate_limiting},
    {str8_lit("fido2_register"),               str8_lit("fido2"),   test_register},
    {str8_lit("fido2_authenticated_mount"),    str8_lit("fido2"),   test_authenticated_mount},
  };

  u64 test_count = ArrayCount(tests);
  u64 passed     = 0;
  u64 failed     = 0;
  u64 skipped    = 0;

  for(u64 i = 0; i < test_count; i += 1)
  {
    AuthTestCase *test = &tests[i];
    b32 is_fido2       = str8_match(test->proto, str8_lit("fido2"), 0);

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
    ctx.arena       = scratch.arena;
    ctx.auth_daemon = auth_daemon;
    ctx.fs_addr     = fs_addr;
    ctx.proto       = test->proto;

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

  b32 run_fido2       = cmd_line_has_flag(cmd_line, str8_lit("fido2"));
  String8 auth_daemon = cmd_line->inputs.first->string;
  String8 fs_addr     = cmd_line->inputs.first->next->string;

  run_auth_tests(scratch.arena, auth_daemon, fs_addr, run_fido2);

  log_scope_flush(scratch.arena);
  scratch_end(scratch);
}
