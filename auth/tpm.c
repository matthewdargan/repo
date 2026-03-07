////////////////////////////////
//~ TPM Functions

internal b32
auth_tpm_available(void)
{
  if(access("/dev/tpmrm0", R_OK | W_OK) == 0) { return 1; }
  if(access("/dev/tpm0", R_OK | W_OK) == 0)   { return 1; }
  return 0;
}

typedef struct Auth_TPM_Context Auth_TPM_Context;
struct Auth_TPM_Context {
  Arena *arena;
  ESYS_CONTEXT *esys;
};

internal b32
auth_tpm_init(Arena *arena, Auth_TPM_Context *ctx, String8 *out_error)
{
  if(arena == 0 || ctx == 0 || out_error == 0) { return 0; }

  ctx->arena = arena;
  ctx->esys  = 0;
  TSS2_RC rc = Esys_Initialize(&ctx->esys, 0, 0);
  if(rc != TSS2_RC_SUCCESS)
  {
    *out_error = str8_lit("TPM: init failed");
    return 0;
  }
  return 1;
}

internal void
auth_tpm_cleanup(Auth_TPM_Context *ctx)
{
  if(ctx == 0) { return; }
  if(ctx->esys != 0) { Esys_Finalize(&ctx->esys); ctx->esys = 0; }
}

internal b32
auth_tpm_create_primary(Auth_TPM_Context *ctx, ESYS_TR *out_handle, String8 *out_error)
{
  if(ctx == 0 || out_handle == 0 || out_error == 0) { return 0; }

  TPM2B_SENSITIVE_CREATE in_sensitive = {0};
  TPM2B_PUBLIC in_public = {
    .publicArea = {
      .type             = TPM2_ALG_ECC,
      .nameAlg          = TPM2_ALG_SHA256,
      .objectAttributes = (TPMA_OBJECT_RESTRICTED | TPMA_OBJECT_DECRYPT | TPMA_OBJECT_FIXEDTPM |
                           TPMA_OBJECT_FIXEDPARENT | TPMA_OBJECT_SENSITIVEDATAORIGIN | TPMA_OBJECT_USERWITHAUTH),
      .parameters.eccDetail = {
        .symmetric      = {.algorithm = TPM2_ALG_AES, .keyBits.aes = 128, .mode.aes = TPM2_ALG_CFB},
        .scheme.scheme  = TPM2_ALG_NULL,
        .curveID        = TPM2_ECC_NIST_P256,
        .kdf.scheme     = TPM2_ALG_NULL,
      },
    },
  };

  TPM2B_DATA outside_info         = {0};
  TPML_PCR_SELECTION creation_pcr = {0};

  TSS2_RC rc = Esys_CreatePrimary(ctx->esys, ESYS_TR_RH_OWNER,
                                   ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                                   &in_sensitive, &in_public, &outside_info, &creation_pcr,
                                   out_handle, 0, 0, 0, 0);
  if(rc != TSS2_RC_SUCCESS)
  {
    *out_error = str8_lit("TPM: primary key failed");
    return 0;
  }
  return 1;
}

