#ifndef ACME_CLIENT_H
#define ACME_CLIENT_H

////////////////////////////////
//~ ACME Client Types

typedef struct URL_Parts URL_Parts;
struct URL_Parts
{
	String8 host;
	String8 path;
	u16 port;
};

typedef struct ACME_Directory ACME_Directory;
struct ACME_Directory
{
	String8 new_nonce_url;
	String8 new_account_url;
	String8 new_order_url;
	String8 revoke_cert_url;
	String8 key_change_url;
};

typedef struct ACME_Client ACME_Client;
struct ACME_Client
{
	Arena *arena;
	SSL_CTX *ssl_ctx;
	String8 directory_url;
	ACME_Directory directory;
	String8 account_url;
	String8 nonce;
	EVP_PKEY *account_key;
};

////////////////////////////////
//~ URL Parsing

internal URL_Parts url_parse_https(String8 url);

////////////////////////////////
//~ HTTPS Requests

internal String8 acme_https_request(ACME_Client *client, Arena *arena, String8 host, u16 port, String8 method,
                                    String8 path, String8 body, String8 *out_location, String8 *out_nonce);

////////////////////////////////
//~ ACME Client Functions

internal String8 acme_get_nonce(ACME_Client *client);
internal ACME_Client *acme_client_alloc(Arena *arena, SSL_CTX *ssl_ctx, String8 directory_url, EVP_PKEY *account_key);

#endif // ACME_CLIENT_H
