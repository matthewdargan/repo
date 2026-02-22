////////////////////////////////
//~ RPC Functions

internal Auth_RPC_State *
auth_rpc_state_alloc(Arena *arena, Auth_KeyRing *keyring, String8 keys_path)
{
  Auth_RPC_State *state = push_array(arena, Auth_RPC_State, 1);
  state->arena          = arena;
  state->keyring        = keyring;
  state->keys_path      = str8_copy(arena, keys_path);
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

  if(params.user.size == 0)
  {
    response.error = str8_lit("auth: user required");
    return response;
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

  Temp scratch = scratch_begin(&state->arena, 1);
  String8 data = os_data_from_file_path(scratch.arena, state->keys_path);
  if(data.size > 0)
  {
    state->keyring->count = 0;
    auth_keyring_load(state->arena, state->keyring, data);
  }
  scratch_end(scratch);

  b32 is_server = str8_match(params.role, str8_lit("server"), 0);
  b32 is_client = str8_match(params.role, str8_lit("client"), 0);

  Auth_Key *key = 0;
  if(has_proto || is_client)
  {
    for(u64 i = 0; i < state->keyring->count; i += 1)
    {
      Auth_Key *candidate = &state->keyring->keys[i];
      if(!str8_match(candidate->user, params.user, 0) || !str8_match(candidate->auth_id, params.auth_id, 0)) { continue; }

      if(has_proto)
      {
        if(proto_is_ed25519 && candidate->type == Auth_Proto_Ed25519)  { key = candidate; break; }
        else if(proto_is_fido2 && candidate->type == Auth_Proto_FIDO2) { key = candidate; break; }
      }
      else                                                             { key = candidate; break; }
    }

    if(key == 0)
    {
      response.error = str8_lit("auth: no credential found");
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
    conv       = auth_conv_alloc(state->arena, state->next_conv_id, params.user, params.auth_id);
    conv->role = str8_copy(state->arena, params.role);
    conv->key  = key;
    if(expected_proto.size > 0) { conv->proto = str8_copy(state->arena, expected_proto); }
    state->next_conv_id += 1;
    SLLQueuePush(state->conv_first, state->conv_last, conv);
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
    conv->state      = Auth_State_ChallengeReady;
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

  u32 now = os_now_unix();
  if(now - conv->start_time > 10)
  {
    response.error = str8_lit("auth: conversation expired");
    return response;
  }

  switch(conv->state)
  {
    case Auth_State_ChallengeReady:
    {
      u8 *buffer       = push_array(arena, u8, 36);
      write_u32(buffer, conv->start_time);
      MemoryCopy(buffer + 4, conv->challenge, 32);
      response.success = 1;
      response.data    = str8(buffer, 36);
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

  u32 now = os_now_unix();
  if(now - conv->start_time > 10)
  {
    response.error = str8_lit("auth: conversation expired");
    return response;
  }

  switch(conv->state)
  {
    case Auth_State_Started:
    {
    if(data.size != 36)
    {
      response.error = str8_lit("auth: invalid challenge format");
      return response;
    }

    u32 challenge_timestamp = read_u32(data.str);
    u32 now_check           = os_now_unix();
    if(now_check - challenge_timestamp > 10)
    {
      response.error = str8_lit("auth: challenge expired");
      return response;
    }

    MemoryCopy(conv->challenge, data.str + 4, 32);

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
      MemoryCopy(assert_params.challenge, conv->challenge, 32);
      assert_params.credential_id           = key->credential_id;
      assert_params.credential_id_len       = key->credential_id_len;

      Auth_Fido2_Assertion assertion = {0};
      if(!auth_fido2_get_assertion(arena, &assert_params, &assertion, &error))
      {
        response.error = error;
        conv->state    = Auth_State_Error;
        conv->error    = error;
        return response;
      }

      MemoryCopy(conv->auth_data, assertion.auth_data, assertion.auth_data_len);
      conv->auth_data_len = assertion.auth_data_len;
      MemoryCopy(conv->signature, assertion.signature, assertion.signature_len);
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
      for(u64 i = 0; i < state->keyring->count; i += 1)
      {
        Auth_Key *candidate = &state->keyring->keys[i];
        if(!str8_match(candidate->user, conv->user, 0) || !str8_match(candidate->auth_id, conv->auth_id, 0)) { continue; }

        if(candidate->type == wire_proto)
        {
          if(wire_proto == Auth_Proto_Ed25519)
          {
            if(payload_len < 32)
            {
              response.error = str8_lit("auth: ed25519 payload too small for public key");
              return response;
            }
            u8 *client_pubkey = payload;
            if(MemoryCompare(client_pubkey, candidate->ed25519_public_key, 32) != 0) { continue; }
          }

          key       = candidate;
          conv->key = key;
          break;
        }
      }

      if(key == 0)
      {
        response.error = str8_lit("auth: no credential found for protocol");
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
      if(auth_data_len > 256 || auth_data_len + 8 > payload_len)
      {
        response.error = str8_lit("auth: invalid auth_data length");
        return response;
      }

      u64 signature_len = payload_len - 8 - auth_data_len;
      if(signature_len > 256)
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
      MemoryCopy(verify_params.challenge, conv->challenge, 32);
      verify_params.auth_data               = conv->auth_data;
      verify_params.auth_data_len           = conv->auth_data_len;
      verify_params.signature               = conv->signature;
      verify_params.signature_len           = conv->signature_len;
      verify_params.public_key              = key->public_key;
      verify_params.public_key_len          = key->public_key_len;

      if(!auth_fido2_verify_signature(arena, &verify_params, &error))
      {
        response.error = error;
        conv->state    = Auth_State_Error;
        conv->error    = error;
        return response;
      }
    }

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
    case Auth_RPC_Command_Read:  { response       = auth_rpc_handle_read(arena, conv); }break;
    case Auth_RPC_Command_Write: { response       = auth_rpc_handle_write(arena, state, conv, request.write_data); }break;
    default:                     { response.error = str8_lit("auth: unknown command"); }break;
  }

  return response;
}
