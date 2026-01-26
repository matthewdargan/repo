////////////////////////////////
//~ RPC Functions

internal Auth_RPC_State *
auth_rpc_state_alloc(Arena *arena, Auth_KeyRing *keyring)
{
  Auth_RPC_State *state = push_array(arena, Auth_RPC_State, 1);
  state->arena = arena;
  state->keyring = keyring;
  state->mutex = mutex_alloc();
  state->next_conv_id = 1;
  return state;
}

internal Auth_RPC_Request
auth_rpc_parse(Arena *arena, String8 command_line)
{
  Auth_RPC_Request request = {0};
  String8 trimmed = str8_skip_chop_whitespace(command_line);
  if(trimmed.size == 0)
  {
    return request;
  }

  String8List parts = str8_split(arena, trimmed, (u8 *)" ", 1, 0);
  if(parts.node_count == 0)
  {
    return request;
  }

  String8 command = parts.first->string;

  if(str8_match(command, str8_lit("start"), 0))
  {
    request.command = Auth_RPC_Command_Start;
    for(String8Node *node = parts.first->next; node != 0; node = node->next)
    {
      String8 param = node->string;
      String8List kv = str8_split(arena, param, (u8 *)"=", 1, 0);
      if(kv.node_count != 2)
      {
        continue;
      }

      String8 key = kv.first->string;
      String8 value = kv.first->next->string;

      if(str8_match(key, str8_lit("proto"), 0))
      {
        request.start.proto = value;
      }
      else if(str8_match(key, str8_lit("role"), 0))
      {
        request.start.role = value;
      }
      else if(str8_match(key, str8_lit("user"), 0))
      {
        request.start.user = value;
      }
      else if(str8_match(key, str8_lit("server"), 0))
      {
        request.start.server = value;
      }
    }
  }
  else if(str8_match(command, str8_lit("read"), 0))
  {
    request.command = Auth_RPC_Command_Read;
  }
  else
  {
    request.command = Auth_RPC_Command_Write;
  }

  return request;
}

internal Auth_RPC_Response
auth_rpc_handle_start(Auth_RPC_State *state, Auth_Conv **out_conv, Auth_RPC_StartParams params)
{
  Auth_RPC_Response response = {0};

  if(!str8_match(params.proto, str8_lit("fido2"), 0))
  {
    response.error = str8_lit("unsupported protocol");
    return response;
  }
  if(!str8_match(params.role, str8_lit("client"), 0) && !str8_match(params.role, str8_lit("server"), 0))
  {
    response.error = str8_lit("invalid role");
    return response;
  }
  if(params.user.size == 0)
  {
    response.error = str8_lit("user required");
    return response;
  }

  Auth_Conv *conv = 0;
  MutexScope(state->mutex)
  {
    conv = auth_conv_alloc(state->arena, state->next_conv_id, params.user, params.server);
    conv->role = str8_copy(state->arena, params.role);
    state->next_conv_id += 1;
    SLLQueuePush(state->conv_first, state->conv_last, conv);
  }

  if(str8_match(params.role, str8_lit("server"), 0))
  {
    Auth_Key *key = auth_keyring_lookup(state->keyring, params.user, params.server);
    if(key == 0)
    {
      response.error = str8_lit("no credential found");
      conv->state = Auth_State_Error;
      conv->error = response.error;
      return response;
    }
    if(!auth_fido2_generate_challenge(conv->challenge))
    {
      response.error = str8_lit("failed to generate challenge");
      conv->state = Auth_State_Error;
      conv->error = response.error;
      return response;
    }
    conv->state = Auth_State_ChallengeReady;
  }
  else
  {
    conv->state = Auth_State_Started;
  }

  *out_conv = conv;
  response.success = 1;
  return response;
}

internal Auth_RPC_Response
auth_rpc_handle_read(Arena *arena, Auth_Conv *conv)
{
  Auth_RPC_Response response = {0};

  if(conv == 0)
  {
    response.error = str8_lit("no active conversation");
    return response;
  }

  switch(conv->state)
  {
  case Auth_State_ChallengeReady:
  {
    response.success = 1;
    response.data = str8(conv->challenge, 32);
    conv->state = Auth_State_ChallengeSent;
  }
  break;

  case Auth_State_Done:
  {
    if(str8_match(conv->role, str8_lit("client"), 0) && conv->auth_data_len > 0 && conv->signature_len > 0)
    {
      // [auth_data_len:4][auth_data][signature]
      u64 total_len = 4 + conv->auth_data_len + conv->signature_len;
      u8 *buffer = push_array(arena, u8, total_len);
      write_u32(buffer, (u32)conv->auth_data_len);
      MemoryCopy(buffer + 4, conv->auth_data, conv->auth_data_len);
      MemoryCopy(buffer + 4 + conv->auth_data_len, conv->signature, conv->signature_len);
      response.success = 1;
      response.data = str8(buffer, total_len);
    }
    else
    {
      response.success = 1;
      response.data = str8_lit("done");
    }
  }
  break;

  case Auth_State_Error:
  {
    response.error = conv->error;
  }
  break;

  default:
  {
    response.error = str8_lit("invalid state for read");
  }
  break;
  }

  return response;
}

