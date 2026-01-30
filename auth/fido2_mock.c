////////////////////////////////
//~ Mock Data

global u8 mock_credential_id[64] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40,
};

global u8 mock_public_key[65] = {
    0x04, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0,
};

global u8 mock_auth_data[37] = {
    0x49, 0x96, 0x0d, 0xe5, 0x88, 0x0e, 0x8c, 0x68, 0x74, 0x34, 0x17, 0x0f, 0x64, 0x76, 0x60, 0x5b,
    0x8f, 0xe4, 0xae, 0xb9, 0xa2, 0x86, 0x32, 0xc7, 0x99, 0x5c, 0xf3, 0xba, 0x83, 0x1d, 0x97, 0x63,
    0x01, 0x00, 0x00, 0x00, 0x01,
};

global u8 mock_signature[64] = {
    0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef, 0xf0,
    0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff, 0x00,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};

////////////////////////////////
//~ Mock FIDO2 Operations

internal b32
auth_fido2_generate_challenge(u8 challenge[32])
{
  for(u64 i = 0; i < 32; i += 1)
  {
    challenge[i] = (u8)((i * 7 + 13) & 0xff);
  }
  return 1;
}

internal Auth_Fido2_DeviceList
auth_fido2_enumerate_devices(Arena *arena)
{
  Auth_Fido2_DeviceList result = {0};

  Auth_Fido2_DeviceInfo *device = push_array(arena, Auth_Fido2_DeviceInfo, 1);
  device->path = str8_lit("/dev/hidraw0");
  device->product = str8_lit("Mock FIDO2 Authenticator");
  device->manufacturer = str8_lit("Mock Manufacturer");
  device->vendor_id = 0x1050;
  device->product_id = 0x0407;

  SLLQueuePush(result.first, result.last, device);
  result.count = 1;

  return result;
}

internal b32
auth_fido2_register_credential(Arena *arena, Auth_Fido2_RegisterParams params, Auth_Key *out_key, String8 *out_error)
{
  if(params.user.size == 0)
  {
    *out_error = str8_lit("fido2: user name is required");
    return 0;
  }

  if(params.rp_id.size == 0)
  {
    *out_error = str8_lit("fido2: RP ID is required");
    return 0;
  }

  out_key->type = Auth_Key_Type_FIDO2;
  out_key->user = str8_copy(arena, params.user);
  out_key->rp_id = str8_copy(arena, params.rp_id);

  MemoryCopy(out_key->credential_id, mock_credential_id, sizeof(mock_credential_id));
  out_key->credential_id_len = sizeof(mock_credential_id);

  MemoryCopy(out_key->public_key, mock_public_key, sizeof(mock_public_key));
  out_key->public_key_len = sizeof(mock_public_key);

  return 1;
}

internal b32
auth_fido2_get_assertion(Arena *arena, Auth_Fido2_AssertParams *params, Auth_Fido2_Assertion *out_assertion,
                         String8 *out_error)
{
  (void)arena;

  if(params->credential_id == 0 || params->credential_id_len == 0)
  {
    *out_error = str8_lit("fido2: credential ID is required");
    return 0;
  }

  if(params->credential_id_len != sizeof(mock_credential_id))
  {
    *out_error = str8_lit("fido2: invalid credential ID length");
    return 0;
  }

  if(!MemoryMatch(params->credential_id, mock_credential_id, sizeof(mock_credential_id)))
  {
    *out_error = str8_lit("fido2: credential not found");
    return 0;
  }

  MemoryCopy(out_assertion->signature, mock_signature, sizeof(mock_signature));
  out_assertion->signature_len = sizeof(mock_signature);

  MemoryCopy(out_assertion->auth_data, mock_auth_data, sizeof(mock_auth_data));
  out_assertion->auth_data_len = sizeof(mock_auth_data);

  return 1;
}

internal b32
auth_fido2_verify_signature(Arena *arena, Auth_Fido2_VerifyParams *params, String8 *out_error)
{
  (void)arena;
  if(params->signature == 0 || params->signature_len == 0)
  {
    *out_error = str8_lit("fido2: signature is required");
    return 0;
  }

  if(params->public_key == 0 || params->public_key_len == 0)
  {
    *out_error = str8_lit("fido2: public key is required");
    return 0;
  }

  if(params->signature_len == sizeof(mock_signature) && MemoryMatch(params->signature, mock_signature, sizeof(mock_signature)))
  {
    return 1;
  }

  *out_error = str8_lit("fido2: signature verification failed");
  return 0;
}
