////////////////////////////////
//~ Ed25519 Operations

internal b32
auth_ed25519_generate_challenge(u8 challenge[32])
{
  if(getentropy(challenge, 32) != 0)
  {
    return 0;
  }
  return 1;
}

internal b32
auth_ed25519_register_credential(Arena *arena, Auth_Ed25519_RegisterParams params, Auth_Key *out_key, String8 *out_error)
{
  EVP_PKEY_CTX *ctx = 0;
  EVP_PKEY *pkey = 0;
  b32 success = 0;

  ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, 0);
  if(ctx == 0)
  {
    *out_error = str8_lit("ed25519: failed to create key generation context");
  }
  else if(EVP_PKEY_keygen_init(ctx) <= 0)
  {
    *out_error = str8_lit("ed25519: failed to initialize key generation");
  }
  else if(EVP_PKEY_keygen(ctx, &pkey) <= 0)
  {
    *out_error = str8_lit("ed25519: failed to generate keypair");
  }
  else
  {
    u8 public_key[32];
    u8 private_key[32];

    size_t public_key_len = 32;
    if(EVP_PKEY_get_raw_public_key(pkey, public_key, &public_key_len) <= 0 || public_key_len != 32)
    {
      *out_error = str8_lit("ed25519: failed to extract public key");
    }
    else
    {
      size_t private_key_len = 32;
      if(EVP_PKEY_get_raw_private_key(pkey, private_key, &private_key_len) <= 0 || private_key_len != 32)
      {
        *out_error = str8_lit("ed25519: failed to extract private key");
      }
      else
      {
        out_key->type = Auth_Proto_Ed25519;
        out_key->user = str8_copy(arena, params.user);
        out_key->auth_id = str8_copy(arena, params.auth_id);

        MemoryCopy(out_key->ed25519_public_key, public_key, 32);
        MemoryCopy(out_key->ed25519_private_key, private_key, 32);

        success = 1;
      }
    }
  }

  if(pkey != 0)
  {
    EVP_PKEY_free(pkey);
  }
  if(ctx != 0)
  {
    EVP_PKEY_CTX_free(ctx);
  }

  return success;
}

internal b32
auth_ed25519_sign_challenge(Auth_Ed25519_SignParams *params, u8 signature[64],
                            String8 *out_error)
{
  EVP_MD_CTX *mdctx = 0;
  EVP_PKEY *pkey = 0;
  b32 success = 0;

  pkey = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, 0, params->private_key, 32);
  if(pkey == 0)
  {
    *out_error = str8_lit("ed25519: failed to create private key");
  }
  else
  {
    mdctx = EVP_MD_CTX_new();
    if(mdctx == 0)
    {
      *out_error = str8_lit("ed25519: failed to create signing context");
    }
    else if(EVP_DigestSignInit(mdctx, 0, 0, 0, pkey) <= 0)
    {
      *out_error = str8_lit("ed25519: failed to initialize signing");
    }
    else
    {
      size_t signature_len = 64;
      if(EVP_DigestSign(mdctx, signature, &signature_len, params->challenge, 32) <= 0 || signature_len != 64)
      {
        *out_error = str8_lit("ed25519: failed to sign challenge");
      }
      else
      {
        success = 1;
      }
    }
  }

  if(mdctx != 0)
  {
    EVP_MD_CTX_free(mdctx);
  }
  if(pkey != 0)
  {
    EVP_PKEY_free(pkey);
  }

  return success;
}

internal b32
auth_ed25519_verify_signature(Auth_Ed25519_VerifyParams *params, String8 *out_error)
{
  EVP_MD_CTX *mdctx = 0;
  EVP_PKEY *pkey = 0;
  b32 success = 0;

  pkey = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, 0, params->public_key, 32);
  if(pkey == 0)
  {
    *out_error = str8_lit("ed25519: failed to create public key");
  }
  else
  {
    mdctx = EVP_MD_CTX_new();
    if(mdctx == 0)
    {
      *out_error = str8_lit("ed25519: failed to create verification context");
    }
    else if(EVP_DigestVerifyInit(mdctx, 0, 0, 0, pkey) <= 0)
    {
      *out_error = str8_lit("ed25519: failed to initialize verification");
    }
    else
    {
      int verify_result = EVP_DigestVerify(mdctx, params->signature, 64, params->challenge, 32);
      if(verify_result <= 0)
      {
        *out_error = str8_lit("ed25519: signature verification failed");
      }
      else
      {
        success = 1;
      }
    }
  }

  if(mdctx != 0)
  {
    EVP_MD_CTX_free(mdctx);
  }
  if(pkey != 0)
  {
    EVP_PKEY_free(pkey);
  }

  return success;
}
