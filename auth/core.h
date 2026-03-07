#ifndef AUTH_CORE_H
#define AUTH_CORE_H

#include <openssl/crypto.h>

////////////////////////////////
//~ Authentication State

typedef enum
{
  Auth_State_None,
  Auth_State_Started,
  Auth_State_ChallengeReady,
  Auth_State_ChallengeSent,
  Auth_State_Done,
  Auth_State_Error,
} Auth_State;

////////////////////////////////
//~ Conversation

typedef struct Auth_Key Auth_Key;
typedef struct Auth_Conv Auth_Conv;

struct Auth_Conv
{
  Auth_Conv *next;
  u64 tag;
  String8 user;
  String8 auth_id;
  String8 role;
  String8 proto;
  Auth_Key *key;
  Auth_State state;
  u64 start_time_us;
  u8 challenge[32];
  u8 auth_data[256];
  u64 auth_data_len;
  u8 signature[256];
  u64 signature_len;
  b32 verified;
  String8 error;
};

////////////////////////////////
//~ Protocol

typedef enum
{
  Auth_Proto_Ed25519 = 1,
  Auth_Proto_FIDO2   = 2,
} Auth_Proto;

////////////////////////////////
//~ Key

typedef struct Auth_Key Auth_Key;
struct Auth_Key
{
  Auth_Proto type;
  String8 user;
  String8 auth_id;
  // FIDO2
  u8 credential_id[256];
  u64 credential_id_len;
  u8 public_key[256];
  u64 public_key_len;
  // Ed25519
  u8 ed25519_public_key[32];
  u8 ed25519_private_key[32];
};

////////////////////////////////
//~ Key Ring

typedef struct Auth_KeyRing Auth_KeyRing;
struct Auth_KeyRing
{
  Arena *arena;
  Auth_Key *keys;
  u64 count;
  u64 capacity;
};

#define AUTH_ENCRYPTED_VERSION   0x01    // Format version
#define AUTH_KDF_ITERATIONS      2000000 // Future-proof to 2030 (3.3x NIST 2023 minimum)
#define AUTH_SALT_SIZE           32      // 256-bit salt for PBKDF2
#define AUTH_NONCE_SIZE          12      // AES-GCM standard nonce size
#define AUTH_TAG_SIZE            16      // AES-GCM 128-bit authentication tag
#define AUTH_KEY_SIZE            32      // AES-256 key size
#define AUTH_MAX_CREDENTIAL_SIZE 256     // Maximum credential/public key size
#define AUTH_MAX_IDENTIFIER_SIZE 256     // Maximum user/auth_id length
#define AUTH_CHALLENGE_SIZE      32      // Ed25519/FIDO2 challenge size
#define AUTH_SIGNATURE_SIZE_MAX  256     // Maximum signature size

#define SecureMemoryZero(ptr, size) OPENSSL_cleanse((void*)(ptr), (size))

////////////////////////////////
//~ Conversation Functions

internal Auth_Conv *auth_conv_alloc(Arena *arena, u64 tag, String8 user, String8 auth_id);

////////////////////////////////
//~ Key Ring Functions

internal Auth_KeyRing auth_keyring_alloc(Arena *arena, u64 capacity);
internal b32 auth_keyring_add(Auth_KeyRing *ring, Auth_Key *key, String8 *out_error);
internal Auth_Key *auth_keyring_lookup(Auth_KeyRing *ring, String8 user, String8 auth_id);
internal void auth_keyring_remove(Auth_KeyRing *ring, String8 user, String8 auth_id, Auth_Proto type);

////////////////////////////////
//~ Key Ring Serialization

internal b32 auth_keyring_encrypt(Arena *arena, String8 plaintext, String8 passphrase, String8 *out_encrypted, String8 *out_error);
internal b32 auth_keyring_decrypt(Arena *arena, String8 encrypted, String8 passphrase, String8 *out_plaintext, String8 *out_error);
internal b32 auth_keyring_save(Arena *arena, Auth_KeyRing *ring, String8 passphrase, String8 *out_data, String8 *out_error);
internal b32 auth_keyring_load(Arena *arena, Auth_KeyRing *ring, String8 data, String8 passphrase);

////////////////////////////////
//~ Security Validation

internal b32 auth_validate_credential_format(Auth_Key *key, String8 *out_error);
internal b32 auth_validate_identifier(String8 str, String8 *out_error);

#endif // AUTH_CORE_H
