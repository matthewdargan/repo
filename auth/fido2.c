////////////////////////////////
//~ FIDO2 Operations

internal String8
auth_fido2_error_string(Arena *arena, int error_code)
{
  const char *error_str = fido_strerr(error_code);
  return str8_copy(arena, str8_cstring((char *)error_str));
}

internal b32
auth_fido2_generate_challenge(u8 challenge[32])
{
  if(getentropy(challenge, 32) != 0)
  {
    return 0;
  }
  return 1;
}

internal Auth_Fido2_DeviceList
auth_fido2_enumerate_devices(Arena *arena)
{
  Auth_Fido2_DeviceList result = {0};
  fido_dev_info_t *dev_list = 0;
  size_t dev_count = 0;

  fido_init(0);

  dev_list = fido_dev_info_new(64);
  if(dev_list == 0)
  {
    return result;
  }

  int r = fido_dev_info_manifest(dev_list, 64, &dev_count);
  if(r != FIDO_OK)
  {
    fido_dev_info_free(&dev_list, 64);
    return result;
  }

  for(size_t i = 0; i < dev_count; i += 1)
  {
    const fido_dev_info_t *di = fido_dev_info_ptr(dev_list, i);
    if(di == 0)
    {
      continue;
    }

    Auth_Fido2_DeviceInfo *info = push_array(arena, Auth_Fido2_DeviceInfo, 1);

    const char *path = fido_dev_info_path(di);
    const char *product = fido_dev_info_product_string(di);
    const char *manufacturer = fido_dev_info_manufacturer_string(di);

    if(path != 0)
    {
      info->path = str8_copy(arena, str8_cstring((char *)path));
    }
    if(product != 0)
    {
      info->product = str8_copy(arena, str8_cstring((char *)product));
    }
    if(manufacturer != 0)
    {
      info->manufacturer = str8_copy(arena, str8_cstring((char *)manufacturer));
    }

    info->vendor_id = fido_dev_info_vendor(di);
    info->product_id = fido_dev_info_product(di);

    SLLQueuePush(result.first, result.last, info);
    result.count += 1;
  }

  fido_dev_info_free(&dev_list, dev_count);
  return result;
}

