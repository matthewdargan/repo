#include "base/inc.h"
#include "9p/inc.h"
#include "auth/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
#include "auth/inc.c"

////////////////////////////////
//~ Integration Tests

internal b32
test_register_credential(Arena *arena, String8 auth_addr)
{
  OS_Handle socket = dial9p_connect(arena, auth_addr, str8_lit("unix"), str8_lit("564"));
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

  String8 user = get_user_name(arena);
  ClientFid9P *root = client9p_attach(arena, client, P9_FID_NONE, user, str8_lit("/"));
  if(root == 0)
  {
    os_file_close(socket);
    return 0;
  }
  client->root = root;

  ClientFid9P *ctl_fid = client9p_open(arena, client, str8_lit("ctl"), P9_OpenFlag_Write);
  if(ctl_fid == 0)
  {
    os_file_close(socket);
    return 0;
  }

  String8 register_cmd = str8_lit("register user=e2etest rp_id=9p.e2etest.local rp_name=E2ETest");
  s64 written = client9p_fid_pwrite(arena, ctl_fid, (void *)register_cmd.str, register_cmd.size, 0);
  if(written != (s64)register_cmd.size)
  {
    os_file_close(socket);
    return 0;
  }

  client9p_fid_close(arena, ctl_fid);
  os_file_close(socket);

  return 1;
}

internal b32
test_client_auth_flow(Arena *arena, String8 fs_addr, String8 auth_addr)
{
  OS_Handle fs_socket = dial9p_connect(arena, fs_addr, str8_lit("unix"), str8_lit("564"));
  if(os_handle_match(fs_socket, os_handle_zero()))
  {
    return 0;
  }

  u64 fs_fd = fs_socket.u64[0];
  Client9P *fs_client = client9p_init(arena, fs_fd);
  if(fs_client == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }

  String8 user = str8_lit("e2etest");
  String8 attach_path = str8_lit("9p.e2etest.local");
  ClientFid9P *auth_fid = client9p_auth(arena, fs_client, auth_addr, user, attach_path);
  if(auth_fid == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }

  ClientFid9P *root = client9p_attach(arena, fs_client, auth_fid->fid, user, attach_path);
  if(root == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }

  os_file_close(fs_socket);
  return 1;
}

internal b32
test_authenticated_mount(Arena *arena, String8 fs_addr, String8 auth_addr)
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

  String8 user = str8_lit("e2etest");
  String8 attach_path = str8_lit("9p.e2etest.local");
  ClientFid9P *auth_fid = client9p_auth(arena, client, auth_addr, user, attach_path);
  if(auth_fid == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }

  ClientFid9P *root = client9p_attach(arena, client, auth_fid->fid, user, attach_path);
  if(root == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }
  client->root = root;

  DirList9P dirs = client9p_fid_read_dirs(arena, root);
  u64 file_count = 0;
  for(DirNode9P *node = dirs.first; node != 0; node = node->next)
  {
    file_count += 1;
  }

  if(file_count == 0)
  {
    os_file_close(fs_socket);
    return 0;
  }

  os_file_close(fs_socket);
  return 1;
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  Arena *arena = arena_alloc();

  if(cmd_line->inputs.node_count < 2)
  {
    fprintf(stderr, "usage: 9auth-integration-test <9auth-addr> <9pfs-addr>\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Integration test requiring FIDO2 hardware.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Example:\n");
    fprintf(stderr, "  9auth-integration-test unix!/var/run/9auth unix!/tmp/9pfs\n");
    fprintf(stderr, "\n");
    fflush(stderr);
    return;
  }

  String8 auth_addr = cmd_line->inputs.first->string;
  String8 fs_addr = cmd_line->inputs.first->next->string;

  u32 passed = 0;
  u32 failed = 0;

  fprintf(stdout, "9auth-integration-test: touch FIDO2 token\n");
  fflush(stdout);
  b32 test1 = test_register_credential(arena, auth_addr);
  if(test1)
  {
    fprintf(stdout, "PASS: register_credential\n");
    fflush(stdout);
    passed += 1;
  }
  else
  {
    fprintf(stderr, "FAIL: register_credential\n");
    fflush(stderr);
    failed += 1;
    arena_release(arena);
    return;
  }

  fprintf(stdout, "9auth-integration-test: touch FIDO2 token\n");
  fflush(stdout);
  b32 test2 = test_client_auth_flow(arena, fs_addr, auth_addr);
  if(test2)
  {
    fprintf(stdout, "PASS: client_auth_flow\n");
    fflush(stdout);
    passed += 1;
  }
  else
  {
    fprintf(stderr, "FAIL: client_auth_flow\n");
    fflush(stderr);
    failed += 1;
    arena_release(arena);
    return;
  }

  fprintf(stdout, "9auth-integration-test: touch FIDO2 token\n");
  fflush(stdout);
  b32 test3 = test_authenticated_mount(arena, fs_addr, auth_addr);
  if(test3)
  {
    fprintf(stdout, "PASS: authenticated_mount\n");
    fflush(stdout);
    passed += 1;
  }
  else
  {
    fprintf(stderr, "FAIL: authenticated_mount\n");
    fflush(stderr);
    failed += 1;
    arena_release(arena);
    return;
  }

  fprintf(stdout, "\n");
  fprintf(stdout, "test: %u passed, %u failed\n", passed, failed);
  fflush(stdout);

  arena_release(arena);
}
