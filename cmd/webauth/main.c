#include "base/inc.h"
#include "json/inc.h"
#include "9p/inc.h"
#include "auth/inc.h"
#include "base/inc.c"
#include "json/inc.c"
#include "9p/inc.c"
#include "auth/inc.c"

////////////////////////////////
//~ Types

typedef struct Session Session;
struct Session
{
  String8 id;
  String8 username;
  u64 expiry_us;
};

typedef struct SessionTable SessionTable;
struct SessionTable
{
  Mutex mutex;
  Arena *arena;
  Session *slots;
  u64 capacity;
};

typedef struct Invite Invite;
struct Invite
{
  String8 token;
  String8 username;
  u64 expiry_us;
  b32 used;
};

typedef struct InviteTable InviteTable;
struct InviteTable
{
  Mutex mutex;
  Arena *arena;
  Invite *slots;
  u64 capacity;
};

typedef struct Challenge Challenge;
struct Challenge
{
  u8 bytes[32];
  u64 expiry_us;
};

typedef struct ChallengeTable ChallengeTable;
struct ChallengeTable
{
  Mutex mutex;
  Arena *arena;
  Challenge *slots;
  u64 capacity;
};

typedef struct Auth9P Auth9P;
struct Auth9P
{
  Client9P *client;
  OS_Handle socket;
  Mutex mutex;
};

typedef struct HTTPRequest HTTPRequest;
struct HTTPRequest
{
  String8 method;
  String8 path;
  String8 query;
  String8 body;
  String8 cookie;
  String8 host;
};

////////////////////////////////
//~ Globals

global SessionTable *g_sessions;
global InviteTable *g_invites;
global ChallengeTable *g_challenges;
global String8 g_rp_id;
global String8 g_rp_name;
global String8 g_auth_socket;
global u64 g_session_duration_us;
global u64 g_invite_duration_us;
global u16 g_listen_port;

////////////////////////////////
//~ Session Table

internal SessionTable *
session_table_alloc(Arena *arena, u64 capacity)
{
  SessionTable *t = push_array(arena, SessionTable, 1);
  t->mutex    = mutex_alloc();
  t->arena    = arena;
  t->slots    = push_array(arena, Session, capacity);
  t->capacity = capacity;
  return t;
}

internal String8
session_create(SessionTable *t, String8 username, u64 duration_us)
{
  u8 random[32];
  if(getentropy(random, 32) != 0) { return str8_zero(); }
  String8 id = hex_from_bytes(t->arena, random, 32);
  MemoryZero(random, 32);

  u64 now    = os_now_microseconds();
  u64 expiry = now + duration_us;

  String8 result = str8_zero();
  MutexScope(t->mutex)
  {
    for(u64 i = 0; i < t->capacity; i += 1)
    {
      Session *s = &t->slots[i];
      if(s->id.size > 0 && now >= s->expiry_us) MemoryZeroStruct(s);
      if(s->id.size == 0)
      {
        s->id        = id;
        s->username  = str8_copy(t->arena, username);
        s->expiry_us = expiry;
        result = id;
        break;
      }
    }
  }

  return result;
}

internal b32
session_validate(SessionTable *t, String8 id, String8 *out_username)
{
  if(id.size == 0) { return 0; }

  u64 now = os_now_microseconds();
  b32 valid = 0;

  MutexScope(t->mutex)
  {
    for(u64 i = 0; i < t->capacity; i += 1)
    {
      Session *s = &t->slots[i];
      if(str8_match(s->id, id, 0) && now < s->expiry_us)
      {
        valid = 1;
        if(out_username) *out_username = s->username;
        break;
      }
    }
  }

  return valid;
}

internal void
session_delete(SessionTable *t, String8 id)
{
  MutexScope(t->mutex)
  {
    for(u64 i = 0; i < t->capacity; i += 1)
    {
      if(str8_match(t->slots[i].id, id, 0))
      {
        MemoryZeroStruct(&t->slots[i]);
        break;
      }
    }
  }
}

////////////////////////////////
//~ Invite Table

internal InviteTable *
invite_table_alloc(Arena *arena, u64 capacity)
{
  InviteTable *t = push_array(arena, InviteTable, 1);
  t->mutex = mutex_alloc();
  t->arena = arena;
  t->slots = push_array(arena, Invite, capacity);
  t->capacity = capacity;
  return t;
}

internal String8
invite_create(InviteTable *t, u64 duration_us)
{
  u8 random[32];
  if(getentropy(random, 32) != 0) { return str8_zero(); }
  String8 token = hex_from_bytes(t->arena, random, 32);
  MemoryZero(random, 32);

  u64 now    = os_now_microseconds();
  u64 expiry = now + duration_us;

  String8 result = str8_zero();
  MutexScope(t->mutex)
  {
    for(u64 i = 0; i < t->capacity; i += 1)
    {
      Invite *inv = &t->slots[i];
      if(inv->token.size > 0 && (now >= inv->expiry_us || inv->used)) MemoryZeroStruct(inv);
      if(inv->token.size == 0)
      {
        inv->token = token;
        inv->expiry_us = expiry;
        result = token;
        break;
      }
    }
  }

  return result;
}