internal b32
auth_tpm_create_pcr_policy(Auth_TPM_Context *ctx, TPM2B_DIGEST *out_policy_digest, String8 *out_error)
{
  if(ctx == 0 || out_policy_digest == 0 || out_error == 0) { return 0; }

  b32 success                           = 0;
  ESYS_TR session                       = ESYS_TR_NONE;
  TPML_PCR_SELECTION *pcr_selection_out = 0;
  TPML_DIGEST *pcr_values               = 0;
  TPM2B_DIGEST *policy_digest           = 0;
  u8 concat_buffer[256];
  TPM2B_DIGEST pcr_digest               = {0};

  SecureMemoryZero(concat_buffer, sizeof(concat_buffer));
  SecureMemoryZero(&pcr_digest, sizeof(pcr_digest));

  TPMT_SYM_DEF symmetric = {.algorithm = TPM2_ALG_NULL};

  TSS2_RC rc = Esys_StartAuthSession(ctx->esys, ESYS_TR_NONE, ESYS_TR_NONE,
                                      ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, 0,
                                      TPM2_SE_TRIAL, &symmetric, TPM2_ALG_SHA256, &session);
  if(rc != TSS2_RC_SUCCESS)
  {
    *out_error = str8_lit("TPM: policy session failed");
    goto cleanup;
  }

  TPML_PCR_SELECTION pcr_selection = {
    .count = 1,
    .pcrSelections = {{
      .hash = TPM2_ALG_SHA256,
      .sizeofSelect = 3,
      .pcrSelect = {0xB7, 0x03, 0x00}, // PCRs 0,1,2,4,5,7,8,9: firmware, bootloader, kernel, initramfs
    }},
  };

  u32 pcr_update_counter = 0;

  rc = Esys_PCR_Read(ctx->esys, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, &pcr_selection, &pcr_update_counter, &pcr_selection_out, &pcr_values);
  if(rc != TSS2_RC_SUCCESS)
  {
    *out_error = str8_lit("TPM: PCR read failed");
    goto cleanup;
  }

  // TPM PCR policy: concatenate PCR values, compute SHA256 digest for PolicyPCR
  u64 concat_offset = 0;
  for(u32 i = 0; i < pcr_values->count; i += 1)
  {
    u64 digest_size = pcr_values->digests[i].size;
    if(concat_offset + digest_size > sizeof(concat_buffer))
    {
      *out_error = str8_lit("TPM: PCR concatenation overflow");
      goto cleanup;
    }
    MemoryCopy(concat_buffer + concat_offset, pcr_values->digests[i].buffer, digest_size);
    concat_offset += digest_size;
  }

  pcr_digest.size = 32;
  if(EVP_Digest(concat_buffer, concat_offset, pcr_digest.buffer, 0, EVP_sha256(), 0) != 1)
  {
    *out_error = str8_lit("TPM: PCR digest failed");
    goto cleanup;
  }

  Esys_Free(pcr_selection_out);
  pcr_selection_out = 0;
  Esys_Free(pcr_values);
  pcr_values = 0;

  rc = Esys_PolicyPCR(ctx->esys, session, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, &pcr_digest, &pcr_selection);
  if(rc != TSS2_RC_SUCCESS)
  {
    *out_error = str8_lit("TPM: PCR policy failed");
    goto cleanup;
  }

  rc = Esys_PolicyGetDigest(ctx->esys, session, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, &policy_digest);
  if(rc != TSS2_RC_SUCCESS)
  {
    *out_error = str8_lit("TPM: get policy digest failed");
    goto cleanup;
  }

  *out_policy_digest = *policy_digest;
  success = 1;

cleanup:
  if(session != ESYS_TR_NONE) { Esys_FlushContext(ctx->esys, session); }
  if(pcr_selection_out != 0)  { Esys_Free(pcr_selection_out); }
  if(pcr_values != 0)         { Esys_Free(pcr_values); }
  if(policy_digest != 0)      { Esys_Free(policy_digest); }
  SecureMemoryZero(concat_buffer, sizeof(concat_buffer));
  SecureMemoryZero(&pcr_digest, sizeof(pcr_digest));
  return success;
}

