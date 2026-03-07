////////////////////////////////
//~ Hex Encoding/Decoding

internal String8
hex_from_bytes(Arena *arena, u8 *bytes, u64 len)
{
  if(arena == 0 || bytes == 0 || len == 0) { return str8_zero(); }
  if(len > (max_u64 / 2))                  { return str8_zero(); }

  String8 result = str8_zero();
  result.size    = len * 2;
  result.str     = push_array(arena, u8, result.size);

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
  if(arena == 0 || hex.str == 0)         { return str8_zero(); }
  if(hex.size == 0 || (hex.size & 1))    { return str8_zero(); }

  String8 result = str8_zero();
  result.size    = hex.size / 2;
  result.str     = push_array(arena, u8, result.size);

  for(u64 i = 0; i < result.size; i += 1)
  {
    u8 hi = hex.str[i * 2 + 0];
    u8 lo = hex.str[i * 2 + 1];
    if(hi >= 128 || lo >= 128)           { return str8_zero(); }

    u8 hi_val = integer_symbol_reverse[hi];
    u8 lo_val = integer_symbol_reverse[lo];
    if(hi_val == 0xFF || lo_val == 0xFF) { return str8_zero(); }

    result.str[i] = (hi_val << 4) | lo_val;
  }

  return result;
}

////////////////////////////////
//~ Conversation Functions

internal Auth_Conv *
auth_conv_alloc(Arena *arena, u64 tag, String8 user, String8 auth_id)
{
  if(arena == 0) { return 0; }

  Auth_Conv *conv = push_array(arena, Auth_Conv, 1);
  if(conv == 0)  { return 0; }

  MemoryZero(conv, sizeof(Auth_Conv));
  conv->tag           = tag;
  conv->user          = str8_copy(arena, user);
  conv->auth_id       = str8_copy(arena, auth_id);
  conv->state         = Auth_State_None;
  conv->start_time_us = os_now_microseconds();
  return conv;
}

////////////////////////////////
//~ Key Ring Functions

internal Auth_KeyRing
auth_keyring_alloc(Arena *arena, u64 capacity)
{
  Auth_KeyRing ring = {0};
  ring.arena        = arena;
  ring.capacity     = (capacity > 0) ? capacity : 16;
  ring.keys         = push_array(arena, Auth_Key, ring.capacity);
  return ring;
}

internal b32
auth_keyring_add(Auth_KeyRing *ring, Auth_Key *key, String8 *out_error)
{
  if(ring == 0 || key == 0)
  {
    if(out_error != 0) { *out_error = str8_lit("auth: invalid parameters"); }
    return 0;
  }

  String8 error = str8_zero();
  if(!auth_validate_credential_format(key, &error))
  {
    if(out_error != 0) { *out_error = error; }
    return 0;
  }

  if(ring->count >= ring->capacity)
  {
    u64 new_cap        = ring->capacity * 2;
    Auth_Key *new_keys = push_array(ring->arena, Auth_Key, new_cap);
    MemoryCopy(new_keys, ring->keys, ring->count * sizeof(Auth_Key));
    SecureMemoryZero(ring->keys, ring->count * sizeof(Auth_Key));
    ring->keys         = new_keys;
    ring->capacity     = new_cap;
  }

  Auth_Key *dst = &ring->keys[ring->count];
  MemoryZero(dst, sizeof(Auth_Key));
  dst->type     = key->type;
  dst->user     = str8_copy(ring->arena, key->user);
  dst->auth_id  = str8_copy(ring->arena, key->auth_id);

  switch(key->type)
  {
    case Auth_Proto_Ed25519:
    {
      MemoryCopy(dst->ed25519_public_key, key->ed25519_public_key, 32);
      MemoryCopy(dst->ed25519_private_key, key->ed25519_private_key, 32);
    }break;

    case Auth_Proto_FIDO2:
    {
      if(key->credential_id_len > sizeof(dst->credential_id))
      {
        if(out_error != 0) { *out_error = str8_lit("auth: credential_id too large"); }
        return 0;
      }
      if(key->public_key_len > sizeof(dst->public_key))
      {
        if(out_error != 0) { *out_error = str8_lit("auth: public_key too large"); }
        return 0;
      }

      dst->credential_id_len = key->credential_id_len;
      dst->public_key_len    = key->public_key_len;
      MemoryCopy(dst->credential_id, key->credential_id, key->credential_id_len);
      MemoryCopy(dst->public_key, key->public_key, key->public_key_len);
    }break;
  }

  ring->count += 1;
  return 1;
}