internal Auth_RPC_Response
auth_rpc_handle_write(Arena *arena, Auth_RPC_State *state, Auth_Conv *conv, String8 data)
{
  Auth_RPC_Response response = {0};

  if(conv == 0)
  {
    response.error = str8_lit("no active conversation");
    return response;
  }

  switch(conv->state)
  {
  case Auth_State_Started:
  {
    if(data.size != 32)
    {
      response.error = str8_lit("invalid challenge length");
      return response;
    }

    MemoryCopy(conv->challenge, data.str, 32);

    Auth_Key *key = auth_keyring_lookup(state->keyring, conv->user, conv->server);
    if(key == 0)
    {
      response.error = str8_lit("no credential found");
      conv->state = Auth_State_Error;
      conv->error = response.error;
      return response;
    }

    Auth_Fido2_AssertParams assert_params = {0};
    assert_params.rp_id = key->rp_id;
    MemoryCopy(assert_params.challenge, conv->challenge, 32);
    assert_params.credential_id = key->credential_id;
    assert_params.credential_id_len = key->credential_id_len;

    Auth_Fido2_Assertion assertion = {0};
    String8 error = str8_zero();
    if(!auth_fido2_get_assertion(arena, &assert_params, &assertion, &error))
    {
      response.error = error;
      conv->state = Auth_State_Error;
      conv->error = error;
      return response;
    }

    MemoryCopy(conv->auth_data, assertion.auth_data, assertion.auth_data_len);
    conv->auth_data_len = assertion.auth_data_len;
    MemoryCopy(conv->signature, assertion.signature, assertion.signature_len);
    conv->signature_len = assertion.signature_len;
    conv->state = Auth_State_Done;
    conv->verified = 1;

    response.success = 1;
  }
  break;

  case Auth_State_ChallengeSent:
  {
    // [auth_data_len:4][auth_data][signature]
    if(data.size < 4)
    {
      response.error = str8_lit("invalid data format");
      return response;
    }

    u32 auth_data_len = read_u32(data.str);
    if(auth_data_len > 256 || auth_data_len + 4 > data.size)
    {
      response.error = str8_lit("invalid auth_data length");
      return response;
    }

    u64 signature_len = data.size - 4 - auth_data_len;
    if(signature_len > 256)
    {
      response.error = str8_lit("signature too large");
      return response;
    }

    MemoryCopy(conv->auth_data, data.str + 4, auth_data_len);
    conv->auth_data_len = auth_data_len;
    MemoryCopy(conv->signature, data.str + 4 + auth_data_len, signature_len);
    conv->signature_len = signature_len;

    Auth_Key *key = auth_keyring_lookup(state->keyring, conv->user, conv->server);
    if(key == 0)
    {
      response.error = str8_lit("no credential found");
      conv->state = Auth_State_Error;
      conv->error = response.error;
      return response;
    }

    Auth_Fido2_VerifyParams verify_params = {0};
    verify_params.rp_id = key->rp_id;
    MemoryCopy(verify_params.challenge, conv->challenge, 32);
    verify_params.auth_data = conv->auth_data;
    verify_params.auth_data_len = conv->auth_data_len;
    verify_params.signature = conv->signature;
    verify_params.signature_len = conv->signature_len;
    verify_params.public_key = key->public_key;
    verify_params.public_key_len = key->public_key_len;

    String8 error = str8_zero();
    if(!auth_fido2_verify_signature(arena, &verify_params, &error))
    {
      response.error = error;
      conv->state = Auth_State_Error;
      conv->error = error;
      return response;
    }

    conv->state = Auth_State_Done;
    conv->verified = 1;

    response.success = 1;
  }
  break;

  default:
  {
    response.error = str8_lit("invalid state for write");
  }
  break;
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
  }
  break;

  case Auth_RPC_Command_Read:
  {
    response = auth_rpc_handle_read(arena, conv);
  }
  break;

  case Auth_RPC_Command_Write:
  {
    response = auth_rpc_handle_write(arena, state, conv, request.write_data);
  }
  break;

  default:
  {
    response.error = str8_lit("unknown command");
  }
  break;
  }

  return response;
}
