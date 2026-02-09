#ifndef AUTH_FIDO2_H
#define AUTH_FIDO2_H

////////////////////////////////
//~ Includes

#include <fido.h>
#include <fido/es256.h>

////////////////////////////////
//~ Registration

typedef struct Auth_Fido2_RegisterParams Auth_Fido2_RegisterParams;
struct Auth_Fido2_RegisterParams
{
  String8 user;
  String8 rp_id;
  b32 require_uv;
};

////////////////////////////////
//~ Assertion

typedef struct Auth_Fido2_AssertParams Auth_Fido2_AssertParams;
struct Auth_Fido2_AssertParams
{
  String8 rp_id;
  u8 challenge[32];
  u8 *credential_id;
  u64 credential_id_len;
  b32 require_uv;
};

typedef struct Auth_Fido2_Assertion Auth_Fido2_Assertion;
struct Auth_Fido2_Assertion
{
  u8 signature[256];
  u64 signature_len;
  u8 auth_data[256];
  u64 auth_data_len;
};

////////////////////////////////
//~ Verification

typedef struct Auth_Fido2_VerifyParams Auth_Fido2_VerifyParams;
struct Auth_Fido2_VerifyParams
{
  String8 rp_id;
  u8 challenge[32];
  u8 *auth_data;
  u64 auth_data_len;
  u8 *signature;
  u64 signature_len;
  u8 *public_key;
  u64 public_key_len;
};

////////////////////////////////
//~ Devices

typedef struct Auth_Fido2_DeviceInfo Auth_Fido2_DeviceInfo;
struct Auth_Fido2_DeviceInfo
{
  Auth_Fido2_DeviceInfo *next;
  String8 path;
  String8 product;
  String8 manufacturer;
  u16 vendor_id;
  u16 product_id;
};

typedef struct Auth_Fido2_DeviceList Auth_Fido2_DeviceList;
struct Auth_Fido2_DeviceList
{
  Auth_Fido2_DeviceInfo *first;
  Auth_Fido2_DeviceInfo *last;
  u64 count;
};

////////////////////////////////
//~ FIDO2 Operations

internal b32 auth_fido2_generate_challenge(u8 challenge[32]);
internal Auth_Fido2_DeviceList auth_fido2_enumerate_devices(Arena *arena);
internal b32 auth_fido2_register_credential(Arena *arena, Auth_Fido2_RegisterParams params, Auth_Key *out_key,
                                            String8 *out_error);
internal b32 auth_fido2_get_assertion(Arena *arena, Auth_Fido2_AssertParams *params,
                                      Auth_Fido2_Assertion *out_assertion, String8 *out_error);
internal b32 auth_fido2_verify_signature(Arena *arena, Auth_Fido2_VerifyParams *params, String8 *out_error);

#endif // AUTH_FIDO2_H
