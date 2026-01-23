#include "base/inc.h"
#include "auth/inc.h"
#include "base/inc.c"
#include "auth/core.c"
#include "auth/fido2_mock.c"
#include "auth/rpc.c"

////////////////////////////////
//~ Key Ring Tests

internal b32
test_keyring_add(Arena *arena)
{
  Auth_KeyRing ring = auth_keyring_alloc(arena, 8);

  Auth_Key key = {0};
  key.user = str8_lit("alice");
  key.rp_id = str8_lit("9p.localhost");
  key.credential_id_len = 32;
  MemorySet(key.credential_id, 0xAA, 32);
  key.public_key_len = 65;
  MemorySet(key.public_key, 0xBB, 65);

  String8 error = str8_zero();
  auth_keyring_add(&ring, &key, &error);

  return ring.count == 1 && str8_match(ring.keys[0].user, str8_lit("alice"), 0) &&
         str8_match(ring.keys[0].rp_id, str8_lit("9p.localhost"), 0) && ring.keys[0].credential_id_len == 32 &&
         ring.keys[0].public_key_len == 65;
}

internal b32
test_keyring_lookup(Arena *arena)
{
  Auth_KeyRing ring = auth_keyring_alloc(arena, 8);

  Auth_Key key1 = {0};
  key1.user = str8_lit("alice");
  key1.rp_id = str8_lit("9p.localhost");
  key1.credential_id_len = 32;
  MemorySet(key1.credential_id, 0xAA, 32);
  key1.public_key_len = 65;
  MemorySet(key1.public_key, 0xAA, 65);

  Auth_Key key2 = {0};
  key2.user = str8_lit("bob");
  key2.rp_id = str8_lit("9p.example.com");
  key2.credential_id_len = 32;
  MemorySet(key2.credential_id, 0xBB, 32);
  key2.public_key_len = 65;
  MemorySet(key2.public_key, 0xBB, 65);

  auth_keyring_add(&ring, &key1, 0);
  auth_keyring_add(&ring, &key2, 0);

  Auth_Key *found1 = auth_keyring_lookup(&ring, str8_lit("alice"), str8_lit("9p.localhost"));
  Auth_Key *found2 = auth_keyring_lookup(&ring, str8_lit("bob"), str8_lit("9p.example.com"));
  Auth_Key *not_found = auth_keyring_lookup(&ring, str8_lit("charlie"), str8_lit("9p.localhost"));

  return found1 != 0 && found2 != 0 && not_found == 0 && str8_match(found1->user, str8_lit("alice"), 0) &&
         str8_match(found2->user, str8_lit("bob"), 0);
}

internal b32
test_keyring_remove(Arena *arena)
{
  Auth_KeyRing ring = auth_keyring_alloc(arena, 8);

  Auth_Key key1 = {0};
  key1.user = str8_lit("alice");
  key1.rp_id = str8_lit("9p.localhost");
  key1.credential_id_len = 32;
  MemorySet(key1.credential_id, 0xAA, 32);
  key1.public_key_len = 65;
  MemorySet(key1.public_key, 0xAA, 65);

  Auth_Key key2 = {0};
  key2.user = str8_lit("bob");
  key2.rp_id = str8_lit("9p.example.com");
  key2.credential_id_len = 32;
  MemorySet(key2.credential_id, 0xBB, 32);
  key2.public_key_len = 65;
  MemorySet(key2.public_key, 0xBB, 65);

  auth_keyring_add(&ring, &key1, 0);
  auth_keyring_add(&ring, &key2, 0);

  if(ring.count != 2)
  {
    return 0;
  }

  auth_keyring_remove(&ring, str8_lit("alice"), str8_lit("9p.localhost"));

  if(ring.count != 1)
  {
    return 0;
  }

  Auth_Key *found_alice = auth_keyring_lookup(&ring, str8_lit("alice"), str8_lit("9p.localhost"));
  Auth_Key *found_bob = auth_keyring_lookup(&ring, str8_lit("bob"), str8_lit("9p.example.com"));

  return found_alice == 0 && found_bob != 0;
}

