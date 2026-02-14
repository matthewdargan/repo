#ifndef AUTH_CORE_H
#define AUTH_CORE_H

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
  u32 start_time;
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
  Auth_Proto_FIDO2 = 2,
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

internal String8 auth_keyring_save(Arena *arena, Auth_KeyRing *ring);
internal b32 auth_keyring_load(Arena *arena, Auth_KeyRing *ring, String8 data);

////////////////////////////////
//~ Security Validation

internal b32 auth_validate_credential_format(Auth_Key *key, String8 *out_error);
internal b32 auth_validate_identifier(String8 str, String8 *out_error);

#endif // AUTH_CORE_H