internal b32
invite_validate(InviteTable *t, String8 token)
{
  fprintf(stderr, "[DEBUG] invite_validate: entry\n"); fflush(stderr);
  if(token.size == 0) { return 0; }

  fprintf(stderr, "[DEBUG] invite_validate: calling os_now_microseconds\n"); fflush(stderr);
  u64 now = os_now_microseconds();
  fprintf(stderr, "[DEBUG] invite_validate: got time=%llu\n", now); fflush(stderr);
  b32 valid = 0;

  fprintf(stderr, "[DEBUG] invite_validate: about to acquire mutex\n"); fflush(stderr);
  MutexScope(t->mutex)
  {
    fprintf(stderr, "[DEBUG] invite_validate: mutex acquired, capacity=%llu\n", t->capacity); fflush(stderr);
    for(u64 i = 0; i < t->capacity; i += 1)
    {
      Invite *inv = &t->slots[i];
      if(str8_match(inv->token, token, 0) && !inv->used && now < inv->expiry_us)
      {
        valid = 1;
        break;
      }
    }
    fprintf(stderr, "[DEBUG] invite_validate: loop complete, valid=%d\n", valid); fflush(stderr);
  }
  fprintf(stderr, "[DEBUG] invite_validate: mutex released\n"); fflush(stderr);

  return valid;
}

internal b32
invite_consume(InviteTable *t, String8 token, String8 username)
{
  if(token.size == 0) { return 0; }

  u64 now = os_now_microseconds();
  b32 consumed = 0;

  MutexScope(t->mutex)
  {
    for(u64 i = 0; i < t->capacity; i += 1)
    {
      Invite *inv = &t->slots[i];
      if(str8_match(inv->token, token, 0) && !inv->used && now < inv->expiry_us)
      {
        inv->used = 1;
        inv->username = str8_copy(t->arena, username);
        consumed = 1;
        break;
      }
    }
  }

  return consumed;
}

////////////////////////////////
//~ Challenge Table

internal ChallengeTable *
challenge_table_alloc(Arena *arena, u64 capacity)
{
  ChallengeTable *t = push_array(arena, ChallengeTable, 1);
  t->mutex = mutex_alloc();
  t->arena = arena;
  t->slots = push_array(arena, Challenge, capacity);
  t->capacity = capacity;
  return t;
}

internal String8
challenge_create(ChallengeTable *t, u8 out[32])
{
  if(getentropy(out, 32) != 0) return str8_zero();

  u64 now = os_now_microseconds();
  u64 expiry = now + 10 * 1000000;

  String8 result = str8_zero();
  MutexScope(t->mutex)
  {
    for(u64 i = 0; i < t->capacity; i += 1)
    {
      Challenge *c = &t->slots[i];
      if(c->expiry_us > 0 && now >= c->expiry_us) MemoryZeroStruct(c);
      if(c->expiry_us == 0)
      {
        MemoryCopy(c->bytes, out, 32);
        c->expiry_us = expiry;
        result = str8_base64_encode(t->arena, str8(out, 32));
        break;
      }
    }
  }

  return result;
}

internal b32
challenge_verify(ChallengeTable *t, u8 bytes[32])
{
  u64 now = os_now_microseconds();
  b32 valid = 0;

  MutexScope(t->mutex)
  {
    for(u64 i = 0; i < t->capacity; i += 1)
    {
      Challenge *c = &t->slots[i];
      if(c->expiry_us > 0 && now < c->expiry_us && MemoryCompare(c->bytes, bytes, 32) == 0)
      {
        valid = 1;
        MemoryZeroStruct(c);
        break;
      }
    }
  }

  return valid;
}

////////////////////////////////
//~ 9P Bridge

internal Auth9P *
auth9p_connect(Arena *arena, String8 socket_path)
{
  Temp scratch = scratch_begin(&arena, 1);
  OS_Handle socket = dial9p_connect(scratch.arena, socket_path, str8_lit("unix"), str8_lit("9pfs"));
  if(socket.u64[0] == 0)
  {
    scratch_end(scratch);
    return 0;
  }

  Client9P *client = client9p_mount(arena, socket.u64[0], str8_zero(), str8_zero(), str8_lit("/"), 0);
  if(client == 0)
  {
    close(socket.u64[0]);
    scratch_end(scratch);
    return 0;
  }

  Auth9P *auth = push_array(arena, Auth9P, 1);
  auth->client = client;
  auth->socket = socket;
  auth->mutex = mutex_alloc();

  scratch_end(scratch);
  return auth;
}

internal void
auth9p_disconnect(Auth9P *auth)
{
  if(auth && auth->client)
  {
    Arena *temp = arena_alloc();
    client9p_unmount(temp, auth->client);
    arena_release(temp);
  }
  if(auth) close(auth->socket.u64[0]);
}

