////////////////////////////////
//~ Hex Encoding/Decoding

internal String8
hex_from_bytes(Arena *arena, u8 *bytes, u64 len)
{
  String8 result = str8_zero();
  result.size = len * 2;
  result.str = push_array(arena, u8, result.size);
  for(u64 i = 0; i < len; i += 1)
  {
    result.str[i * 2 + 0] = integer_symbols[(bytes[i] >> 4) & 0xF];
    result.str[i * 2 + 1] = integer_symbols[(bytes[i] >> 0) & 0xF];
  }
  return result;
}

internal String8
bytes_from_hex(Arena *arena, String8 hex)
{
  String8 result = str8_zero();
  result.size = hex.size / 2;
  result.str = push_array(arena, u8, result.size);
  for(u64 i = 0; i < result.size; i += 1)
  {
    u8 hi = hex.str[i * 2 + 0];
    u8 lo = hex.str[i * 2 + 1];
    if(hi >= 128 || lo >= 128)
    {
      return str8_zero();
    }
    u8 hi_val = integer_symbol_reverse[hi];
    u8 lo_val = integer_symbol_reverse[lo];
    if(hi_val == 0xFF || lo_val == 0xFF)
    {
      return str8_zero();
    }
    result.str[i] = (hi_val << 4) | lo_val;
  }
  return result;
}

////////////////////////////////
//~ Conversation Functions

internal Auth_Conv *
auth_conv_alloc(Arena *arena, u64 tag, String8 user, String8 server)
{
  Auth_Conv *conv = push_array(arena, Auth_Conv, 1);
  conv->tag = tag;
  conv->user = str8_copy(arena, user);
  conv->server = str8_copy(arena, server);
  conv->state = Auth_State_None;
  conv->start_time = os_now_microseconds();
  return conv;
}

internal b32
auth_conv_is_expired(Auth_Conv *conv, u64 current_time, u64 timeout_seconds)
{
  u64 elapsed = current_time - conv->start_time;
  return elapsed > (timeout_seconds * 1000000);
}

////////////////////////////////
//~ Key Ring Functions

internal Auth_KeyRing
auth_keyring_alloc(Arena *arena, u64 capacity)
{
  Auth_KeyRing ring = {0};
  ring.arena = arena;
  ring.capacity = (capacity > 0) ? capacity : 16;
  ring.keys = push_array(arena, Auth_Key, ring.capacity);
  return ring;
}

internal b32
auth_keyring_add(Auth_KeyRing *ring, Auth_Key *key, String8 *out_error)
{
  String8 error = str8_zero();
  if(!auth_validate_credential_format(key, &error))
  {
    if(out_error != 0)
    {
      *out_error = error;
    }
    return 0;
  }

  if(ring->count >= ring->capacity)
  {
    u64 new_cap = ring->capacity * 2;
    Auth_Key *new_keys = push_array(ring->arena, Auth_Key, new_cap);
    MemoryCopy(new_keys, ring->keys, ring->count * sizeof(Auth_Key));
    ring->keys = new_keys;
    ring->capacity = new_cap;
  }

  Auth_Key *dst = &ring->keys[ring->count];
  dst->user = str8_copy(ring->arena, key->user);
  dst->rp_id = str8_copy(ring->arena, key->rp_id);
  dst->credential_id_len = key->credential_id_len;
  MemoryCopy(dst->credential_id, key->credential_id, key->credential_id_len);
  dst->public_key_len = key->public_key_len;
  MemoryCopy(dst->public_key, key->public_key, key->public_key_len);
  ring->count += 1;

  return 1;
}

internal Auth_Key *
auth_keyring_lookup(Auth_KeyRing *ring, String8 user, String8 rp_id)
{
  for(u64 i = 0; i < ring->count; i += 1)
  {
    Auth_Key *key = &ring->keys[i];
    if(str8_match(key->user, user, 0) && str8_match(key->rp_id, rp_id, 0))
    {
      return key;
    }
  }
  return 0;
}

internal void
auth_keyring_remove(Auth_KeyRing *ring, String8 user, String8 rp_id)
{
  for(u64 i = 0; i < ring->count; i += 1)
  {
    Auth_Key *key = &ring->keys[i];
    if(str8_match(key->user, user, 0) && str8_match(key->rp_id, rp_id, 0))
    {
      if(i < ring->count - 1)
      {
        MemoryCopy(&ring->keys[i], &ring->keys[i + 1], (ring->count - i - 1) * sizeof(Auth_Key));
      }
      ring->count -= 1;
      return;
    }
  }
}