internal b32
auth_tpm_seal(Arena *arena, String8 key, String8 sealed_path, String8 *out_error)
{
  if(arena == 0 || key.str == 0 || sealed_path.str == 0 || out_error == 0) { return 0; }
  if(key.size != 32) { *out_error = str8_lit("TPM: key must be 32 bytes"); return 0; }

  Auth_TPM_Context ctx = {0};
  if(!auth_tpm_init(arena, &ctx, out_error)) { return 0; }

  ESYS_TR primary = ESYS_TR_NONE;
  if(!auth_tpm_create_primary(&ctx, &primary, out_error))
  {
    auth_tpm_cleanup(&ctx);
    return 0;
  }

  TPM2B_DIGEST policy_digest = {0};
  if(!auth_tpm_create_pcr_policy(&ctx, &policy_digest, out_error))
  {
    Esys_FlushContext(ctx.esys, primary);
    auth_tpm_cleanup(&ctx);
    return 0;
  }

  TPM2B_SENSITIVE_CREATE in_sensitive = {0};
  in_sensitive.sensitive.data.size    = key.size;
  MemoryCopy(in_sensitive.sensitive.data.buffer, key.str, key.size);

  TPM2B_PUBLIC in_public = {
    .publicArea = {
      .type                                     = TPM2_ALG_KEYEDHASH,
      .nameAlg                                  = TPM2_ALG_SHA256,
      .objectAttributes                         = (TPMA_OBJECT_FIXEDTPM | TPMA_OBJECT_FIXEDPARENT),
      .authPolicy                               = policy_digest,
      .parameters.keyedHashDetail.scheme.scheme = TPM2_ALG_NULL,
    },
  };

  TPM2B_DATA outside_info         = {0};
  TPML_PCR_SELECTION creation_pcr = {0};
  TPM2B_PRIVATE *out_private      = 0;
  TPM2B_PUBLIC *out_public        = 0;

  TSS2_RC rc = Esys_Create(ctx.esys, primary, ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                            &in_sensitive, &in_public, &outside_info, &creation_pcr,
                            &out_private, &out_public, 0, 0, 0);
  SecureMemoryZero(&in_sensitive, sizeof(in_sensitive));

  if(rc != TSS2_RC_SUCCESS)
  {
    Esys_FlushContext(ctx.esys, primary);
    auth_tpm_cleanup(&ctx);
    *out_error = str8_lit("TPM: create sealed object failed");
    return 0;
  }

  Temp temp         = temp_begin(arena);
  u64 blob_capacity = KB(4);
  u8 *blob          = push_array(temp.arena, u8, blob_capacity);

  size_t private_offset = 0;
  size_t public_offset  = 0;

  TSS2_RC rc_marshal = Tss2_MU_TPM2B_PRIVATE_Marshal(out_private, blob, KB(2), &private_offset);
  if(rc_marshal != TSS2_RC_SUCCESS)
  {
    Esys_Free(out_private);
    Esys_Free(out_public);
    Esys_FlushContext(ctx.esys, primary);
    auth_tpm_cleanup(&ctx);
    SecureMemoryZero(blob, blob_capacity);
    temp_end(temp);
    *out_error = str8_lit("TPM: marshal private failed");
    return 0;
  }

  rc_marshal = Tss2_MU_TPM2B_PUBLIC_Marshal(out_public, blob + private_offset, KB(2), &public_offset);
  if(rc_marshal != TSS2_RC_SUCCESS)
  {
    Esys_Free(out_private);
    Esys_Free(out_public);
    Esys_FlushContext(ctx.esys, primary);
    auth_tpm_cleanup(&ctx);
    SecureMemoryZero(blob, blob_capacity);
    temp_end(temp);
    *out_error = str8_lit("TPM: marshal public failed");
    return 0;
  }

  u64 blob_size     = private_offset + public_offset;
  b32 write_success = os_write_data_to_file_path(sealed_path, str8(blob, blob_size));

  Esys_Free(out_private);
  Esys_Free(out_public);
  Esys_FlushContext(ctx.esys, primary);
  auth_tpm_cleanup(&ctx);

  SecureMemoryZero(blob, blob_capacity);
  temp_end(temp);

  if(!write_success) { *out_error = str8_lit("TPM: write sealed blob failed"); return 0; }
  return 1;
}