internal b32
auth9p_register(Arena *arena, Auth9P *auth, String8 username, String8 auth_id,
                String8 cred_id, String8 pubkey, String8 *error)
{
  if(!auth || !auth->client)
  {
    *error = str8_lit("not connected");
    return 0;
  }

  Temp scratch = scratch_begin(&arena, 1);
  b32 ok = 0;

  MutexScope(auth->mutex)
  {
    String8 cred_hex = hex_from_bytes(scratch.arena, cred_id.str, cred_id.size);
    String8 pub_hex = hex_from_bytes(scratch.arena, pubkey.str, pubkey.size);
    String8 cmd = str8f(scratch.arena, "register user=%S auth-id=%S proto=fido2 credential-id=%S pubkey=%S",
                        username, auth_id, cred_hex, pub_hex);

    ClientFid9P *fid = client9p_open(scratch.arena, auth->client, str8_lit("ctl"), P9_OpenFlag_Write);
    if(!fid)
    {
      *error = str8_lit("open /ctl failed");
      goto done;
    }

    s64 n = client9p_fid_pwrite(scratch.arena, fid, cmd.str, cmd.size, 0);
    if(n != (s64)cmd.size)
    {
      *error = str8_lit("write failed");
      client9p_fid_close(scratch.arena, fid);
      goto done;
    }

    client9p_fid_close(scratch.arena, fid);
    ok = 1;
  }

done:
  scratch_end(scratch);
  return ok;
}

internal b32
auth9p_verify(Arena *arena, Auth9P *auth, String8 username, String8 auth_id,
              u8 *auth_data, u64 auth_data_len, u8 *sig, u64 sig_len, String8 *error)
{
  if(!auth || !auth->client)
  {
    *error = str8_lit("not connected");
    return 0;
  }

  Temp scratch = scratch_begin(&arena, 1);
  b32 ok = 0;

  MutexScope(auth->mutex)
  {
    ClientFid9P *fid = client9p_open(scratch.arena, auth->client, str8_lit("rpc"),
                                     P9_OpenFlag_Read | P9_OpenFlag_Write);
    if(!fid)
    {
      *error = str8_lit("open /rpc failed");
      goto done;
    }

    String8 start = str8f(scratch.arena, "start user=%S auth-id=%S proto=fido2 role=server", username, auth_id);
    s64 n = client9p_fid_pwrite(scratch.arena, fid, start.str, start.size, 0);
    if(n != (s64)start.size)
    {
      *error = str8_lit("start failed");
      client9p_fid_close(scratch.arena, fid);
      goto done;
    }

    u8 challenge[36];
    n = client9p_fid_pread(scratch.arena, fid, challenge, 36, 0);
    if(n != 36)
    {
      *error = str8_lit("read challenge failed");
      client9p_fid_close(scratch.arena, fid);
      goto done;
    }

    u64 wire_len = 8 + 8 + auth_data_len + sig_len;
    u8 *wire = push_array(scratch.arena, u8, wire_len);
    write_u64(wire, 2);
    write_u64(wire + 8, auth_data_len);
    MemoryCopy(wire + 16, auth_data, auth_data_len);
    MemoryCopy(wire + 16 + auth_data_len, sig, sig_len);

    n = client9p_fid_pwrite(scratch.arena, fid, wire, wire_len, 0);
    if(n != (s64)wire_len)
    {
      *error = str8_lit("write assertion failed");
      client9p_fid_close(scratch.arena, fid);
      goto done;
    }

    u8 result[512];
    n = client9p_fid_pread(scratch.arena, fid, result, 512, 0);
    if(n <= 0)
    {
      *error = str8_lit("read result failed");
      client9p_fid_close(scratch.arena, fid);
      goto done;
    }

    String8 result_str = str8(result, n);
    if(str8_match(result_str, str8_lit("done"), 0)) ok = 1;
    else *error = str8_copy(arena, result_str);

    client9p_fid_close(scratch.arena, fid);
  }

done:
  scratch_end(scratch);
  return ok;
}

////////////////////////////////
//~ HTTP Helpers