internal b32
auth_fido2_register_credential(Arena *arena, Auth_Fido2_RegisterParams params, Auth_Key *out_key, String8 *out_error)
{
  fido_dev_t *dev = 0;
  fido_cred_t *cred = 0;

  Auth_Fido2_DeviceList devices = auth_fido2_enumerate_devices(arena);
  if(devices.count == 0)
  {
    *out_error = str8_lit("fido2: no devices found");
    return 0;
  }

  Auth_Fido2_DeviceInfo *device = devices.first;

  dev = fido_dev_new();
  if(dev == 0)
  {
    *out_error = str8_lit("fido2: failed to allocate device");
    return 0;
  }

  Temp temp = temp_begin(arena);
  String8 path_copy = str8_copy(temp.arena, device->path);
  char *path_cstr = (char *)path_copy.str;

  int r = fido_dev_open(dev, path_cstr);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to open device: %S", auth_fido2_error_string(arena, r));
    temp_end(temp);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  cred = fido_cred_new();
  if(cred == 0)
  {
    *out_error = str8_lit("fido2: failed to allocate credential");
    temp_end(temp);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  r = fido_cred_set_type(cred, COSE_ES256);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to set credential type: %S", auth_fido2_error_string(arena, r));
    temp_end(temp);
    fido_cred_free(&cred);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  String8 rp_id_copy = str8_copy(temp.arena, params.rp_id);
  char *rp_id_cstr = (char *)rp_id_copy.str;

  r = fido_cred_set_rp(cred, rp_id_cstr, rp_id_cstr);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to set relying party: %S", auth_fido2_error_string(arena, r));
    temp_end(temp);
    fido_cred_free(&cred);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  String8 user_copy = str8_copy(temp.arena, params.user);
  char *user_name_cstr = (char *)user_copy.str;

  const u8 *user_id = params.user.str;
  size_t user_id_len = params.user.size;

  r = fido_cred_set_user(cred, user_id, user_id_len, user_name_cstr, 0, 0);
  temp_end(temp);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to set user: %S", auth_fido2_error_string(arena, r));
    fido_cred_free(&cred);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  u8 client_data_hash[32];
  if(!auth_fido2_generate_challenge(client_data_hash))
  {
    *out_error = str8_lit("fido2: failed to generate challenge");
    fido_cred_free(&cred);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  r = fido_cred_set_clientdata_hash(cred, client_data_hash, 32);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to set client data hash: %S", auth_fido2_error_string(arena, r));
    fido_cred_free(&cred);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  if(params.require_uv)
  {
    r = fido_cred_set_uv(cred, FIDO_OPT_TRUE);
    if(r != FIDO_OK)
    {
      *out_error = str8f(arena, "fido2: failed to set UV option: %S", auth_fido2_error_string(arena, r));
      fido_cred_free(&cred);
      fido_dev_close(dev);
      fido_dev_free(&dev);
      return 0;
    }
  }

  r = fido_cred_set_rk(cred, FIDO_OPT_FALSE);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to set RK option: %S", auth_fido2_error_string(arena, r));
    fido_cred_free(&cred);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  r = fido_dev_make_cred(dev, cred, 0);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to make credential: %S", auth_fido2_error_string(arena, r));
    fido_cred_free(&cred);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  const u8 *cred_id = fido_cred_id_ptr(cred);
  size_t cred_id_len = fido_cred_id_len(cred);

  if(cred_id == 0 || cred_id_len == 0 || cred_id_len > 256)
  {
    *out_error = str8_lit("fido2: invalid credential ID");
    fido_cred_free(&cred);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  const u8 *pubkey = fido_cred_pubkey_ptr(cred);
  size_t pubkey_len = fido_cred_pubkey_len(cred);

  if(pubkey == 0 || pubkey_len == 0 || pubkey_len > 256)
  {
    *out_error = str8_lit("fido2: invalid public key");
    fido_cred_free(&cred);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  out_key->type = Auth_Proto_FIDO2;
  out_key->user = str8_copy(arena, params.user);
  out_key->auth_id = str8_copy(arena, params.rp_id);

  MemoryCopy(out_key->credential_id, cred_id, cred_id_len);
  out_key->credential_id_len = cred_id_len;

  MemoryCopy(out_key->public_key, pubkey, pubkey_len);
  out_key->public_key_len = pubkey_len;

  fido_cred_free(&cred);
  fido_dev_close(dev);
  fido_dev_free(&dev);

  return 1;
}

internal b32
auth_fido2_get_assertion(Arena *arena, Auth_Fido2_AssertParams *params, Auth_Fido2_Assertion *out_assertion,
                         String8 *out_error)
{
  Auth_Fido2_DeviceList devices = auth_fido2_enumerate_devices(arena);
  if(devices.count == 0)
  {
    *out_error = str8_lit("fido2: no devices found");
    return 0;
  }

  Auth_Fido2_DeviceInfo *device = devices.first;

  fido_dev_t *dev = fido_dev_new();
  if(dev == 0)
  {
    *out_error = str8_lit("fido2: failed to allocate device");
    return 0;
  }

  Temp temp = temp_begin(arena);
  String8 path_copy = str8_copy(temp.arena, device->path);
  char *path_cstr = (char *)path_copy.str;

  int r = fido_dev_open(dev, path_cstr);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to open device: %S", auth_fido2_error_string(arena, r));
    temp_end(temp);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  fido_assert_t *assert = fido_assert_new();
  if(assert == 0)
  {
    *out_error = str8_lit("fido2: failed to allocate assertion");
    temp_end(temp);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  String8 rp_id_copy = str8_copy(temp.arena, params->rp_id);
  char *rp_id_cstr = (char *)rp_id_copy.str;

  r = fido_assert_set_rp(assert, rp_id_cstr);
  temp_end(temp);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to set relying party: %S", auth_fido2_error_string(arena, r));
    fido_assert_free(&assert);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  r = fido_assert_set_clientdata_hash(assert, params->challenge, 32);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to set client data hash: %S", auth_fido2_error_string(arena, r));
    fido_assert_free(&assert);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  r = fido_assert_allow_cred(assert, params->credential_id, params->credential_id_len);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to allow credential: %S", auth_fido2_error_string(arena, r));
    fido_assert_free(&assert);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  if(params->require_uv)
  {
    r = fido_assert_set_uv(assert, FIDO_OPT_TRUE);
    if(r != FIDO_OK)
    {
      *out_error = str8f(arena, "fido2: failed to set UV option: %S", auth_fido2_error_string(arena, r));
      fido_assert_free(&assert);
      fido_dev_close(dev);
      fido_dev_free(&dev);
      return 0;
    }
  }

  r = fido_dev_get_assert(dev, assert, 0);
  if(r != FIDO_OK)
  {
    *out_error = str8f(arena, "fido2: failed to get assertion: %S", auth_fido2_error_string(arena, r));
    fido_assert_free(&assert);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  const u8 *sig = fido_assert_sig_ptr(assert, 0);
  size_t sig_len = fido_assert_sig_len(assert, 0);

  if(sig == 0 || sig_len == 0 || sig_len > 256)
  {
    *out_error = str8_lit("fido2: invalid signature");
    fido_assert_free(&assert);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  const u8 *authdata = fido_assert_authdata_ptr(assert, 0);
  size_t authdata_len = fido_assert_authdata_len(assert, 0);

  if(authdata == 0 || authdata_len == 0 || authdata_len > 256)
  {
    *out_error = str8_lit("fido2: invalid authenticator data");
    fido_assert_free(&assert);
    fido_dev_close(dev);
    fido_dev_free(&dev);
    return 0;
  }

  MemoryCopy(out_assertion->signature, sig, sig_len);
  out_assertion->signature_len = sig_len;

  MemoryCopy(out_assertion->auth_data, authdata, authdata_len);
  out_assertion->auth_data_len = authdata_len;

  fido_assert_free(&assert);
  fido_dev_close(dev);
  fido_dev_free(&dev);

  return 1;
}

internal b32
auth_fido2_verify_signature(Arena *arena, Auth_Fido2_VerifyParams *params, String8 *out_error)
{
  fido_init(0);

  fido_assert_t *assert = fido_assert_new();
  if(assert == 0)
  {
    *out_error = str8_lit("fido2: failed to allocate assert");
    return 0;
  }

  es256_pk_t *pk = es256_pk_new();
  if(pk == 0)
  {
    *out_error = str8_lit("fido2: failed to allocate public key");
    fido_assert_free(&assert);
    return 0;
  }

  int r = es256_pk_from_ptr(pk, params->public_key, params->public_key_len);
  if(r != FIDO_OK)
  {
    *out_error = str8_lit("fido2: failed to parse public key");
    es256_pk_free(&pk);
    fido_assert_free(&assert);
    return 0;
  }

  Temp temp = scratch_begin(&arena, 1);
  String8 rp_id_copy = str8_copy(temp.arena, params->rp_id);
  char *rp_id_cstr = (char *)rp_id_copy.str;
  r = fido_assert_set_rp(assert, rp_id_cstr);
  scratch_end(temp);
  if(r != FIDO_OK)
  {
    *out_error = str8_lit("fido2: failed to set RP ID");
    es256_pk_free(&pk);
    fido_assert_free(&assert);
    return 0;
  }

  r = fido_assert_set_clientdata_hash(assert, params->challenge, 32);
  if(r != FIDO_OK)
  {
    *out_error = str8_lit("fido2: failed to set clientdata hash");
    es256_pk_free(&pk);
    fido_assert_free(&assert);
    return 0;
  }

  r = fido_assert_set_count(assert, 1);
  if(r != FIDO_OK)
  {
    *out_error = str8_lit("fido2: failed to set count");
    es256_pk_free(&pk);
    fido_assert_free(&assert);
    return 0;
  }

  r = fido_assert_set_authdata(assert, 0, params->auth_data, params->auth_data_len);
  if(r != FIDO_OK)
  {
    *out_error = str8_lit("fido2: failed to set authdata");
    es256_pk_free(&pk);
    fido_assert_free(&assert);
    return 0;
  }

  r = fido_assert_set_sig(assert, 0, params->signature, params->signature_len);
  if(r != FIDO_OK)
  {
    *out_error = str8_lit("fido2: failed to set signature");
    es256_pk_free(&pk);
    fido_assert_free(&assert);
    return 0;
  }

  r = fido_assert_verify(assert, 0, COSE_ES256, pk);
  if(r != FIDO_OK)
  {
    *out_error = str8_lit("fido2: signature verification failed");
    es256_pk_free(&pk);
    fido_assert_free(&assert);
    return 0;
  }

  es256_pk_free(&pk);
  fido_assert_free(&assert);

  return 1;
}
