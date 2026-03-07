////////////////////////////////
//~ Rate Limiting

internal u64
auth_rate_limit_hash(String8 user, String8 auth_id)
{
  u64 hash = 5381;
  for(u64 i = 0; i < user.size; i += 1)    { hash = ((hash << 5) + hash) + user.str[i]; }
  for(u64 i = 0; i < auth_id.size; i += 1) { hash = ((hash << 5) + hash) + auth_id.str[i]; }
  return hash;
}

internal b32
auth_is_rate_limited(Auth_RPC_State *state, String8 user, String8 auth_id, u64 now_us)
{
  u64 hash   = auth_rate_limit_hash(user, auth_id);
  u64 bucket = hash % AUTH_RATE_LIMIT_BUCKETS;

  for(Auth_RateLimit *rl = state->rate_limit_buckets[bucket]; rl != 0; rl = rl->next)
  {
    if(rl->hash == hash)
    {
      if(now_us < rl->lockout_until_us) { return 1; }

      rl->attempt_count    = 0;
      rl->lockout_until_us = 0;
      break;
    }
  }
  return 0;
}

internal void
auth_record_auth_attempt(Auth_RPC_State *state, String8 user, String8 auth_id, b32 success, u64 now_us)
{
  u64 hash   = auth_rate_limit_hash(user, auth_id);
  u64 bucket = hash % AUTH_RATE_LIMIT_BUCKETS;

  Auth_RateLimit *rl = 0;
  for(Auth_RateLimit *r = state->rate_limit_buckets[bucket]; r != 0; r = r->next)
  {
    if(r->hash == hash)
    {
      rl = r;
      break;
    }
  }

  if(rl == 0)
  {
    rl       = push_array(state->arena, Auth_RateLimit, 1);
    rl->hash = hash;
    rl->next = state->rate_limit_buckets[bucket];
    state->rate_limit_buckets[bucket] = rl;
  }

  if(success)
  {
    rl->attempt_count    = 0;
    rl->lockout_until_us = 0;
  }
  else
  {
    rl->attempt_count   += 1;
    rl->last_attempt_us  = now_us;

    if(rl->attempt_count >= AUTH_MAX_ATTEMPTS) { rl->lockout_until_us = now_us + AUTH_LOCKOUT_US; }
  }
}

////////////////////////////////
//~ RPC Functions

internal void
auth_rpc_cleanup_expired_conversations(Auth_RPC_State *state)
{
  u64 now_us = os_now_microseconds();
  MutexScope(state->mutex)
  {
    Auth_Conv **prev_next = &state->conv_first;
    for(Auth_Conv *conv = state->conv_first; conv != 0;)
    {
      Auth_Conv *next     = conv->next;
      b32 should_remove   = 0;
      u64 max_conv_age_us = AUTH_RPC_CONVERSATION_MAX_AGE_US;
      u64 done_cleanup_us = AUTH_RPC_DONE_CLEANUP_DELAY_US;

      if(conv->state == Auth_State_Done || conv->state == Auth_State_Error) { should_remove = ((now_us - conv->start_time_us) > done_cleanup_us); }
      else if((now_us - conv->start_time_us) > max_conv_age_us)             { should_remove = 1; }

      if(should_remove)
      {
        SecureMemoryZero(conv->challenge, sizeof(conv->challenge));
        SecureMemoryZero(conv->signature, sizeof(conv->signature));
        SecureMemoryZero(conv->auth_data, sizeof(conv->auth_data));
        *prev_next = next;
        if(state->conv_last == conv) { state->conv_last = 0; }
        state->conv_count -= 1;
      }
      else { prev_next = &conv->next; }

      conv = next;
    }
  }
}