internal HTTPRequest
http_parse(Arena *arena, String8 data)
{
  HTTPRequest req = {0};

  u64 line_end = 0;
  for(u64 i = 0; i < data.size; i += 1)
  {
    if(data.str[i] == '\n')
    {
      line_end = i;
      break;
    }
  }

  String8 line = str8_substr(data, rng_1u64(0, line_end));
  String8List parts = str8_split(arena, line, (u8 *)" ", 1, 0);
  if(parts.node_count >= 2)
  {
    req.method = parts.first->string;
    String8 path_query = parts.first->next->string;

    for(u64 i = 0; i < path_query.size; i += 1)
    {
      if(path_query.str[i] == '?')
      {
        req.path = str8_prefix(path_query, i);
        req.query = str8_skip(path_query, i + 1);
        goto parsed_path;
      }
    }
    req.path = path_query;
  parsed_path:;
  }

  u64 pos = line_end + 1;
  for(u64 i = pos; i + 1 < data.size; i += 1)
  {
    if(data.str[i] == '\r' && data.str[i + 1] == '\n')
    {
      String8 hdr = str8_substr(data, rng_1u64(pos, i));
      if(str8_match(str8_prefix(hdr, 7), str8_lit("Cookie:"), 0))
        req.cookie = str8_skip_chop_whitespace(str8_skip(hdr, 7));
      else if(str8_match(str8_prefix(hdr, 5), str8_lit("Host:"), 0))
        req.host = str8_skip_chop_whitespace(str8_skip(hdr, 5));

      pos = i + 2;
      if(i + 3 < data.size && data.str[i + 2] == '\r' && data.str[i + 3] == '\n')
      {
        req.body = str8_skip(data, i + 4);
        break;
      }
    }
  }

  return req;
}

internal String8
http_query_param(Arena *arena, String8 query, String8 name)
{
  String8List params = str8_split(arena, query, (u8 *)"&", 1, 0);
  for(String8Node *n = params.first; n; n = n->next)
  {
    String8List kv = str8_split(arena, n->string, (u8 *)"=", 1, 0);
    if(kv.node_count == 2 && str8_match(kv.first->string, name, 0))
      return kv.first->next->string;
  }
  return str8_zero();
}

internal String8
cookie_get(Arena *arena, String8 cookie_header, String8 name)
{
  String8List cookies = str8_split(arena, cookie_header, (u8 *)"; ", 2, 0);
  for(String8Node *n = cookies.first; n; n = n->next)
  {
    String8List kv = str8_split(arena, n->string, (u8 *)"=", 1, 0);
    if(kv.node_count == 2 && str8_match(kv.first->string, name, 0))
      return kv.first->next->string;
  }
  return str8_zero();
}

internal String8
cookie_set(Arena *arena, String8 name, String8 value, String8 domain, u64 max_age)
{
  if(domain.size > 0)
    return str8f(arena, "Set-Cookie: %S=%S; HttpOnly; SameSite=Lax; Path=/; Domain=%S; Max-Age=%llu\r\n",
                 name, value, domain, max_age);
  return str8f(arena, "Set-Cookie: %S=%S; HttpOnly; SameSite=Lax; Path=/; Max-Age=%llu\r\n",
               name, value, max_age);
}

internal void
http_send(OS_Handle sock, u16 code, String8 headers, String8 type, String8 body)
{
  Temp scratch = scratch_begin(0, 0);
  String8 status = str8_lit("OK");
  if(code == 302) status = str8_lit("Found");
  else if(code == 400) status = str8_lit("Bad Request");
  else if(code == 401) status = str8_lit("Unauthorized");
  else if(code == 403) status = str8_lit("Forbidden");
  else if(code == 404) status = str8_lit("Not Found");
  else if(code == 500) status = str8_lit("Internal Server Error");

  String8 response = str8f(scratch.arena,
                           "HTTP/1.1 %u %S\r\n%SContent-Type: %S\r\nContent-Length: %llu\r\n\r\n%S",
                           code, status, headers, type, body.size, body);

  write(sock.u64[0], response.str, response.size);
  scratch_end(scratch);
}

////////////////////////////////
//~ WebAuthn

internal b32
webauthn_parse_attestation(Arena *arena, String8 json, String8 *cred_id, String8 *pubkey, String8 *error)
{
  Temp scratch = scratch_begin(&arena, 1);
  JSON_Value *root = json_parse(scratch.arena, json);
  if(!root || root->kind != JSON_ValueKind_Object)
  {
    *error = str8_lit("invalid json");
    goto fail;
  }

  JSON_Value *raw_id = json_object_get(root, str8_lit("rawId"));
  if(!raw_id || raw_id->kind != JSON_ValueKind_String)
  {
    *error = str8_lit("missing rawId");
    goto fail;
  }

  String8 cred_bytes = str8_base64_decode(scratch.arena, raw_id->string);
  if(cred_bytes.size == 0)
  {
    *error = str8_lit("invalid rawId");
    goto fail;
  }

  JSON_Value *response = json_object_get(root, str8_lit("response"));
  if(!response || response->kind != JSON_ValueKind_Object)
  {
    *error = str8_lit("missing response");
    goto fail;
  }

  JSON_Value *attestation = json_object_get(response, str8_lit("attestationObject"));
  if(!attestation || attestation->kind != JSON_ValueKind_String)
  {
    *error = str8_lit("missing attestationObject");
    goto fail;
  }

  String8 attest_bytes = str8_base64_decode(scratch.arena, attestation->string);
  if(attest_bytes.size == 0)
  {
    *error = str8_lit("invalid attestationObject");
    goto fail;
  }

  *cred_id = str8_copy(arena, cred_bytes);
  *pubkey = str8_copy(arena, attest_bytes);
  scratch_end(scratch);
  return 1;

fail:
  scratch_end(scratch);
  return 0;
}