internal b32
test_keyring_save_load(Arena *arena)
{
  Temp scratch = scratch_begin(&arena, 1);

  Auth_KeyRing ring1 = auth_keyring_alloc(scratch.arena, 8);

  Auth_Key key1 = {0};
  key1.user = str8_lit("alice");
  key1.rp_id = str8_lit("9p.localhost");
  key1.credential_id_len = 16;
  for(u64 i = 0; i < 16; i += 1)
  {
    key1.credential_id[i] = (u8)i;
  }
  key1.public_key_len = 32;
  for(u64 i = 0; i < 32; i += 1)
  {
    key1.public_key[i] = (u8)(i + 100);
  }

  auth_keyring_add(&ring1, &key1, 0);

  String8 saved = auth_keyring_save(scratch.arena, &ring1);

  Auth_KeyRing ring2 = auth_keyring_alloc(arena, 8);
  b32 load_ok = auth_keyring_load(arena, &ring2, saved);

  if(!load_ok)
  {
    scratch_end(scratch);
    return 0;
  }

  if(ring2.count != 1)
  {
    scratch_end(scratch);
    return 0;
  }

  Auth_Key *loaded = &ring2.keys[0];
  b32 result = str8_match(loaded->user, str8_lit("alice"), 0) &&
               str8_match(loaded->rp_id, str8_lit("9p.localhost"), 0) && loaded->credential_id_len == 16 &&
               loaded->public_key_len == 32 && MemoryMatch(loaded->credential_id, key1.credential_id, 16) &&
               MemoryMatch(loaded->public_key, key1.public_key, 32);

  scratch_end(scratch);
  return result;
}

////////////////////////////////
//~ RPC Parsing Tests

internal b32
test_rpc_parse_start(Arena *arena)
{
  String8 cmd = str8_lit("start proto=fido2 role=client user=alice server=9p.localhost");
  Auth_RPC_Request req = auth_rpc_parse(arena, cmd);

  return req.command == Auth_RPC_Command_Start && str8_match(req.start.proto, str8_lit("fido2"), 0) &&
         str8_match(req.start.role, str8_lit("client"), 0) && str8_match(req.start.user, str8_lit("alice"), 0) &&
         str8_match(req.start.server, str8_lit("9p.localhost"), 0);
}

internal b32
test_rpc_parse_read(Arena *arena)
{
  String8 cmd = str8_lit("read");
  Auth_RPC_Request req = auth_rpc_parse(arena, cmd);

  return req.command == Auth_RPC_Command_Read;
}

internal b32
test_rpc_parse_empty(Arena *arena)
{
  String8 cmd = str8_lit("");
  Auth_RPC_Request req = auth_rpc_parse(arena, cmd);

  return req.command == Auth_RPC_Command_None;
}

internal b32
test_rpc_parse_whitespace(Arena *arena)
{
  String8 cmd = str8_lit("   start   proto=fido2   user=alice  ");
  Auth_RPC_Request req = auth_rpc_parse(arena, cmd);

  return req.command == Auth_RPC_Command_Start && str8_match(req.start.proto, str8_lit("fido2"), 0) &&
         str8_match(req.start.user, str8_lit("alice"), 0);
}

////////////////////////////////
//~ RPC State Machine Tests

internal b32
test_rpc_handle_start_success(Arena *arena)
{
  Auth_KeyRing ring = auth_keyring_alloc(arena, 8);
  Auth_RPC_State *state = auth_rpc_state_alloc(arena, &ring);

  Auth_Conv *conv = 0;
  Auth_RPC_StartParams params = {0};
  params.proto = str8_lit("fido2");
  params.role = str8_lit("client");
  params.user = str8_lit("alice");
  params.server = str8_lit("9p.localhost");

  Auth_RPC_Response resp = auth_rpc_handle_start(arena, state, &conv, params);

  return resp.error.size == 0 && conv != 0 && conv->state == Auth_State_Started &&
         str8_match(conv->user, str8_lit("alice"), 0);
}

internal b32
test_rpc_handle_start_invalid_proto(Arena *arena)
{
  Auth_KeyRing ring = auth_keyring_alloc(arena, 8);
  Auth_RPC_State *state = auth_rpc_state_alloc(arena, &ring);

  Auth_Conv *conv = 0;
  Auth_RPC_StartParams params = {0};
  params.proto = str8_lit("invalid");
  params.role = str8_lit("client");
  params.user = str8_lit("alice");

  Auth_RPC_Response resp = auth_rpc_handle_start(arena, state, &conv, params);

  return resp.error.size > 0 && conv == 0;
}