internal Auth_RPC_State *
auth_rpc_state_alloc(Arena *arena, Auth_KeyRing *keyring, String8 keys_path, String8 passphrase)
{
  Auth_RPC_State *state = push_array(arena, Auth_RPC_State, 1);
  state->arena          = arena;
  state->keyring        = keyring;
  state->keys_path      = str8_copy(arena, keys_path);
  state->passphrase     = str8_copy(arena, passphrase);
  state->mutex          = mutex_alloc();
  state->next_conv_id   = 1;
  return state;
}

internal Auth_RPC_Request
auth_rpc_parse(Arena *arena, String8 command_line)
{
  Auth_RPC_Request request = {0};
  String8 trimmed          = str8_skip_chop_whitespace(command_line);
  if(trimmed.size == 0) { return request; }

  String8List parts = str8_split(arena, trimmed, (u8 *)" ", 1, 0);
  if(parts.node_count == 0) { return request; }

  String8 command = parts.first->string;
  if(str8_match(command, str8_lit("start"), 0))
  {
    request.command = Auth_RPC_Command_Start;
    for(String8Node *node = parts.first->next; node != 0; node = node->next)
    {
      String8 param  = node->string;
      String8List kv = str8_split(arena, param, (u8 *)"=", 1, 0);
      if(kv.node_count != 2) { continue; }

      String8 key   = kv.first->string;
      String8 value = kv.first->next->string;

      if(str8_match(key, str8_lit("user"), 0))         { request.start.user    = value; }
      else if(str8_match(key, str8_lit("auth-id"), 0)) { request.start.auth_id = value; }
      else if(str8_match(key, str8_lit("proto"), 0))   { request.start.proto   = value; }
      else if(str8_match(key, str8_lit("role"), 0))    { request.start.role    = value; }
    }
  }
  else if(str8_match(command, str8_lit("read"), 0)) { request.command = Auth_RPC_Command_Read; }
  else                                              { request.command = Auth_RPC_Command_Write; }

  return request;
}

