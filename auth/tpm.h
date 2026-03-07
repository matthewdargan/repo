#ifndef AUTH_TPM_H
#define AUTH_TPM_H

#include <openssl/kdf.h>
#include <tss2/tss2_esys.h>
#include <tss2/tss2_mu.h>

////////////////////////////////
//~ Types

typedef enum Auth_TPM_Source
{
  Auth_TPM_Source_None,
  Auth_TPM_Source_Hardware,
  Auth_TPM_Source_MachineID,
} Auth_TPM_Source;

typedef struct Auth_TPM_Result
{
  String8 key;
  Auth_TPM_Source source;
} Auth_TPM_Result;

////////////////////////////////
//~ TPM Functions

internal b32 auth_tpm_available(void);
internal b32 auth_tpm_seal(Arena *arena, String8 key, String8 sealed_path, String8 *out_error);
internal b32 auth_tpm_unseal(Arena *arena, String8 sealed_path, String8 *out_key, String8 *out_error);

////////////////////////////////
//~ Machine-ID Fallback

internal b32 auth_tpm_derive_from_machine_id(Arena *arena, String8 *out_key, String8 *out_error);

////////////////////////////////
//~ Unified Sealing

internal b32 auth_tpm_seal_key(Arena *arena, String8 key, String8 tpm_sealed_path, String8 *out_error);
internal Auth_TPM_Result auth_tpm_unseal_key(Arena *arena, String8 *out_error);

#endif // AUTH_TPM_H