internal b32
test_rpc_handle_start_invalid_role(Arena *arena)
{
  Auth_KeyRing ring = auth_keyring_alloc(arena, 8);
  Auth_RPC_State *state = auth_rpc_state_alloc(arena, &ring);

  Auth_Conv *conv = 0;
  Auth_RPC_StartParams params = {0};
  params.proto = str8_lit("fido2");
  params.role = str8_lit("invalid");
  params.user = str8_lit("alice");

  Auth_RPC_Response resp = auth_rpc_handle_start(arena, state, &conv, params);

  return resp.error.size > 0;
}

internal b32
test_rpc_handle_start_missing_user(Arena *arena)
{
  Auth_KeyRing ring = auth_keyring_alloc(arena, 8);
  Auth_RPC_State *state = auth_rpc_state_alloc(arena, &ring);

  Auth_Conv *conv = 0;
  Auth_RPC_StartParams params = {0};
  params.proto = str8_lit("fido2");
  params.role = str8_lit("client");
  params.user = str8_zero();

  Auth_RPC_Response resp = auth_rpc_handle_start(arena, state, &conv, params);

  return resp.error.size > 0;
}

////////////////////////////////
//~ FIDO2 Mock Tests

internal b32
test_fido2_register_credential(Arena *arena)
{
  Auth_Fido2_RegisterParams params = {0};
  params.user = str8_lit("alice");
  params.rp_id = str8_lit("9p.localhost");
  params.rp_name = str8_lit("9P Authentication");
  params.require_uv = 0;

  Auth_Key key = {0};
  String8 error = str8_zero();

  b32 ok = auth_fido2_register_credential(arena, params, &key, &error);

  return ok && error.size == 0 && key.credential_id_len > 0 && key.public_key_len > 0 &&
         str8_match(key.user, str8_lit("alice"), 0);
}

internal b32
test_fido2_get_assertion(Arena *arena)
{
  Auth_Fido2_RegisterParams reg_params = {0};
  reg_params.user = str8_lit("alice");
  reg_params.rp_id = str8_lit("9p.localhost");
  reg_params.rp_name = str8_lit("9P Authentication");

  Auth_Key key = {0};
  String8 error = str8_zero();
  b32 ok = auth_fido2_register_credential(arena, reg_params, &key, &error);
  if(!ok)
  {
    return 0;
  }

  u8 challenge[32] = {0};
  auth_fido2_generate_challenge(challenge);

  Auth_Fido2_AssertParams params = {0};
  params.rp_id = str8_lit("9p.localhost");
  MemoryCopy(params.challenge, challenge, 32);
  params.credential_id = key.credential_id;
  params.credential_id_len = key.credential_id_len;
  params.require_uv = 0;

  Auth_Fido2_Assertion assertion = {0};
  ok = auth_fido2_get_assertion(arena, &params, &assertion, &error);

  return ok && error.size == 0 && assertion.signature_len > 0 && assertion.auth_data_len > 0;
}

internal b32
test_fido2_verify_signature(Arena *arena)
{
  Auth_Fido2_RegisterParams reg_params = {0};
  reg_params.user = str8_lit("alice");
  reg_params.rp_id = str8_lit("9p.localhost");
  reg_params.rp_name = str8_lit("9P Authentication");

  Auth_Key key = {0};
  String8 error = str8_zero();
  b32 ok = auth_fido2_register_credential(arena, reg_params, &key, &error);
  if(!ok)
  {
    return 0;
  }

  u8 challenge[32] = {0};
  auth_fido2_generate_challenge(challenge);

  Auth_Fido2_AssertParams assert_params = {0};
  assert_params.rp_id = str8_lit("9p.localhost");
  MemoryCopy(assert_params.challenge, challenge, 32);
  assert_params.credential_id = key.credential_id;
  assert_params.credential_id_len = key.credential_id_len;

  Auth_Fido2_Assertion assertion = {0};
  ok = auth_fido2_get_assertion(arena, &assert_params, &assertion, &error);
  if(!ok)
  {
    return 0;
  }

  Auth_Fido2_VerifyParams verify_params = {0};
  MemoryCopy(verify_params.challenge, challenge, 32);
  verify_params.auth_data = assertion.auth_data;
  verify_params.auth_data_len = assertion.auth_data_len;
  verify_params.signature = assertion.signature;
  verify_params.signature_len = assertion.signature_len;
  verify_params.public_key = key.public_key;
  verify_params.public_key_len = key.public_key_len;

  ok = auth_fido2_verify_signature(&verify_params, &error);

  return ok && error.size == 0;
}

