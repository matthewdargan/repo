#ifndef AUTH_ED25519_H
#define AUTH_ED25519_H

////////////////////////////////
//~ Includes

#include <openssl/evp.h>
#include <openssl/err.h>

////////////////////////////////
//~ Challenge Generation

internal b32 auth_ed25519_generate_challenge(u8 challenge[32]);

////////////////////////////////
//~ Key Generation

internal b32 auth_ed25519_generate_keypair(u8 public_key[32], u8 private_key[32], String8 *out_error);

////////////////////////////////
//~ Challenge Signing

typedef struct Auth_Ed25519_SignParams Auth_Ed25519_SignParams;
struct Auth_Ed25519_SignParams
{
  u8 challenge[32];
  u8 private_key[32];
};

internal b32 auth_ed25519_sign_challenge(Auth_Ed25519_SignParams *params,
                                         u8 signature[64], String8 *out_error);

////////////////////////////////
//~ Signature Verification

typedef struct Auth_Ed25519_VerifyParams Auth_Ed25519_VerifyParams;
struct Auth_Ed25519_VerifyParams
{
  u8 challenge[32];
  u8 signature[64];
  u8 public_key[32];
};

internal b32 auth_ed25519_verify_signature(Auth_Ed25519_VerifyParams *params,
                                           String8 *out_error);

#endif // AUTH_ED25519_H
