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
  ClientFid9P *auth_fid = client9p_auth(arena, fs_client, auth_addr, str8_lit("fido2"), user, attach_path);
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
  ClientFid9P *auth_fid = client9p_auth(arena, client, auth_addr, str8_lit("fido2"), user, attach_path);
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
//~ Ed25519 Integration Tests

internal b32
test_ed25519_keygen(Arena *arena, String8 auth_addr)
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

  String8 keygen_cmd = str8_lit("keygen proto=ed25519 user=e2etest server=9p.e2etest-ed25519.local");
  s64 written = client9p_fid_pwrite(arena, ctl_fid, (void *)keygen_cmd.str, keygen_cmd.size, 0);
  if(written != (s64)keygen_cmd.size)
  {
    os_file_close(socket);
    return 0;
  }

  client9p_fid_close(arena, ctl_fid);
  os_file_close(socket);

  return 1;
}

internal b32
test_ed25519_auth_flow(Arena *arena, String8 fs_addr, String8 auth_addr)
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
  String8 attach_path = str8_lit("9p.e2etest-ed25519.local");
  ClientFid9P *auth_fid = client9p_auth(arena, fs_client, auth_addr, str8_lit("ed25519"), user, attach_path);
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

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  Arena *arena = arena_alloc();

  if(cmd_line->inputs.node_count < 2)
  {
    fprintf(stderr,
            "usage: 9auth-integration-test [--fido2] <9auth-addr> <9pfs-addr>\n"
            "\n"
            "Integration tests for 9auth authentication.\n"
            "\n"
            "By default, runs Ed25519 tests (no hardware required).\n"
            "Use --fido2 to also run FIDO2 tests (requires hardware token).\n"
            "\n"
            "Example:\n"
            "  9auth-integration-test unix!/var/run/9auth unix!/tmp/9pfs\n"
            "  9auth-integration-test --fido2 unix!/var/run/9auth unix!/tmp/9pfs\n"
            "\n");
    fflush(stderr);
    return;
  }

  b32 force_fido2 = cmd_line_has_flag(cmd_line, str8_lit("fido2"));

  String8 auth_addr = cmd_line->inputs.first->string;
  String8 fs_addr = cmd_line->inputs.first->next->string;

  u32 passed = 0;
  u32 failed = 0;
  u32 skipped = 0;

  b32 has_fido2 = 0;
  if(force_fido2)
  {
    has_fido2 = 1;
    fprintf(stdout, "FIDO2 tests: forced by --fido2 flag\n");
    fflush(stdout);
  }
  else
  {
    Auth_Fido2_DeviceList devices = auth_fido2_enumerate_devices(arena);
    has_fido2 = (devices.count > 0);
    if(has_fido2)
    {
      fprintf(stdout, "FIDO2 tests: hardware detected (%lu devices)\n", devices.count);
      fflush(stdout);
    }
    else
    {
      fprintf(stdout, "FIDO2 tests: skipped (no hardware, use --fido2 to force)\n");
      fflush(stdout);
    }
  }

  b32 test_ed_keygen = test_ed25519_keygen(arena, auth_addr);
  if(test_ed_keygen)
  {
    fprintf(stdout, "PASS: ed25519_keygen\n");
    fflush(stdout);
    passed += 1;
  }
  else
  {
    fprintf(stderr, "FAIL: ed25519_keygen\n");
    fflush(stderr);
    failed += 1;
  }

  b32 test_ed_auth = test_ed25519_auth_flow(arena, fs_addr, auth_addr);
  if(test_ed_auth)
  {
    fprintf(stdout, "PASS: ed25519_auth_flow\n");
    fflush(stdout);
    passed += 1;
  }
  else
  {
    fprintf(stderr, "FAIL: ed25519_auth_flow\n");
    fflush(stderr);
    failed += 1;
  }

  if(has_fido2)
  {
    fprintf(stdout, "9auth-integration-test: touch FIDO2 token\n");
    fflush(stdout);
    b32 test1 = test_register_credential(arena, auth_addr);
    if(test1)
    {
      fprintf(stdout, "PASS: fido2_register_credential\n");
      fflush(stdout);
      passed += 1;
    }
    else
    {
      fprintf(stderr, "FAIL: fido2_register_credential\n");
      fflush(stderr);
      failed += 1;
    }

    fprintf(stdout, "9auth-integration-test: touch FIDO2 token\n");
    fflush(stdout);
    b32 test2 = test_client_auth_flow(arena, fs_addr, auth_addr);
    if(test2)
    {
      fprintf(stdout, "PASS: fido2_client_auth_flow\n");
      fflush(stdout);
      passed += 1;
    }
    else
    {
      fprintf(stderr, "FAIL: fido2_client_auth_flow\n");
      fflush(stderr);
      failed += 1;
    }

    fprintf(stdout, "9auth-integration-test: touch FIDO2 token\n");
    fflush(stdout);
    b32 test3 = test_authenticated_mount(arena, fs_addr, auth_addr);
    if(test3)
    {
      fprintf(stdout, "PASS: fido2_authenticated_mount\n");
      fflush(stdout);
      passed += 1;
    }
    else
    {
      fprintf(stderr, "FAIL: fido2_authenticated_mount\n");
      fflush(stderr);
      failed += 1;
    }
  }
  else
  {
    skipped = 3;
  }

  fprintf(stdout, "\n");
  fprintf(stdout, "test: %u passed, %u failed, %u skipped\n", passed, failed, skipped);
  fflush(stdout);

  arena_release(arena);
}