internal b32
webauthn_parse_assertion(Arena *arena, String8 json, u8 *auth_data, u64 *auth_data_len,
                         u8 *sig, u64 *sig_len, String8 *error)
{
  Temp scratch = scratch_begin(&arena, 1);
  JSON_Value *root = json_parse(scratch.arena, json);
  if(!root || root->kind != JSON_ValueKind_Object)
  {
    *error = str8_lit("invalid json");
    goto fail;
  }

  JSON_Value *response = json_object_get(root, str8_lit("response"));
  if(!response || response->kind != JSON_ValueKind_Object)
  {
    *error = str8_lit("missing response");
    goto fail;
  }

  JSON_Value *auth_data_val = json_object_get(response, str8_lit("authenticatorData"));
  if(!auth_data_val || auth_data_val->kind != JSON_ValueKind_String)
  {
    *error = str8_lit("missing authenticatorData");
    goto fail;
  }

  String8 ad_bytes = str8_base64_decode(scratch.arena, auth_data_val->string);
  if(ad_bytes.size == 0 || ad_bytes.size > 256)
  {
    *error = str8_lit("invalid authenticatorData");
    goto fail;
  }

  JSON_Value *sig_val = json_object_get(response, str8_lit("signature"));
  if(!sig_val || sig_val->kind != JSON_ValueKind_String)
  {
    *error = str8_lit("missing signature");
    goto fail;
  }

  String8 sig_bytes = str8_base64_decode(scratch.arena, sig_val->string);
  if(sig_bytes.size == 0 || sig_bytes.size > 256)
  {
    *error = str8_lit("invalid signature");
    goto fail;
  }

  MemoryCopy(auth_data, ad_bytes.str, ad_bytes.size);
  *auth_data_len = ad_bytes.size;
  MemoryCopy(sig, sig_bytes.str, sig_bytes.size);
  *sig_len = sig_bytes.size;
  scratch_end(scratch);
  return 1;

fail:
  scratch_end(scratch);
  return 0;
}

////////////////////////////////
//~ Handlers

internal void
handle_auth_check(Arena *arena, HTTPRequest *req, OS_Handle sock)
{
  String8 sid = cookie_get(arena, req->cookie, str8_lit("session"));
  if(session_validate(g_sessions, sid, 0))
    http_send(sock, 200, str8_zero(), str8_lit("text/plain"), str8_lit("OK"));
  else
    http_send(sock, 401, str8_zero(), str8_lit("text/plain"), str8_lit("Unauthorized"));
}

internal void
handle_login_page(Arena *arena, HTTPRequest *req, OS_Handle sock)
{
  fprintf(stderr, "[DEBUG] login: creating challenge\n"); fflush(stderr);
  u8 challenge[32];
  String8 challenge_b64 = challenge_create(g_challenges, challenge);
  if(challenge_b64.size == 0)
  {
    http_send(sock, 500, str8_zero(), str8_lit("text/plain"), str8_lit("Challenge generation failed"));
    return;
  }

  fprintf(stderr, "[DEBUG] login: formatting options\n"); fflush(stderr);
  String8 options = str8f(arena,
    "{\"challenge\":\"%S\",\"rpId\":\"%S\",\"userVerification\":\"required\",\"timeout\":60000}",
    challenge_b64, g_rp_id);

  fprintf(stderr, "[DEBUG] login: formatting HTML\n"); fflush(stderr);
  String8 html = str8f(arena,
    "<!DOCTYPE html><html><head><title>Login</title></head><body>"
    "<h1>Login</h1><input id=\"u\" placeholder=\"Username\"/>"
    "<button onclick=\"login()\">Login with Biometrics</button><script>"
    "async function login(){"
    "const u=document.getElementById('u').value;if(!u){alert('Enter username');return;}"
    "const o=%S;o.challenge=Uint8Array.from(atob(o.challenge),c=>c.charCodeAt(0));"
    "const c=await navigator.credentials.get({publicKey:o});"
    "const r={username:u,response:{"
    "authenticatorData:btoa(String.fromCharCode(...new Uint8Array(c.response.authenticatorData))),"
    "signature:btoa(String.fromCharCode(...new Uint8Array(c.response.signature)))"
    "}};"
    "const res=await fetch('/login/verify',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(r)});"
    "if(res.ok)window.location='/';else alert('Login failed');"
    "}</script></body></html>", options);
  fprintf(stderr, "[DEBUG] login: HTML formatted (size=%llu)\n", html.size); fflush(stderr);

  fprintf(stderr, "[DEBUG] login: sending HTTP response\n"); fflush(stderr);
  http_send(sock, 200, str8_zero(), str8_lit("text/html"), html);
  fprintf(stderr, "[DEBUG] login: HTTP response sent\n"); fflush(stderr);
}