////////////////////////////////
//~ Key Ring Serialization

internal String8
auth_keyring_save(Arena *arena, Auth_KeyRing *ring)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List list = {0};

  for(u64 i = 0; i < ring->count; i += 1)
  {
    Auth_Key *key = &ring->keys[i];
    String8 cred_hex = hex_from_bytes(scratch.arena, key->credential_id, key->credential_id_len);
    String8 pubkey_hex = hex_from_bytes(scratch.arena, key->public_key, key->public_key_len);
    String8 line = str8f(scratch.arena, "%S %S %S %S\n", key->user, key->rp_id, cred_hex, pubkey_hex);
    str8_list_push(scratch.arena, &list, line);
  }

  String8 result = str8_list_join(arena, list, 0);
  scratch_end(scratch);
  return result;
}

internal b32
auth_keyring_load(Arena *arena, Auth_KeyRing *ring, String8 data)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List lines = str8_split(scratch.arena, data, (u8 *)"\n", 1, 0);

  for(String8Node *n = lines.first; n != 0; n = n->next)
  {
    String8 line = str8_skip_chop_whitespace(n->string);
    if(line.size == 0)
    {
      continue;
    }

    String8List parts = str8_split(scratch.arena, line, (u8 *)" ", 1, 0);
    if(parts.node_count != 4)
    {
      scratch_end(scratch);
      return 0;
    }

    String8Node *user_node = parts.first;
    String8Node *rp_node = user_node->next;
    String8Node *cred_node = rp_node->next;
    String8Node *pub_node = cred_node->next;

    String8 cred_bytes = bytes_from_hex(scratch.arena, cred_node->string);
    String8 pub_bytes = bytes_from_hex(scratch.arena, pub_node->string);

    if(cred_bytes.size == 0 || pub_bytes.size == 0 || cred_bytes.size > 256 || pub_bytes.size > 256)
    {
      scratch_end(scratch);
      return 0;
    }

    Auth_Key key = {0};
    key.user = str8_copy(arena, user_node->string);
    key.rp_id = str8_copy(arena, rp_node->string);
    key.credential_id_len = cred_bytes.size;
    MemoryCopy(key.credential_id, cred_bytes.str, cred_bytes.size);
    key.public_key_len = pub_bytes.size;
    MemoryCopy(key.public_key, pub_bytes.str, pub_bytes.size);

    if(!auth_keyring_add(ring, &key, 0))
    {
      scratch_end(scratch);
      return 0;
    }
  }

  scratch_end(scratch);
  return 1;
}

////////////////////////////////
//~ Security Validation

internal b32
auth_validate_identifier(String8 str, String8 name, String8 *out_error)
{
  if(str.size == 0)
  {
    *out_error = str8_lit("identifier cannot be empty");
    return 0;
  }

  if(str.size > 256)
  {
    *out_error = str8_lit("identifier too long (max 256 chars)");
    return 0;
  }

  for(u64 i = 0; i < str.size; i += 1)
  {
    u8 c = str.str[i];
    if(c < 0x20 || c == 0x7F)
    {
      *out_error = str8_lit("identifier contains invalid characters");
      return 0;
    }
  }

  return 1;
}

internal b32
auth_validate_credential_format(Auth_Key *key, String8 *out_error)
{
  if(!auth_validate_identifier(key->user, str8_lit("user"), out_error))
  {
    return 0;
  }
  if(!auth_validate_identifier(key->rp_id, str8_lit("rp_id"), out_error))
  {
    return 0;
  }

  if(key->credential_id_len < 16)
  {
    *out_error = str8_lit("credential_id too short (min 16 bytes)");
    return 0;
  }
  if(key->credential_id_len > 256)
  {
    *out_error = str8_lit("credential_id too long (max 256 bytes)");
    return 0;
  }

  if(key->public_key_len < 32)
  {
    *out_error = str8_lit("public_key too short (min 32 bytes)");
    return 0;
  }
  if(key->public_key_len > 256)
  {
    *out_error = str8_lit("public_key too long (max 256 bytes)");
    return 0;
  }

  return 1;
}