internal b32
auth_tpm_unseal(Arena *arena, String8 sealed_path, String8 *out_key, String8 *out_error)
{
  if(arena == 0 || sealed_path.str == 0 || out_key == 0 || out_error == 0) { return 0; }

  String8 blob = os_data_from_file_path(arena, sealed_path);
  if(blob.size == 0 || blob.size > KB(4))
  {
    *out_error = str8_lit("TPM: sealed blob invalid size");
    return 0;
  }

  TPM2B_PRIVATE in_private = {0};
  TPM2B_PUBLIC in_public   = {0};
  size_t private_offset    = 0;
  size_t public_offset     = 0;
  TSS2_RC rc_unmarshal     = 0;

  rc_unmarshal = Tss2_MU_TPM2B_PRIVATE_Unmarshal(blob.str, blob.size, &private_offset, &in_private);
  if(rc_unmarshal != TSS2_RC_SUCCESS)
  {
    *out_error = str8_lit("TPM: unmarshal private failed");
    return 0;
  }

  rc_unmarshal = Tss2_MU_TPM2B_PUBLIC_Unmarshal(blob.str + private_offset, blob.size - private_offset, &public_offset, &in_public);
  if(rc_unmarshal != TSS2_RC_SUCCESS)
  {
    *out_error = str8_lit("TPM: unmarshal public failed");
    return 0;
  }

  Auth_TPM_Context ctx = {0};
  if(!auth_tpm_init(arena, &ctx, out_error)) { return 0; }

  ESYS_TR primary = ESYS_TR_NONE;
  if(!auth_tpm_create_primary(&ctx, &primary, out_error))
  {
    auth_tpm_cleanup(&ctx);
    return 0;
  }

  ESYS_TR object = ESYS_TR_NONE;
  TSS2_RC rc     = Esys_Load(ctx.esys, primary, ESYS_TR_PASSWORD, ESYS_TR_NONE, ESYS_TR_NONE,
                              &in_private, &in_public, &object);
  if(rc != TSS2_RC_SUCCESS)
  {
    Esys_FlushContext(ctx.esys, primary);
    auth_tpm_cleanup(&ctx);
    *out_error = str8_lit("TPM: load sealed object failed");
    return 0;
  }

  ESYS_TR session        = ESYS_TR_NONE;
  TPMT_SYM_DEF symmetric = {.algorithm = TPM2_ALG_NULL};

  rc = Esys_StartAuthSession(ctx.esys, ESYS_TR_NONE, ESYS_TR_NONE,
                              ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, 0,
                              TPM2_SE_POLICY, &symmetric, TPM2_ALG_SHA256, &session);
  if(rc != TSS2_RC_SUCCESS)
  {
    Esys_FlushContext(ctx.esys, object);
    Esys_FlushContext(ctx.esys, primary);
    auth_tpm_cleanup(&ctx);
    *out_error = str8_lit("TPM: unseal session failed");
    return 0;
  }

  TPML_PCR_SELECTION pcr_selection = {
    .count = 1,
    .pcrSelections = {{.hash = TPM2_ALG_SHA256, .sizeofSelect = 3, .pcrSelect = {0xB7, 0x03, 0x00}}},
  };

  rc = Esys_PolicyPCR(ctx.esys, session, ESYS_TR_NONE, ESYS_TR_NONE, ESYS_TR_NONE, 0, &pcr_selection);
  if(rc != TSS2_RC_SUCCESS)
  {
    Esys_FlushContext(ctx.esys, session);
    Esys_FlushContext(ctx.esys, object);
    Esys_FlushContext(ctx.esys, primary);
    auth_tpm_cleanup(&ctx);
    *out_error = str8_lit("TPM: PCR policy failed (tampering detected)");
    return 0;
  }

  TPM2B_SENSITIVE_DATA *unsealed = 0;
  rc = Esys_Unseal(ctx.esys, object, session, ESYS_TR_NONE, ESYS_TR_NONE, &unsealed);

  Esys_FlushContext(ctx.esys, session);
  Esys_FlushContext(ctx.esys, object);
  Esys_FlushContext(ctx.esys, primary);
  auth_tpm_cleanup(&ctx);

  if(rc != TSS2_RC_SUCCESS) { *out_error = str8_lit("TPM: unseal failed"); return 0; }
  if(unsealed->size != 32)
  {
    SecureMemoryZero(unsealed->buffer, unsealed->size);
    Esys_Free(unsealed);
    *out_error = str8_lit("TPM: unsealed key wrong size");
    return 0;
  }

  u8 *key_bytes = push_array(arena, u8, 32);
  MemoryCopy(key_bytes, unsealed->buffer, 32);
  SecureMemoryZero(unsealed->buffer, unsealed->size);
  Esys_Free(unsealed);

  *out_key = str8(key_bytes, 32);
  return 1;
}

////////////////////////////////
//~ Machine-ID Fallback

