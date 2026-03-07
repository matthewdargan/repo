#ifndef AUTH_RPC_H
#define AUTH_RPC_H

////////////////////////////////
//~ RPC Timeouts

#define AUTH_RPC_CONVERSATION_TIMEOUT_US (5 * Million(1))  // 5 seconds for challenge-response
#define AUTH_RPC_CONVERSATION_MAX_AGE_US (60 * Million(1)) // 60 seconds max conversation lifetime
#define AUTH_RPC_DONE_CLEANUP_DELAY_US   (5 * Million(1))  // 5 seconds before cleaning completed conversations

////////////////////////////////
//~ DoS Protection

#define AUTH_MAX_ATTEMPTS               5                // Maximum failed attempts before lockout
#define AUTH_LOCKOUT_US                 (5 * Million(1)) // 5 seconds lockout after max failed attempts
#define AUTH_RATE_LIMIT_BUCKETS         256              // Hash table size for rate limit tracking
#define AUTH_MAX_CONVERSATIONS          1000             // Maximum concurrent auth conversations
#define AUTH_KEYRING_RELOAD_INTERVAL_US (1 * Million(1)) // Reload keyring at most once per second

////////////////////////////////
//~ RPC Command Types

typedef enum
{
  Auth_RPC_Command_None,
  Auth_RPC_Command_Start,
  Auth_RPC_Command_Read,
  Auth_RPC_Command_Write,
} Auth_RPC_Command;

////////////////////////////////
//~ RPC Request/Response

typedef struct Auth_RPC_StartParams Auth_RPC_StartParams;
struct Auth_RPC_StartParams
{
  String8 user;
  String8 auth_id;
  String8 proto;
  String8 role;
};

typedef struct Auth_RPC_Request Auth_RPC_Request;
struct Auth_RPC_Request
{
  Auth_RPC_Command command;
  Auth_RPC_StartParams start;
  String8 write_data;
};

typedef struct Auth_RPC_Response Auth_RPC_Response;
struct Auth_RPC_Response
{
  b32 success;
  String8 error;
  String8 data;
};

////////////////////////////////
//~ Rate Limiting

typedef struct Auth_RateLimit Auth_RateLimit;
struct Auth_RateLimit
{
  Auth_RateLimit *next;
  u64 hash;
  u32 attempt_count;
  u64 lockout_until_us;
  u64 last_attempt_us;
};

////////////////////////////////
//~ RPC State

typedef struct Auth_RPC_State Auth_RPC_State;
struct Auth_RPC_State
{
  Arena *arena;
  Auth_KeyRing *keyring;
  String8 keys_path;
  String8 passphrase;
  Mutex mutex;
  Auth_Conv *conv_first;
  Auth_Conv *conv_last;
  u64 next_conv_id;
  u64 conv_count;
  u64 last_keyring_reload_us;
  Auth_RateLimit *rate_limit_buckets[AUTH_RATE_LIMIT_BUCKETS];
};

////////////////////////////////
//~ RPC Functions

internal Auth_RPC_State *auth_rpc_state_alloc(Arena *arena, Auth_KeyRing *keyring, String8 keys_path, String8 passphrase);
internal Auth_RPC_Request auth_rpc_parse(Arena *arena, String8 command_line);
internal Auth_RPC_Response auth_rpc_handle_start(Auth_RPC_State *state, Auth_Conv **out_conv, Auth_RPC_StartParams params);
internal Auth_RPC_Response auth_rpc_execute(Arena *arena, Auth_RPC_State *state, Auth_Conv *conv, Auth_RPC_Request request);

#endif // AUTH_RPC_H
