#ifndef AUTH_RPC_H
#define AUTH_RPC_H

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
  String8 proto;
  String8 role;
  String8 user;
  String8 server;
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
//~ RPC State

typedef struct Auth_RPC_State Auth_RPC_State;
struct Auth_RPC_State
{
  Arena *arena;
  Auth_KeyRing *keyring;
  Auth_Conv *conv_first;
  Auth_Conv *conv_last;
  u64 next_conv_id;
};

////////////////////////////////
//~ RPC Functions

internal Auth_RPC_State *auth_rpc_state_alloc(Arena *arena, Auth_KeyRing *keyring);
internal Auth_RPC_Request auth_rpc_parse(Arena *arena, String8 command_line);
internal Auth_RPC_Response auth_rpc_execute(Arena *arena, Auth_RPC_State *state, Auth_Conv *conv,
                                            Auth_RPC_Request *request);

#endif // AUTH_RPC_H