internal void
handle_login_verify(Arena *arena, HTTPRequest *req, OS_Handle sock)
{
  u8 auth_data[256];
  u64 auth_data_len = 0;
  u8 sig[256];
  u64 sig_len = 0;
  String8 error = str8_zero();

  if(!webauthn_parse_assertion(arena, req->body, auth_data, &auth_data_len, sig, &sig_len, &error))
  {
    log_infof("assertion parse failed: %S", error);
    http_send(sock, 401, str8_zero(), str8_lit("text/plain"), error);
    return;
  }

  JSON_Value *root = json_parse(arena, req->body);
  if(!root || root->kind != JSON_ValueKind_Object)
  {
    http_send(sock, 400, str8_zero(), str8_lit("text/plain"), str8_lit("Invalid JSON"));
    return;
  }

  JSON_Value *username_val = json_object_get(root, str8_lit("username"));
  if(!username_val || username_val->kind != JSON_ValueKind_String)
  {
    http_send(sock, 400, str8_zero(), str8_lit("text/plain"), str8_lit("Missing username"));
    return;
  }

  String8 username = username_val->string;

  Auth9P *auth = auth9p_connect(arena, g_auth_socket);
  if(!auth)
  {
    http_send(sock, 500, str8_zero(), str8_lit("text/plain"), str8_lit("9auth connection failed"));
    return;
  }

  b32 verified = auth9p_verify(arena, auth, username, g_rp_id, auth_data, auth_data_len, sig, sig_len, &error);
  auth9p_disconnect(auth);

  if(verified)
  {
    String8 sid = session_create(g_sessions, username, g_session_duration_us);
    if(sid.size > 0)
    {
      String8 cookie = cookie_set(arena, str8_lit("session"), sid, str8_zero(), g_session_duration_us / 1000000);
      http_send(sock, 200, cookie, str8_lit("text/plain"), str8_lit("OK"));
    }
    else
    {
      http_send(sock, 500, str8_zero(), str8_lit("text/plain"), str8_lit("Session creation failed"));
    }
  }
  else
  {
    log_infof("verification failed: %S", error);
    http_send(sock, 401, str8_zero(), str8_lit("text/plain"), str8_lit("Verification failed"));
  }
}

internal void
handle_logout(Arena *arena, HTTPRequest *req, OS_Handle sock)
{
  String8 sid = cookie_get(arena, req->cookie, str8_lit("session"));
  session_delete(g_sessions, sid);

  String8 clear = cookie_set(arena, str8_lit("session"), str8_lit(""), str8_zero(), 0);
  String8 redir = str8_lit("Location: /login\r\n");
  String8 headers = str8_cat(arena, clear, redir);
  http_send(sock, 302, headers, str8_lit("text/plain"), str8_lit("Logged out"));
}

internal void
handle_register_page(Arena *arena, HTTPRequest *req, OS_Handle sock)
{
  fprintf(stderr, "[DEBUG] register: parsing invite token\n"); fflush(stderr);
  String8 token = http_query_param(arena, req->query, str8_lit("invite"));

  fprintf(stderr, "[DEBUG] register: validating invite (token size=%llu)\n", token.size); fflush(stderr);
  if(!invite_validate(g_invites, token))
  {
    fprintf(stderr, "[DEBUG] register: invalid invite\n"); fflush(stderr);
    http_send(sock, 401, str8_zero(), str8_lit("text/plain"), str8_lit("Invalid invite"));
    return;
  }

  fprintf(stderr, "[DEBUG] register: creating challenge\n"); fflush(stderr);
  u8 challenge[32];
  String8 challenge_b64 = challenge_create(g_challenges, challenge);

  fprintf(stderr, "[DEBUG] register: generating user_id\n"); fflush(stderr);
  u8 user_id[64];
  getentropy(user_id, 64);
  fprintf(stderr, "[DEBUG] register: encoding user_id\n"); fflush(stderr);
  String8 user_id_b64 = str8_base64_encode(arena, str8(user_id, 64));

  fprintf(stderr, "[DEBUG] register: formatting options\n"); fflush(stderr);
  String8 options = str8f(arena,
    "{\"challenge\":\"%S\",\"rp\":{\"id\":\"%S\",\"name\":\"%S\"},"
    "\"user\":{\"id\":\"%S\",\"name\":\"user\",\"displayName\":\"user\"},"
    "\"pubKeyCredParams\":[{\"type\":\"public-key\",\"alg\":-7}],"
    "\"authenticatorSelection\":{\"userVerification\":\"required\"},\"timeout\":60000,\"attestation\":\"none\"}",
    challenge_b64, g_rp_id, g_rp_name, user_id_b64);

  fprintf(stderr, "[DEBUG] register: formatting HTML\n"); fflush(stderr);
  String8 html = str8f(arena,
    "<!DOCTYPE html><html><head><title>Register</title></head><body>"
    "<h1>Register</h1><input id=\"u\" placeholder=\"Username\"/>"
    "<button onclick=\"register()\">Register with Security Key</button><script>"
    "async function register(){"
    "const u=document.getElementById('u').value;if(!u){alert('Enter username');return;}"
    "const o=%S;o.user.name=u;o.user.displayName=u;"
    "o.challenge=Uint8Array.from(atob(o.challenge),c=>c.charCodeAt(0));"
    "o.user.id=Uint8Array.from(atob(o.user.id),c=>c.charCodeAt(0));"
    "const c=await navigator.credentials.create({publicKey:o});"
    "const a={rawId:btoa(String.fromCharCode(...new Uint8Array(c.rawId))),"
    "response:{attestationObject:btoa(String.fromCharCode(...new Uint8Array(c.response.attestationObject)))},"
    "username:u,invite:'%S'};"
    "const res=await fetch('/register/complete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(a)});"
    "if(res.ok)window.location='/login';else alert('Registration failed');"
    "}</script></body></html>", options, token);
  fprintf(stderr, "[DEBUG] register: HTML formatted (size=%llu)\n", html.size); fflush(stderr);

  fprintf(stderr, "[DEBUG] register: sending HTTP response\n"); fflush(stderr);
  http_send(sock, 200, str8_zero(), str8_lit("text/html"), html);
  fprintf(stderr, "[DEBUG] register: HTTP response sent\n"); fflush(stderr);
}