////////////////////////////////
//~ Error Case Tests

internal b32
test_error_invalid_credential(Arena *arena)
{
  u8 challenge[32] = {0};
  auth_fido2_generate_challenge(challenge);

  u8 invalid_cred[32];
  MemorySet(invalid_cred, 0xFF, 32);

  Auth_Fido2_AssertParams params = {0};
  params.rp_id = str8_lit("9p.localhost");
  MemoryCopy(params.challenge, challenge, 32);
  params.credential_id = invalid_cred;
  params.credential_id_len = 32;

  Auth_Fido2_Assertion assertion = {0};
  String8 error = str8_zero();
  b32 ok = auth_fido2_get_assertion(arena, &params, &assertion, &error);

  return !ok && error.size > 0;
}

internal b32
test_error_missing_key_lookup(Arena *arena)
{
  Auth_KeyRing ring = auth_keyring_alloc(arena, 8);

  Auth_Key *key = auth_keyring_lookup(&ring, str8_lit("nonexistent"), str8_lit("9p.localhost"));

  return key == 0;
}

internal b32
test_error_invalid_serialization(Arena *arena)
{
  String8 invalid_data = str8_lit("invalid data format\nmore bad data");

  Auth_KeyRing ring = auth_keyring_alloc(arena, 8);
  b32 ok = auth_keyring_load(arena, &ring, invalid_data);

  return !ok;
}

////////////////////////////////
//~ Test Runner

typedef struct TestCase TestCase;
struct TestCase
{
  String8 name;
  b32 (*func)(Arena *);
};

internal void
run_tests(Arena *arena)
{
  TestCase tests[] = {
      {str8_lit("keyring_add"), test_keyring_add},
      {str8_lit("keyring_lookup"), test_keyring_lookup},
      {str8_lit("keyring_remove"), test_keyring_remove},
      {str8_lit("keyring_save_load"), test_keyring_save_load},
      {str8_lit("rpc_parse_start"), test_rpc_parse_start},
      {str8_lit("rpc_parse_read"), test_rpc_parse_read},
      {str8_lit("rpc_parse_empty"), test_rpc_parse_empty},
      {str8_lit("rpc_parse_whitespace"), test_rpc_parse_whitespace},
      {str8_lit("rpc_handle_start_success"), test_rpc_handle_start_success},
      {str8_lit("rpc_handle_start_invalid_proto"), test_rpc_handle_start_invalid_proto},
      {str8_lit("rpc_handle_start_invalid_role"), test_rpc_handle_start_invalid_role},
      {str8_lit("rpc_handle_start_missing_user"), test_rpc_handle_start_missing_user},
      {str8_lit("fido2_register_credential"), test_fido2_register_credential},
      {str8_lit("fido2_get_assertion"), test_fido2_get_assertion},
      {str8_lit("fido2_verify_signature"), test_fido2_verify_signature},
      {str8_lit("error_invalid_credential"), test_error_invalid_credential},
      {str8_lit("error_missing_key_lookup"), test_error_missing_key_lookup},
      {str8_lit("error_invalid_serialization"), test_error_invalid_serialization},
  };

  u64 test_count = ArrayCount(tests);
  u64 passed = 0;
  u64 failed = 0;

  for(u64 i = 0; i < test_count; i += 1)
  {
    Temp scratch = scratch_begin(&arena, 1);
    b32 result = tests[i].func(scratch.arena);
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

  log_infof("test: %llu passed, %llu failed\n", passed, failed);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  (void)cmd_line;

  Temp scratch = scratch_begin(0, 0);
  Log *log = log_alloc();
  log_select(log);
  log_scope_begin();

  run_tests(scratch.arena);

  log_scope_flush(scratch.arena);
  scratch_end(scratch);
}