internal Auth_RPC_Response
auth_rpc_handle_start(Auth_RPC_State *state, Auth_Conv **out_conv, Auth_RPC_StartParams params)
{
  Auth_RPC_Response response = {0};
  u64 now_us                 = os_now_microseconds();

  auth_rpc_cleanup_expired_conversations(state);

  if(params.user.size == 0)
  {
    response.error = str8_lit("auth: user required");
    return response;
  }

  MutexScope(state->mutex)
  {
    if(auth_is_rate_limited(state, params.user, params.auth_id, now_us))
    {
      response.error = str8_lit("auth: rate limited");
      return response;
    }
  }

  b32 has_proto        = params.proto.size > 0;
  b32 proto_is_ed25519 = str8_match(params.proto, str8_lit("ed25519"), 0);
  b32 proto_is_fido2   = str8_match(params.proto, str8_lit("fido2"), 0);
  if(has_proto && !proto_is_ed25519 && !proto_is_fido2)
  {
    response.error = str8_lit("auth: unsupported protocol");
    return response;
  }
  if(!str8_match(params.role, str8_lit("client"), 0) && !str8_match(params.role, str8_lit("server"), 0))
  {
    response.error = str8_lit("auth: invalid role");
    return response;
  }

  b32 is_server = str8_match(params.role, str8_lit("server"), 0);
  b32 is_client = str8_match(params.role, str8_lit("client"), 0);

  Auth_Key *key = 0;

  b32 should_reload = 0;
  MutexScope(state->mutex)
  {
    u64 elapsed_us = now_us - state->last_keyring_reload_us;
    should_reload  = (elapsed_us >= AUTH_KEYRING_RELOAD_INTERVAL_US);
  }

  if(should_reload)
  {
    Temp scratch = scratch_begin(&state->arena, 1);
    String8 data = os_data_from_file_path(scratch.arena, state->keys_path);
    if(data.size > 0)
    {
      Auth_KeyRing temp_ring = auth_keyring_alloc(state->arena, 0);
      if(auth_keyring_load(state->arena, &temp_ring, data, state->passphrase))
      {
        MutexScope(state->mutex)
        {
          SecureMemoryZero(state->keyring->keys, state->keyring->count * sizeof(Auth_Key));
          state->keyring->count         = temp_ring.count;
          state->keyring->keys          = temp_ring.keys;
          state->last_keyring_reload_us = now_us;
        }
      }
      SecureMemoryZero((void*)data.str, data.size);
    }
    scratch_end(scratch);
  }

  if(has_proto || is_client)
  {
    MutexScope(state->mutex)
    {
      for(u64 i = 0; i < state->keyring->count; i += 1)
      {
        Auth_Key *candidate = &state->keyring->keys[i];

        b32 user_match    = (candidate->user.size == params.user.size &&
                             CRYPTO_memcmp(candidate->user.str, params.user.str, params.user.size) == 0);
        b32 auth_id_match = (candidate->auth_id.size == params.auth_id.size &&
                             CRYPTO_memcmp(candidate->auth_id.str, params.auth_id.str, params.auth_id.size) == 0);
        b32 proto_match   = 1;

        if(has_proto)
        {
          b32 ed25519_match = proto_is_ed25519 && (candidate->type == Auth_Proto_Ed25519);
          b32 fido2_match   = proto_is_fido2 && (candidate->type == Auth_Proto_FIDO2);
          proto_match       = ed25519_match || fido2_match;
        }

        if(user_match && auth_id_match && proto_match && key == 0) { key = candidate; }
      }
    }

    if(key == 0)
    {
      MutexScope(state->mutex)
      {
        auth_record_auth_attempt(state, params.user, params.auth_id, 0, now_us);
      }
      response.error = str8_lit("auth: authentication failed");
      return response;
    }
  }

  String8 expected_proto = str8_zero();
  if(key != 0)
  {
    if(key->type == Auth_Proto_Ed25519)    { expected_proto = str8_lit("ed25519"); }
    else if(key->type == Auth_Proto_FIDO2) { expected_proto = str8_lit("fido2"); }
    else
    {
      response.error = str8_lit("auth: unknown key type");
      return response;
    }
  }

  Auth_Conv *conv = 0;
  MutexScope(state->mutex)
  {
    if(state->conv_count >= AUTH_MAX_CONVERSATIONS)
    {
      if(state->conv_first != 0)
      {
        Auth_Conv *evict = state->conv_first;
        SecureMemoryZero(evict->challenge, sizeof(evict->challenge));
        SecureMemoryZero(evict->signature, sizeof(evict->signature));
        SecureMemoryZero(evict->auth_data, sizeof(evict->auth_data));
        SLLQueuePop(state->conv_first, state->conv_last);
        state->conv_count -= 1;
      }
    }

    conv       = auth_conv_alloc(state->arena, state->next_conv_id, params.user, params.auth_id);
    conv->role = str8_copy(state->arena, params.role);
    conv->key  = key;
    if(expected_proto.size > 0) { conv->proto = str8_copy(state->arena, expected_proto); }
    state->next_conv_id += 1;
    SLLQueuePush(state->conv_first, state->conv_last, conv);
    state->conv_count += 1;
  }

  if(is_server)
  {
    b32 challenge_ok = 0;
    if(key != 0 && key->type == Auth_Proto_Ed25519)    { challenge_ok = auth_ed25519_generate_challenge(conv->challenge); }
    else if(key != 0 && key->type == Auth_Proto_FIDO2) { challenge_ok = auth_fido2_generate_challenge(conv->challenge); }
    else                                               { challenge_ok = auth_ed25519_generate_challenge(conv->challenge); }

    if(!challenge_ok)
    {
      response.error = str8_lit("auth: failed to generate challenge");
      conv->state    = Auth_State_Error;
      conv->error    = response.error;
      return response;
    }
    conv->state = Auth_State_ChallengeReady;
  }
  else { conv->state = Auth_State_Started; }

  *out_conv        = conv;
  response.success = 1;
  return response;
}