internal Auth_Key *
auth_keyring_lookup(Auth_KeyRing *ring, String8 user, String8 auth_id)
{
  if(ring == 0 || ring->keys == 0) { return 0; }

  Auth_Key *found = 0;

  // Constant-time search - always iterate all keys to prevent timing oracle
  for(u64 i = 0; i < ring->count; i += 1)
  {
    Auth_Key *key = &ring->keys[i];

    b32 user_match    = (key->user.size == user.size && CRYPTO_memcmp(key->user.str, user.str, user.size) == 0);
    b32 auth_id_match = (key->auth_id.size == auth_id.size && CRYPTO_memcmp(key->auth_id.str, auth_id.str, auth_id.size) == 0);

    if(user_match && auth_id_match && found == 0) { found = key; }
  }

  return found;
}

internal void
auth_keyring_remove(Auth_KeyRing *ring, String8 user, String8 auth_id, Auth_Proto type)
{
  if(ring == 0 || ring->keys == 0) { return; }

  for(u64 i = 0; i < ring->count; i += 1)
  {
    Auth_Key *key = &ring->keys[i];
    if(key->type == type && str8_match(key->user, user, 0) && str8_match(key->auth_id, auth_id, 0))
    {
      SecureMemoryZero(key, sizeof(Auth_Key));

      if(i < ring->count - 1)
      {
        MemoryCopy(&ring->keys[i], &ring->keys[i + 1], (ring->count - i - 1) * sizeof(Auth_Key));
      }

      ring->count -= 1;
      SecureMemoryZero(&ring->keys[ring->count], sizeof(Auth_Key));
      return;
    }
  }
}

////////////////////////////////
//~ Key Ring Serialization

internal b32
auth_keyring_encrypt(Arena *arena, String8 plaintext, String8 passphrase, String8 *out_encrypted, String8 *out_error)
{
  if(arena == 0 || plaintext.str == 0 || passphrase.str == 0 || out_encrypted == 0 || out_error == 0) { return 0; }
  if(passphrase.size > (u64)max_s32) { *out_error = str8_lit("auth: passphrase too long"); return 0; }
  if(plaintext.size > (max_u64 - 1 - AUTH_SALT_SIZE - AUTH_NONCE_SIZE - AUTH_TAG_SIZE))
  {
    *out_error = str8_lit("auth: plaintext too large");
    return 0;
  }

  b32 success         = 0;
  EVP_CIPHER_CTX *ctx = 0;
  u8 key[AUTH_KEY_SIZE];
  u8 salt[AUTH_SALT_SIZE];
  u8 nonce[AUTH_NONCE_SIZE];

  SecureMemoryZero(key, AUTH_KEY_SIZE);
  SecureMemoryZero(salt, AUTH_SALT_SIZE);
  SecureMemoryZero(nonce, AUTH_NONCE_SIZE);

  if(getentropy(salt, AUTH_SALT_SIZE) != 0)
  {
    *out_error = str8_lit("auth: failed to generate salt");
    goto cleanup;
  }

  if(getentropy(nonce, AUTH_NONCE_SIZE) != 0)
  {
    *out_error = str8_lit("auth: failed to generate nonce");
    goto cleanup;
  }

  // Derive AES-256 key from passphrase using PBKDF2-HMAC-SHA256
  if(PKCS5_PBKDF2_HMAC((char *)passphrase.str, (int)passphrase.size, salt, AUTH_SALT_SIZE,
                        AUTH_KDF_ITERATIONS, EVP_sha256(), AUTH_KEY_SIZE, key) != 1)
  {
    *out_error = str8_lit("auth: key derivation failed");
    goto cleanup;
  }

  u64 encrypted_size = 1 + AUTH_SALT_SIZE + AUTH_NONCE_SIZE + AUTH_TAG_SIZE + plaintext.size;
  u8 *encrypted      = push_array(arena, u8, encrypted_size);

  encrypted[0] = AUTH_ENCRYPTED_VERSION;
  MemoryCopy(encrypted + 1, salt, AUTH_SALT_SIZE);
  MemoryCopy(encrypted + 1 + AUTH_SALT_SIZE, nonce, AUTH_NONCE_SIZE);

  u8 *ciphertext = encrypted + 1 + AUTH_SALT_SIZE + AUTH_NONCE_SIZE + AUTH_TAG_SIZE;
  u8 *tag_ptr    = encrypted + 1 + AUTH_SALT_SIZE + AUTH_NONCE_SIZE;

  ctx = EVP_CIPHER_CTX_new();
  if(ctx == 0)
  {
    *out_error = str8_lit("auth: failed to create cipher context");
    goto cleanup;
  }

  if(EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), 0, key, nonce) != 1)
  {
    *out_error = str8_lit("auth: encryption init failed");
    goto cleanup;
  }

  int len = 0;
  if(plaintext.size > (u64)max_s32)
  {
    *out_error = str8_lit("auth: plaintext too large for encryption");
    goto cleanup;
  }

  if(EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext.str, (int)plaintext.size) != 1)
  {
    *out_error = str8_lit("auth: encryption failed");
    goto cleanup;
  }

  if(EVP_EncryptFinal_ex(ctx, ciphertext + len, &len) != 1)
  {
    *out_error = str8_lit("auth: encryption finalization failed");
    goto cleanup;
  }

  if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, AUTH_TAG_SIZE, tag_ptr) != 1)
  {
    *out_error = str8_lit("auth: failed to get authentication tag");
    goto cleanup;
  }

  *out_encrypted = str8(encrypted, encrypted_size);
  success = 1;