internal b32
auth_tpm_derive_from_machine_id(Arena *arena, String8 *out_key, String8 *out_error)
{
  if(arena == 0 || out_key == 0 || out_error == 0) { return 0; }

  char *env_path = getenv("AUTH_MACHINE_ID_PATH");
  if(env_path == 0)
  {
    *out_error = str8_lit("TPM: machine-id fallback requires AUTH_MACHINE_ID_PATH");
    return 0;
  }

  String8 machine_id_path = str8_cstring(env_path);
  String8 machine_id_raw  = os_data_from_file_path(arena, machine_id_path);
  if(machine_id_raw.size == 0)
  {
    *out_error = str8f(arena, "TPM: failed to read %S", machine_id_path);
    return 0;
  }

  String8 machine_id = str8_skip_chop_whitespace(machine_id_raw);
  if(machine_id.size != 32) { *out_error = str8_lit("TPM: invalid machine-id format"); return 0; }

  u8 *key_bytes = push_array(arena, u8, 32);

  EVP_KDF *kdf = EVP_KDF_fetch(0, "HKDF", 0);
  if(kdf == 0)
  {
    *out_error = str8_lit("TPM: HKDF not available");
    return 0;
  }

  EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
  EVP_KDF_free(kdf);

  if(kctx == 0)
  {
    *out_error = str8_lit("TPM: HKDF context failed");
    return 0;
  }

  String8 salt = str8_lit("9auth-keyseal-v1");
  String8 info = str8_lit("encryption-key");

  OSSL_PARAM params[] = {
    OSSL_PARAM_construct_utf8_string("digest", "SHA256", 0),
    OSSL_PARAM_construct_octet_string("salt", (void*)salt.str, salt.size),
    OSSL_PARAM_construct_octet_string("key", (void*)machine_id.str, machine_id.size),
    OSSL_PARAM_construct_octet_string("info", (void*)info.str, info.size),
    OSSL_PARAM_construct_end()
  };

  if(EVP_KDF_derive(kctx, key_bytes, 32, params) != 1)
  {
    EVP_KDF_CTX_free(kctx);
    SecureMemoryZero(key_bytes, 32);
    *out_error = str8_lit("TPM: HKDF derivation failed");
    return 0;
  }

  EVP_KDF_CTX_free(kctx);
  *out_key = str8(key_bytes, 32);
  return 1;
}

////////////////////////////////
//~ Unified Sealing

internal b32
auth_tpm_seal_key(Arena *arena, String8 key, String8 tpm_sealed_path, String8 *out_error)
{
  if(arena == 0 || key.str == 0 || tpm_sealed_path.str == 0 || out_error == 0) { return 0; }
  if(key.size != 32) { *out_error = str8_lit("TPM: key must be 32 bytes"); return 0; }

  if(auth_tpm_available())
  {
    String8 tpm_error = str8_zero();
    if(auth_tpm_seal(arena, key, tpm_sealed_path, &tpm_error)) { return 1; }
  }

  String8 derived = str8_zero();
  if(!auth_tpm_derive_from_machine_id(arena, &derived, out_error)) { return 0; }

  if(CRYPTO_memcmp(key.str, derived.str, 32) != 0)
  {
    SecureMemoryZero((void*)derived.str, derived.size);
    *out_error = str8_lit("TPM: derived key mismatch");
    return 0;
  }

  SecureMemoryZero((void*)derived.str, derived.size);
  return 1;
}

internal Auth_TPM_Result
auth_tpm_unseal_key(Arena *arena, String8 *out_error)
{
  Auth_TPM_Result result  = {0};
  if(arena == 0 || out_error == 0) { return result; }

  String8 tpm_sealed_path = str8_lit("/var/lib/9auth/tpm-sealed-key");

  if(auth_tpm_available())
  {
    String8 tpm_error = str8_zero();
    String8 tpm_key   = str8_zero();

    if(auth_tpm_unseal(arena, tpm_sealed_path, &tpm_key, &tpm_error))
    {
      result.key    = tpm_key;
      result.source = Auth_TPM_Source_Hardware;
      return result;
    }
  }

  String8 key = str8_zero();
  if(!auth_tpm_derive_from_machine_id(arena, &key, out_error)) { return result; }

  result.key    = key;
  result.source = Auth_TPM_Source_MachineID;
  return result;
}