internal Auth_RPC_Response
auth_rpc_handle_read(Arena *arena, Auth_Conv *conv)
{
  Auth_RPC_Response response = {0};
  if(conv == 0)
  {
    response.error = str8_lit("auth: no active conversation");
    return response;
  }

  u64 now_us     = os_now_microseconds();
  u64 timeout_us = AUTH_RPC_CONVERSATION_TIMEOUT_US;
  u64 elapsed_us = now_us - conv->start_time_us;
  if(elapsed_us > timeout_us)
  {
    response.error = str8_lit("auth: conversation expired");
    return response;
  }

  switch(conv->state)
  {
    case Auth_State_ChallengeReady:
    {
      response.success = 1;
      response.data    = str8(conv->challenge, 32);
      conv->state      = Auth_State_ChallengeSent;
    }break;

    case Auth_State_Done:
    {
      if(str8_match(conv->role, str8_lit("client"), 0) && conv->signature_len > 0)
      {
        // Ed25519: [protocol:8][public_key:32][signature:64]
        // FIDO2: [protocol:8][auth_data_len:8][auth_data][signature]
        u64 proto = 0;
        if(str8_match(conv->proto, str8_lit("ed25519"), 0))    { proto = Auth_Proto_Ed25519; }
        else if(str8_match(conv->proto, str8_lit("fido2"), 0)) { proto = Auth_Proto_FIDO2; }

        u64 total_len = 8 + conv->signature_len;
        if(proto == Auth_Proto_Ed25519) { total_len += 32; }
        if(conv->auth_data_len > 0)     { total_len += 8 + conv->auth_data_len; }

        u8 *buffer    = push_array(arena, u8, total_len);
        u8 *write_ptr = buffer;

        write_u64(write_ptr, proto);
        write_ptr += 8;

        if(proto == Auth_Proto_Ed25519)
        {
          MemoryCopy(write_ptr, conv->key->ed25519_public_key, 32);
          write_ptr += 32;
        }
        if(conv->auth_data_len > 0)
        {
          write_u64(write_ptr, conv->auth_data_len);
          write_ptr += 8;
          MemoryCopy(write_ptr, conv->auth_data, conv->auth_data_len);
          write_ptr += conv->auth_data_len;
        }

        MemoryCopy(write_ptr, conv->signature, conv->signature_len);

        response.success = 1;
        response.data    = str8(buffer, total_len);
      }
      else
      {
        response.success = 1;
        response.data    = str8_lit("done");
      }
    }break;

    case Auth_State_Error: { response.error = conv->error; }break;
    default:               { response.error = str8_lit("auth: invalid state for read"); }break;
  }

  return response;
}