cleanup:
  if(ctx != 0) { EVP_CIPHER_CTX_free(ctx); }
  SecureMemoryZero(key, AUTH_KEY_SIZE);
  SecureMemoryZero(salt, AUTH_SALT_SIZE);
  SecureMemoryZero(nonce, AUTH_NONCE_SIZE);
  return success;
}

internal b32
auth_keyring_decrypt(Arena *arena, String8 encrypted, String8 passphrase, String8 *out_plaintext, String8 *out_error)
{
  if(arena == 0 || encrypted.str == 0 || passphrase.str == 0 || out_plaintext == 0 || out_error == 0) { return 0; }
  if(passphrase.size > (u64)max_s32) { *out_error = str8_lit("auth: passphrase too long"); return 0; }

  b32 success         = 0;
  EVP_CIPHER_CTX *ctx = 0;
  u8 key[AUTH_KEY_SIZE];
  u8 *plaintext       = 0;
  u64 plaintext_len   = 0;

  SecureMemoryZero(key, AUTH_KEY_SIZE);

  u64 min_size = 1 + AUTH_SALT_SIZE + AUTH_NONCE_SIZE + AUTH_TAG_SIZE;
  if(encrypted.size < min_size)
  {
    *out_error = str8_lit("auth: invalid encrypted data");
    goto cleanup;
  }

  u8 version = encrypted.str[0];
  if(version != AUTH_ENCRYPTED_VERSION)
  {
    *out_error = str8_lit("auth: unsupported version");
    goto cleanup;
  }

  u8 *salt           = encrypted.str + 1;
  u8 *nonce          = encrypted.str + 1 + AUTH_SALT_SIZE;
  u8 *tag            = encrypted.str + 1 + AUTH_SALT_SIZE + AUTH_NONCE_SIZE;
  u8 *ciphertext     = encrypted.str + 1 + AUTH_SALT_SIZE + AUTH_NONCE_SIZE + AUTH_TAG_SIZE;
  u64 ciphertext_len = encrypted.size - min_size;

  if(PKCS5_PBKDF2_HMAC((char *)passphrase.str, (int)passphrase.size, salt, AUTH_SALT_SIZE,
                        AUTH_KDF_ITERATIONS, EVP_sha256(), AUTH_KEY_SIZE, key) != 1)
  {
    *out_error = str8_lit("auth: key derivation failed");
    goto cleanup;
  }

  plaintext     = push_array(arena, u8, ciphertext_len);
  plaintext_len = ciphertext_len;

  ctx = EVP_CIPHER_CTX_new();
  if(ctx == 0)
  {
    *out_error = str8_lit("auth: failed to create cipher context");
    goto cleanup;
  }

  if(EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), 0, key, nonce) != 1)
  {
    *out_error = str8_lit("auth: decryption init failed");
    goto cleanup;
  }

  int len = 0;
  if(ciphertext_len > (u64)max_s32)
  {
    *out_error = str8_lit("auth: ciphertext too large");
    goto cleanup;
  }

  if(EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, (int)ciphertext_len) != 1)
  {
    *out_error = str8_lit("auth: decryption failed");
    goto cleanup;
  }

  if(EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, AUTH_TAG_SIZE, tag) != 1)
  {
    *out_error = str8_lit("auth: failed to set authentication tag");
    goto cleanup;
  }

  int final_len = 0;
  if(EVP_DecryptFinal_ex(ctx, plaintext + len, &final_len) != 1)
  {
    *out_error = str8_lit("auth: authentication failed");
    goto cleanup;
  }

  *out_plaintext = str8(plaintext, len + final_len);
  success = 1;