internal void
handle_register_complete(Arena *arena, HTTPRequest *req, OS_Handle sock)
{
  JSON_Value *root = json_parse(arena, req->body);
  if(!root || root->kind != JSON_ValueKind_Object)
  {
    http_send(sock, 400, str8_zero(), str8_lit("text/plain"), str8_lit("Invalid JSON"));
    return;
  }

  JSON_Value *username_val = json_object_get(root, str8_lit("username"));
  JSON_Value *invite_val = json_object_get(root, str8_lit("invite"));
  if(!username_val || !invite_val)
  {
    http_send(sock, 400, str8_zero(), str8_lit("text/plain"), str8_lit("Missing fields"));
    return;
  }

  String8 username = username_val->string;
  String8 token = invite_val->string;

  if(!invite_consume(g_invites, token, username))
  {
    http_send(sock, 401, str8_zero(), str8_lit("text/plain"), str8_lit("Invalid invite"));
    return;
  }

  String8 cred_id = str8_zero();
  String8 pubkey = str8_zero();
  String8 error = str8_zero();

  if(!webauthn_parse_attestation(arena, req->body, &cred_id, &pubkey, &error))
  {
    log_infof("attestation parse failed: %S", error);
    http_send(sock, 400, str8_zero(), str8_lit("text/plain"), error);
    return;
  }

  Auth9P *auth = auth9p_connect(arena, g_auth_socket);
  if(!auth)
  {
    http_send(sock, 500, str8_zero(), str8_lit("text/plain"), str8_lit("9auth connection failed"));
    return;
  }

  b32 ok = auth9p_register(arena, auth, username, g_rp_id, cred_id, pubkey, &error);
  auth9p_disconnect(auth);

  if(ok)
    http_send(sock, 200, str8_zero(), str8_lit("text/plain"), str8_lit("Registration successful"));
  else
  {
    log_infof("registration failed: %S", error);
    http_send(sock, 500, str8_zero(), str8_lit("text/plain"), error);
  }
}

////////////////////////////////
//~ Connection Handler

internal void
handle_connection(OS_Handle sock)
{
  Arena *arena = arena_alloc();
  u8 *buf = push_array(arena, u8, MB(1));
  u64 total = 0;

  for(;;)
  {
    ssize_t n = read(sock.u64[0], buf + total, KB(16));
    if(n <= 0) { break; }
    total += (u64)n;

    if(total >= 4)
    {
      for(u64 i = 3; i < total; i += 1)
      {
        if(buf[i - 3] == '\r' && buf[i - 2] == '\n' && buf[i - 1] == '\r' && buf[i] == '\n') { goto done; }
      }
    }
    if(total >= MB(1)) { break; }
  }

done:;
  HTTPRequest req = http_parse(arena, str8(buf, total));

  if(str8_match(req.path, str8_lit("/auth"), 0))
    handle_auth_check(arena, &req, sock);
  else if(str8_match(req.path, str8_lit("/login"), 0) && str8_match(req.method, str8_lit("GET"), 0))
    handle_login_page(arena, &req, sock);
  else if(str8_match(req.path, str8_lit("/login/verify"), 0) && str8_match(req.method, str8_lit("POST"), 0))
    handle_login_verify(arena, &req, sock);
  else if(str8_match(req.path, str8_lit("/logout"), 0) && str8_match(req.method, str8_lit("POST"), 0))
    handle_logout(arena, &req, sock);
  else if(str8_match(req.path, str8_lit("/register"), 0) && str8_match(req.method, str8_lit("GET"), 0))
    handle_register_page(arena, &req, sock);
  else if(str8_match(req.path, str8_lit("/register/complete"), 0) && str8_match(req.method, str8_lit("POST"), 0))
    handle_register_complete(arena, &req, sock);
  else
    http_send(sock, 404, str8_zero(), str8_lit("text/plain"), str8_lit("Not Found"));

  close(sock.u64[0]);
  arena_release(arena);
}