internal Auth_RPC_Response
auth_rpc_handle_write(Arena *arena, Auth_RPC_State *state, Auth_Conv *conv, String8 data)
{
  Auth_RPC_Response response = {0};
  if(conv == 0)
  {
    response.error = str8_lit("auth: no active conversation");
    return response;
  }

  u64 now_us     = os_now_microseconds();
  u64 timeout_us = AUTH_RPC_CONVERSATION_TIMEOUT_US;
  u64 elapsed_us = now_us - conv->start_time_us;
  if(elapsed_us > timeout_us)
  {
    response.error = str8_lit("auth: conversation expired");
    return response;
  }

  switch(conv->state)
  {
    case Auth_State_Started:
    {
    if(data.size != 32)
    {
      response.error = str8_lit("auth: invalid challenge format");
      return response;
    }

    MemoryCopy(conv->challenge, data.str, 32);

    Auth_Key *key = conv->key;
    String8 error = str8_zero();

    if(str8_match(conv->proto, str8_lit("ed25519"), 0))
    {
      Auth_Ed25519_SignParams sign_params = {0};
      MemoryCopy(sign_params.challenge, conv->challenge, 32);
      MemoryCopy(sign_params.private_key, key->ed25519_private_key, 32);

      if(!auth_ed25519_sign_challenge(&sign_params, conv->signature, &error))
      {
        response.error = error;
        conv->state    = Auth_State_Error;
        conv->error    = error;
        return response;
      }

      conv->signature_len = 64;
      conv->auth_data_len = 0;
    }
    else if(str8_match(conv->proto, str8_lit("fido2"), 0))
    {
      Auth_Fido2_AssertParams assert_params = {0};
      assert_params.rp_id                   = key->auth_id;
      assert_params.credential_id           = key->credential_id;
      assert_params.credential_id_len       = key->credential_id_len;
      MemoryCopy(assert_params.challenge, conv->challenge, 32);

      Auth_Fido2_Assertion assertion = {0};
      if(!auth_fido2_get_assertion(arena, &assert_params, &assertion, &error))
      {
        response.error = error;
        conv->state    = Auth_State_Error;
        conv->error    = error;
        return response;
      }

      MemoryCopy(conv->auth_data, assertion.auth_data, assertion.auth_data_len);
      MemoryCopy(conv->signature, assertion.signature, assertion.signature_len);
      conv->auth_data_len = assertion.auth_data_len;
      conv->signature_len = assertion.signature_len;
    }
    else
    {
      response.error = str8_lit("auth: unknown protocol");
      conv->state    = Auth_State_Error;
      conv->error    = response.error;
      return response;
    }

    conv->state    = Auth_State_Done;
    conv->verified = 1;

    response.success = 1;
    }break;

    case Auth_State_ChallengeSent:
    {
    if(data.size < 8)
    {
      response.error = str8_lit("auth: invalid data format: too small");
      return response;
    }

    u64 wire_proto  = read_u64(data.str);
    u8 *payload     = data.str + 8;
    u64 payload_len = data.size - 8;

    if(wire_proto != Auth_Proto_Ed25519 && wire_proto != Auth_Proto_FIDO2)
    {
      response.error = str8_lit("auth: invalid protocol in wire format");
      return response;
    }

    Auth_Key *key = conv->key;
    if(key == 0)
    {
      if(wire_proto == Auth_Proto_Ed25519 && payload_len < 32)
      {
        response.error = str8_lit("auth: ed25519 payload too small for public key");
        return response;
      }

      u8 *client_pubkey = (wire_proto == Auth_Proto_Ed25519 && payload_len >= 32) ? payload : 0;

      for(u64 i = 0; i < state->keyring->count; i += 1)
      {
        Auth_Key *candidate = &state->keyring->keys[i];
        b32 user_match      = str8_match(candidate->user, conv->user, 0);
        b32 auth_id_match   = str8_match(candidate->auth_id, conv->auth_id, 0);
        b32 proto_match     = (candidate->type == wire_proto);
        b32 pubkey_match    = 1;

        if(wire_proto == Auth_Proto_Ed25519 && client_pubkey != 0)
        {
          pubkey_match = (CRYPTO_memcmp(client_pubkey, candidate->ed25519_public_key, 32) == 0);
        }

        if(user_match && auth_id_match && proto_match && pubkey_match && key == 0)
        {
          key       = candidate;
          conv->key = key;
        }
      }

      if(key == 0)
      {
        response.error = str8_lit("auth: authentication failed");
        conv->state    = Auth_State_Error;
        conv->error    = response.error;
        return response;
      }
    }
    else if(key->type != wire_proto)
    {
      response.error = str8_lit("auth: signature protocol does not match credential");
      conv->state    = Auth_State_Error;
      conv->error    = response.error;
      return response;
    }

    if(conv->proto.size == 0)
    {
      conv->proto = (wire_proto == Auth_Proto_Ed25519) ? str8_lit("ed25519") : str8_lit("fido2");
    }

    String8 error = str8_zero();

    if(wire_proto == Auth_Proto_Ed25519)
    {
      // Ed25519 format: [protocol:8][public_key:32][signature:64]
      if(payload_len != 96)
      {
        response.error = str8_lit("auth: invalid Ed25519 signature");
        return response;
      }

      MemoryCopy(conv->signature, payload + 32, 64);
      conv->signature_len = 64;
      conv->auth_data_len = 0;

      Auth_Ed25519_VerifyParams verify_params = {0};
      MemoryCopy(verify_params.challenge, conv->challenge, 32);
      MemoryCopy(verify_params.signature, conv->signature, 64);
      MemoryCopy(verify_params.public_key, key->ed25519_public_key, 32);

      if(!auth_ed25519_verify_signature(&verify_params, &error))
      {
        auth_record_auth_attempt(state, conv->user, conv->auth_id, 0, os_now_microseconds());
        response.error = error;
        conv->state    = Auth_State_Error;
        conv->error    = error;
        return response;
      }
    }
    else if(wire_proto == Auth_Proto_FIDO2)
    {
      // FIDO2 format: [protocol:8][auth_data_len:8][auth_data][signature]
      if(payload_len < 8)
      {
        response.error = str8_lit("auth: invalid FIDO2 format");
        return response;
      }

      u64 auth_data_len = read_u64(payload);

      if(auth_data_len > AUTH_MAX_CREDENTIAL_SIZE || payload_len < 8 || auth_data_len > payload_len - 8)
      {
        response.error = str8_lit("auth: invalid auth_data length");
        return response;
      }

      if(payload_len < 8 + auth_data_len)
      {
        response.error = str8_lit("auth: payload too small");
        return response;
      }

      u64 signature_len = payload_len - 8 - auth_data_len;
      if(signature_len > AUTH_SIGNATURE_SIZE_MAX)
      {
        response.error = str8_lit("auth: signature too large");
        return response;
      }

      MemoryCopy(conv->auth_data, payload + 8, auth_data_len);
      conv->auth_data_len = auth_data_len;
      MemoryCopy(conv->signature, payload + 8 + auth_data_len, signature_len);
      conv->signature_len = signature_len;

      Auth_Fido2_VerifyParams verify_params = {0};
      verify_params.rp_id                   = key->auth_id;
      verify_params.auth_data               = conv->auth_data;
      verify_params.auth_data_len           = conv->auth_data_len;
      verify_params.signature               = conv->signature;
      verify_params.signature_len           = conv->signature_len;
      verify_params.public_key              = key->public_key;
      verify_params.public_key_len          = key->public_key_len;
      MemoryCopy(verify_params.challenge, conv->challenge, 32);

      if(!auth_fido2_verify_signature(arena, &verify_params, &error))
      {
        auth_record_auth_attempt(state, conv->user, conv->auth_id, 0, os_now_microseconds());
        response.error = error;
        conv->state    = Auth_State_Error;
        conv->error    = error;
        return response;
      }
    }

    auth_record_auth_attempt(state, conv->user, conv->auth_id, 1, os_now_microseconds());
    conv->state    = Auth_State_Done;
    conv->verified = 1;

    response.success = 1;
    }break;

    default: { response.error = str8_lit("auth: invalid state for write"); }break;
  }

  return response;
}

internal Auth_RPC_Response
auth_rpc_execute(Arena *arena, Auth_RPC_State *state, Auth_Conv *conv, Auth_RPC_Request request)
{
  Auth_RPC_Response response = {0};

  switch(request.command)
  {
    case Auth_RPC_Command_Start:
    {
      Auth_Conv *new_conv = 0;
      response = auth_rpc_handle_start(state, &new_conv, request.start);
    }break;
    case Auth_RPC_Command_Read:  { response = auth_rpc_handle_read(arena, conv); }break;
    case Auth_RPC_Command_Write: { response = auth_rpc_handle_write(arena, state, conv, request.write_data); }break;
    default:                     { response.error = str8_lit("auth: unknown command"); }break;
  }

  return response;
}