cleanup:
  if(ctx != 0)                   { EVP_CIPHER_CTX_free(ctx); }
  SecureMemoryZero(key, AUTH_KEY_SIZE);
  if(plaintext != 0) { SecureMemoryZero(plaintext, plaintext_len); }
  return success;
}

internal b32
auth_keyring_save(Arena *arena, Auth_KeyRing *ring, String8 passphrase, String8 *out_data, String8 *out_error)
{
  Temp scratch     = scratch_begin(&arena, 1);
  String8List list = {0};

  for(u64 i = 0; i < ring->count; i += 1)
  {
    Auth_Key *key = &ring->keys[i];
    String8 line  = str8_zero();

    switch(key->type)
    {
      case Auth_Proto_Ed25519:
      {
        String8 pubkey_hex  = hex_from_bytes(scratch.arena, key->ed25519_public_key, 32);
        String8 privkey_hex = hex_from_bytes(scratch.arena, key->ed25519_private_key, 32);
        line                = str8f(scratch.arena, "ed25519 %S %S %S %S\n", key->user, key->auth_id, pubkey_hex, privkey_hex);
      }break;

      case Auth_Proto_FIDO2:
      {
        String8 cred_hex   = hex_from_bytes(scratch.arena, key->credential_id, key->credential_id_len);
        String8 pubkey_hex = hex_from_bytes(scratch.arena, key->public_key, key->public_key_len);
        line               = str8f(scratch.arena, "fido2 %S %S %S %S\n", key->user, key->auth_id, cred_hex, pubkey_hex);
      }break;
    }

    if(line.size > 0) { str8_list_push(scratch.arena, &list, line); }
  }

  String8 plaintext = str8_list_join(scratch.arena, list, 0);
  String8 result    = str8_zero();

  if(passphrase.size > 0)
  {
    if(!auth_keyring_encrypt(arena, plaintext, passphrase, &result, out_error))
    {
      SecureMemoryZero((void*)plaintext.str, plaintext.size);
      scratch_end(scratch);
      return 0;
    }
  }
  else { result = str8_copy(arena, plaintext); }

  SecureMemoryZero((void*)plaintext.str, plaintext.size);
  scratch_end(scratch);
  *out_data = result;
  return 1;
}