////////////////////////////////
//~ Entry Point

internal b32
check_group_membership(String8 group_name)
{
  gid_t gid = getgid();
  gid_t egid = getegid();

  int ngroups = getgroups(0, 0);
  if(ngroups < 0) { return 0; }

  gid_t *groups = malloc(ngroups * sizeof(gid_t));
  if(!groups) { return 0; }

  if(getgroups(ngroups, groups) < 0)
  {
    free(groups);
    return 0;
  }

  Temp scratch = scratch_begin(0, 0);
  char *group_cstr = (char *)push_array(scratch.arena, u8, group_name.size + 1);
  MemoryCopy(group_cstr, group_name.str, group_name.size);
  group_cstr[group_name.size] = 0;

  struct group *gr = getgrnam(group_cstr);
  scratch_end(scratch);

  if(!gr)
  {
    free(groups);
    return 0;
  }

  gid_t target_gid = gr->gr_gid;
  b32 member = (gid == target_gid || egid == target_gid);

  for(int i = 0; i < ngroups && !member; i += 1)
  {
    if(groups[i] == target_gid) member = 1;
  }

  free(groups);
  return member;
}

internal void
run_invite_command(Arena *arena)
{
  if(!check_group_membership(str8_lit("9auth")))
  {
    fprintf(stderr, "Error: You must be in the '9auth' group to generate invites\n");
    fprintf(stderr, "Ask your system administrator to add you to the group\n");
    exit(1);
  }

  fprintf(stderr, "Error: invite command requires a running webauth daemon\n");
  fprintf(stderr, "TODO: Implement IPC to communicate with running daemon\n");
  fprintf(stderr, "\nFor now, use: webauth --test-invite=true\n");
  exit(1);
}

internal void
entry_point(CmdLine *cmd_line)
{
  Arena *arena = arena_alloc();

  String8 mode = cmd_line_string(cmd_line, str8_lit("mode"));
  if(str8_match(mode, str8_lit("invite"), 0))
  {
    run_invite_command(arena);
    return;
  }

  String8 rp_id = cmd_line_string(cmd_line, str8_lit("rp-id"));
  String8 rp_name = cmd_line_string(cmd_line, str8_lit("rp-name"));
  String8 socket = cmd_line_string(cmd_line, str8_lit("auth-socket"));
  String8 session_days = cmd_line_string(cmd_line, str8_lit("session-duration"));
  String8 invite_hours = cmd_line_string(cmd_line, str8_lit("invite-duration"));
  String8 port_str = cmd_line_string(cmd_line, str8_lit("listen-port"));

  g_rp_id = rp_id.size > 0 ? str8_copy(arena, rp_id) : str8_lit("auth.dargs.dev");
  g_rp_name = rp_name.size > 0 ? str8_copy(arena, rp_name) : str8_lit("Auth");
  g_auth_socket = socket.size > 0 ? str8_copy(arena, socket) : str8_lit("/run/9auth/socket");
  g_session_duration_us = (session_days.size > 0 ? u64_from_str8(session_days, 10) : 7) * 24 * 60 * 60 * 1000000;
  g_invite_duration_us = (invite_hours.size > 0 ? u64_from_str8(invite_hours, 10) : 24) * 60 * 60 * 1000000;
  g_listen_port = port_str.size > 0 ? (u16)u64_from_str8(port_str, 10) : 8080;

  g_sessions = session_table_alloc(arena, 4096);
  g_invites = invite_table_alloc(arena, 256);
  g_challenges = challenge_table_alloc(arena, 1024);

  String8 test_mode = cmd_line_string(cmd_line, str8_lit("test-invite"));
  if(str8_match(test_mode, str8_lit("true"), 0))
  {
    String8 token = invite_create(g_invites, g_invite_duration_us);
    if(token.size > 0)
    {
      fprintf(stderr, "\n");
      fprintf(stderr, "==================== TEST INVITE ====================\n");
      fprintf(stderr, "  http://localhost:%u/register?invite=%.*s\n", g_listen_port, (int)token.size, token.str);
      fprintf(stderr, "=====================================================\n");
      fprintf(stderr, "\n");
      fflush(stderr);
    }
  }

  log_info(str8_lit("webauth starting"));
  log_infof("  RP ID: %S", g_rp_id);
  log_infof("  Socket: %S", g_auth_socket);
  log_infof("  Port: %u", g_listen_port);

  OS_Handle listen = os_socket_listen_tcp(g_listen_port);
  if(listen.u64[0] == 0)
  {
    log_error(str8_lit("Failed to create listening socket"));
    return;
  }

  log_infof("Listening on port %u", g_listen_port);

  for(;;)
  {
    OS_Handle client = os_socket_accept(listen);
    if(client.u64[0] != 0) handle_connection(client);
  }

  close(listen.u64[0]);
}