internal b32
auth_keyring_load(Arena *arena, Auth_KeyRing *ring, String8 data, String8 passphrase)
{
  if(arena == 0 || ring == 0) { return 0; }

  Temp scratch      = scratch_begin(&arena, 1);
  String8 plaintext = str8_zero();

  if(data.size > 0 && data.str != 0 && data.str[0] == AUTH_ENCRYPTED_VERSION)
  {
    String8 error = str8_zero();
    if(!auth_keyring_decrypt(scratch.arena, data, passphrase, &plaintext, &error))
    {
      scratch_end(scratch);
      return 0;
    }
  }
  else { plaintext = data; }

  String8List lines = str8_split(scratch.arena, plaintext, (u8 *)"\n", 1, 0);

  for(String8Node *n = lines.first; n != 0; n = n->next)
  {
    String8 line = str8_skip_chop_whitespace(n->string);
    if(line.size == 0) { continue; }

    String8List parts = str8_split(scratch.arena, line, (u8 *)" ", 1, 0);
    if(parts.node_count != 5)
    {
      SecureMemoryZero((void*)plaintext.str, plaintext.size);
      scratch_end(scratch);
      return 0;
    }

    String8Node *type_node    = parts.first;
    String8Node *user_node    = type_node->next;
    String8Node *auth_id_node = user_node->next;
    String8Node *data1_node   = auth_id_node->next;
    String8Node *data2_node   = data1_node->next;

    Auth_Key key = {0};
    key.user     = str8_copy(arena, user_node->string);
    key.auth_id  = str8_copy(arena, auth_id_node->string);

    if(str8_match(type_node->string, str8_lit("ed25519"), 0))
    {
      key.type = Auth_Proto_Ed25519;

      String8 pub_bytes  = bytes_from_hex(scratch.arena, data1_node->string);
      String8 priv_bytes = bytes_from_hex(scratch.arena, data2_node->string);
      if(pub_bytes.size != 32 || priv_bytes.size != 32)
      {
        SecureMemoryZero((void*)plaintext.str, plaintext.size);
        scratch_end(scratch);
        return 0;
      }

      MemoryCopy(key.ed25519_public_key, pub_bytes.str, 32);
      MemoryCopy(key.ed25519_private_key, priv_bytes.str, 32);
    }
    else if(str8_match(type_node->string, str8_lit("fido2"), 0))
    {
      key.type = Auth_Proto_FIDO2;

      String8 cred_bytes = bytes_from_hex(scratch.arena, data1_node->string);
      String8 pub_bytes  = bytes_from_hex(scratch.arena, data2_node->string);
      if(cred_bytes.size == 0 || pub_bytes.size == 0 || cred_bytes.size > AUTH_MAX_CREDENTIAL_SIZE || pub_bytes.size > AUTH_MAX_CREDENTIAL_SIZE)
      {
        SecureMemoryZero((void*)plaintext.str, plaintext.size);
        scratch_end(scratch);
        return 0;
      }

      key.credential_id_len = cred_bytes.size;
      key.public_key_len    = pub_bytes.size;
      MemoryCopy(key.credential_id, cred_bytes.str, cred_bytes.size);
      MemoryCopy(key.public_key, pub_bytes.str, pub_bytes.size);
    }
    else { continue; }

    if(!auth_keyring_add(ring, &key, 0))
    {
      SecureMemoryZero((void*)plaintext.str, plaintext.size);
      scratch_end(scratch);
      return 0;
    }
  }

  SecureMemoryZero((void*)plaintext.str, plaintext.size);
  scratch_end(scratch);
  return 1;
}

////////////////////////////////
//~ Security Validation

internal b32
auth_validate_identifier(String8 str, String8 *out_error)
{
  if(out_error == 0) { return 0; }

  if(str.size == 0)
  {
    *out_error = str8_lit("auth: identifier cannot be empty");
    return 0;
  }
  if(str.size > AUTH_MAX_IDENTIFIER_SIZE)
  {
    *out_error = str8_lit("auth: identifier too long (max 256 chars)");
    return 0;
  }

  for(u64 i = 0; i < str.size; i += 1)
  {
    u8 c = str.str[i];
    if(c < 0x21 || c == 0x7F)
    {
      *out_error = str8_lit("auth: identifier contains invalid characters");
      return 0;
    }
  }

  return 1;
}

internal b32
auth_validate_credential_format(Auth_Key *key, String8 *out_error)
{
  if(key == 0 || out_error == 0)                         { return 0; }
  if(!auth_validate_identifier(key->user, out_error))    { return 0; }
  if(!auth_validate_identifier(key->auth_id, out_error)) { return 0; }

  switch(key->type)
  {
    case Auth_Proto_Ed25519: break;
    case Auth_Proto_FIDO2:
    {
      if(key->credential_id_len < 16)
      {
        *out_error = str8_lit("auth: credential_id too short (min 16 bytes)");
        return 0;
      }
      if(key->credential_id_len > AUTH_MAX_CREDENTIAL_SIZE)
      {
        *out_error = str8_lit("auth: credential_id too long (max 256 bytes)");
        return 0;
      }

      if(key->public_key_len < 32)
      {
        *out_error = str8_lit("auth: public_key too short (min 32 bytes)");
        return 0;
      }
      if(key->public_key_len > AUTH_MAX_CREDENTIAL_SIZE)
      {
        *out_error = str8_lit("auth: public_key too long (max 256 bytes)");
        return 0;
      }
    }break;

    default:
    {
      *out_error = str8_lit("auth: unknown key type");
      return 0;
    }
  }

  return 1;
}
