#include <openssl/err.h>
#include <openssl/ssl.h>

// clang-format off
#include "base/inc.h"
#include "json/inc.h"
#include "http/inc.h"
#include "acme/inc.h"
#include "base/inc.c"
#include "json/inc.c"
#include "http/inc.c"
#include "acme/inc.c"
// clang-format on

////////////////////////////////
//~ Configuration Constants

read_only global u64 max_request_size = MB(1);
read_only global u64 max_header_size = KB(64);
read_only global u64 request_timeout_ms = 30000;

////////////////////////////////
//~ Backend Configuration

typedef struct Backend Backend;
struct Backend
{
	String8 path_prefix;
	String8 backend_host;
	u16 backend_port;
};

typedef struct ProxyConfig ProxyConfig;
struct ProxyConfig
{
	Backend *backends;
	u64 backend_count;
	u16 listen_port;
};

global ProxyConfig *proxy_config = 0;

////////////////////////////////
//~ ACME Challenge Management

typedef struct ACME_Challenge ACME_Challenge;
struct ACME_Challenge
{
	String8 token;
	String8 key_authorization;
	u64 timestamp;
};

typedef struct ACME_ChallengeTable ACME_ChallengeTable;
struct ACME_ChallengeTable
{
	Mutex mutex;
	Arena *arena;
	ACME_Challenge *challenges;
	u64 count;
	u64 capacity;
};

global ACME_ChallengeTable *acme_challenges = 0;

internal ACME_ChallengeTable *
acme_challenge_table_alloc(Arena *arena)
{
	ACME_ChallengeTable *table = push_array(arena, ACME_ChallengeTable, 1);
	table->arena = arena_alloc();
	table->mutex = mutex_alloc();
	table->capacity = 16;
	table->challenges = push_array(table->arena, ACME_Challenge, table->capacity);
	return table;
}

internal void
acme_challenge_add(ACME_ChallengeTable *table, String8 token, String8 key_auth)
{
	MutexScope(table->mutex)
	{
		ACME_Challenge *slot = 0;
		for(u64 i = 0; i < table->capacity; i += 1)
		{
			if(table->challenges[i].token.size == 0)
			{
				slot = &table->challenges[i];
				break;
			}
		}

		if(slot == 0)
		{
			u64 new_capacity = table->capacity * 2;
			ACME_Challenge *new_challenges = push_array(table->arena, ACME_Challenge, new_capacity);
			MemoryCopy(new_challenges, table->challenges, sizeof(ACME_Challenge) * table->capacity);
			table->challenges = new_challenges;
			slot = &table->challenges[table->capacity];
			table->capacity = new_capacity;
		}

		slot->token = str8_copy(table->arena, token);
		slot->key_authorization = str8_copy(table->arena, key_auth);
		slot->timestamp = os_now_microseconds();
		table->count += 1;
	}
}

internal String8
acme_challenge_lookup(ACME_ChallengeTable *table, String8 token)
{
	String8 result = str8_zero();
	MutexScope(table->mutex)
	{
		for(u64 i = 0; i < table->capacity; i += 1)
		{
			if(table->challenges[i].token.size > 0 && str8_match(table->challenges[i].token, token, 0))
			{
				result = table->challenges[i].key_authorization;
				break;
			}
		}
	}
	return result;
}

internal void
acme_challenge_remove(ACME_ChallengeTable *table, String8 token)
{
	MutexScope(table->mutex)
	{
		for(u64 i = 0; i < table->capacity; i += 1)
		{
			if(table->challenges[i].token.size > 0 && str8_match(table->challenges[i].token, token, 0))
			{
				MemoryZeroStruct(&table->challenges[i]);
				table->count -= 1;
				break;
			}
		}
	}
}

////////////////////////////////
//~ TLS Context Management

typedef struct TLS_Context TLS_Context;
struct TLS_Context
{
	SSL_CTX *ssl_ctx;
	Arena *arena;
	String8 cert_path;
	String8 key_path;
	b32 enabled;
};

global TLS_Context *tls_context = 0;
global SSL_CTX *acme_client_ssl_ctx = 0;

internal TLS_Context *
tls_context_alloc(Arena *arena, String8 cert_path, String8 key_path)
{
	TLS_Context *ctx = push_array(arena, TLS_Context, 1);
	ctx->arena = arena;
	ctx->cert_path = str8_copy(arena, cert_path);
	ctx->key_path = str8_copy(arena, key_path);

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();

	ctx->ssl_ctx = SSL_CTX_new(TLS_server_method());
	if(ctx->ssl_ctx == 0)
	{
		log_error(str8_lit("httpproxy: failed to create SSL context\n"));
		return ctx;
	}

	SSL_CTX_set_min_proto_version(ctx->ssl_ctx, TLS1_3_VERSION);

	Temp scratch = scratch_begin(&arena, 1);
	char cert_buf[4096];
	char key_buf[4096];

	if(cert_path.size >= sizeof(cert_buf) || key_path.size >= sizeof(key_buf))
	{
		log_error(str8_lit("httpproxy: certificate or key path too long\n"));
		SSL_CTX_free(ctx->ssl_ctx);
		ctx->ssl_ctx = 0;
		scratch_end(scratch);
		return ctx;
	}

	MemoryCopy(cert_buf, cert_path.str, cert_path.size);
	cert_buf[cert_path.size] = 0;
	MemoryCopy(key_buf, key_path.str, key_path.size);
	key_buf[key_path.size] = 0;

	if(SSL_CTX_use_certificate_chain_file(ctx->ssl_ctx, cert_buf) <= 0)
	{
		log_errorf("httpproxy: failed to load certificate from %S\n", cert_path);
		ERR_print_errors_fp(stderr);
		SSL_CTX_free(ctx->ssl_ctx);
		ctx->ssl_ctx = 0;
		scratch_end(scratch);
		return ctx;
	}

	if(SSL_CTX_use_PrivateKey_file(ctx->ssl_ctx, key_buf, SSL_FILETYPE_PEM) <= 0)
	{
		log_errorf("httpproxy: failed to load private key from %S\n", key_path);
		ERR_print_errors_fp(stderr);
		SSL_CTX_free(ctx->ssl_ctx);
		ctx->ssl_ctx = 0;
		scratch_end(scratch);
		return ctx;
	}

	if(!SSL_CTX_check_private_key(ctx->ssl_ctx))
	{
		log_error(str8_lit("httpproxy: private key does not match certificate\n"));
		SSL_CTX_free(ctx->ssl_ctx);
		ctx->ssl_ctx = 0;
		scratch_end(scratch);
		return ctx;
	}

	ctx->enabled = 1;
	scratch_end(scratch);
	return ctx;
}

////////////////////////////////
//~ JSON Web Signature for ACME

typedef struct ACME_AccountKey ACME_AccountKey;
struct ACME_AccountKey
{
	EVP_PKEY *pkey;
	Arena *arena;
	String8 key_path;
};

global ACME_AccountKey *acme_account_key = 0;

internal ACME_AccountKey *
acme_account_key_alloc(Arena *arena, String8 key_path)
{
	ACME_AccountKey *key = push_array(arena, ACME_AccountKey, 1);
	key->arena = arena;
	key->key_path = str8_copy(arena, key_path);

	Temp scratch = scratch_begin(&arena, 1);

	OS_Handle file = os_file_open(OS_AccessFlag_Read, key_path);
	if(!os_handle_match(file, os_handle_zero()))
	{
		OS_FileProperties props = os_properties_from_file(file);
		u8 *key_buffer = push_array(scratch.arena, u8, props.size);
		u64 bytes_read = os_file_read(file, rng_1u64(0, props.size), key_buffer);
		String8 key_data = str8(key_buffer, bytes_read);
		os_file_close(file);

		BIO *bio = BIO_new_mem_buf(key_data.str, (int)key_data.size);
		key->pkey = PEM_read_bio_PrivateKey(bio, 0, 0, 0);
		BIO_free(bio);

		if(key->pkey != 0)
		{
			log_infof("httpproxy: loaded existing ACME account key from %S\n", key_path);
			scratch_end(scratch);
			return key;
		}
		else
		{
			log_error(str8_lit("httpproxy: failed to parse existing ACME account key, generating new one\n"));
		}
	}

	log_infof("httpproxy: generating new ACME account key at %S\n", key_path);

	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, 0);
	if(ctx == 0)
	{
		log_error(str8_lit("httpproxy: failed to create EVP_PKEY_CTX\n"));
		scratch_end(scratch);
		return key;
	}

	if(EVP_PKEY_keygen_init(ctx) <= 0)
	{
		log_error(str8_lit("httpproxy: failed to initialize key generation\n"));
		EVP_PKEY_CTX_free(ctx);
		scratch_end(scratch);
		return key;
	}

	if(EVP_PKEY_keygen(ctx, &key->pkey) <= 0)
	{
		log_error(str8_lit("httpproxy: failed to generate key\n"));
		EVP_PKEY_CTX_free(ctx);
		scratch_end(scratch);
		return key;
	}

	EVP_PKEY_CTX_free(ctx);

	BIO *bio = BIO_new(BIO_s_mem());
	PEM_write_bio_PrivateKey(bio, key->pkey, 0, 0, 0, 0, 0);

	BUF_MEM *mem = 0;
	BIO_get_mem_ptr(bio, &mem);

	char path_buf[4096];
	if(key_path.size >= sizeof(path_buf))
	{
		log_error(str8_lit("httpproxy: key path too long\n"));
		BIO_free(bio);
		scratch_end(scratch);
		return key;
	}

	MemoryCopy(path_buf, key_path.str, key_path.size);
	path_buf[key_path.size] = 0;

	OS_Handle new_file = os_file_open(OS_AccessFlag_Write, key_path);
	os_file_write(new_file, rng_1u64(0, mem->length), mem->data);
	os_file_close(new_file);

	BIO_free(bio);

	log_infof("httpproxy: saved new ACME account key to %S\n", key_path);
	scratch_end(scratch);
	return key;
}

////////////////////////////////
//~ URL Parsing

typedef struct URL_Parts URL_Parts;
struct URL_Parts
{
	String8 host;
	String8 path;
	u16 port;
};

internal URL_Parts
url_parse_https(String8 url)
{
	URL_Parts result = {0};
	result.port = 443;

	if(!str8_match(str8_prefix(url, 8), str8_lit("https://"), 0))
	{
		return result;
	}

	String8 remainder = str8_skip(url, 8);
	u64 slash_pos = 0;
	for(u64 i = 0; i < remainder.size; i += 1)
	{
		if(remainder.str[i] == '/')
		{
			slash_pos = i;
			break;
		}
	}

	if(slash_pos > 0)
	{
		result.host = str8_prefix(remainder, slash_pos);
		result.path = str8_skip(remainder, slash_pos);
	}
	else
	{
		result.host = remainder;
		result.path = str8_lit("/");
	}

	return result;
}

////////////////////////////////
//~ ACME Client

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
	String8 directory_url;
	ACME_Directory directory;
	String8 account_url;
	String8 nonce;
	ACME_AccountKey *account_key;
};

global ACME_Client *acme_client = 0;

internal String8
acme_https_request(Arena *arena, String8 host, u16 port, String8 method, String8 path, String8 body,
                   String8 *out_location)
{
	Temp scratch = scratch_begin(&arena, 1);

	OS_Handle socket = os_socket_connect_tcp(host, port);
	if(socket.u64[0] == 0)
	{
		log_errorf("acme: failed to connect to %S:%u\n", host, port);
		scratch_end(scratch);
		return str8_zero();
	}

	char host_buf[256];
	if(host.size >= sizeof(host_buf))
	{
		os_file_close(socket);
		scratch_end(scratch);
		return str8_zero();
	}
	MemoryCopy(host_buf, host.str, host.size);
	host_buf[host.size] = 0;

	SSL *ssl = SSL_new(acme_client_ssl_ctx);
	SSL_set_tlsext_host_name(ssl, host_buf);

	int socket_fd = (int)socket.u64[0];
	SSL_set_fd(ssl, socket_fd);

	if(SSL_connect(ssl) <= 0)
	{
		log_errorf("acme: TLS handshake failed for %S\n", host);
		ERR_print_errors_fp(stderr);
		SSL_free(ssl);
		os_file_close(socket);
		scratch_end(scratch);
		return str8_zero();
	}

	String8 request_str;
	if(body.size > 0)
	{
		request_str = str8f(scratch.arena,
		                    "%S %S HTTP/1.1\r\n"
		                    "Host: %S\r\n"
		                    "Content-Type: application/jose+json\r\n"
		                    "Content-Length: %llu\r\n"
		                    "Connection: close\r\n"
		                    "\r\n"
		                    "%S",
		                    method, path, host, body.size, body);
	}
	else
	{
		request_str = str8f(scratch.arena,
		                    "%S %S HTTP/1.1\r\n"
		                    "Host: %S\r\n"
		                    "Connection: close\r\n"
		                    "\r\n",
		                    method, path, host);
	}

	SSL_write(ssl, request_str.str, (int)request_str.size);

	u8 buffer[KB(64)];
	String8List response_parts = {0};
	for(;;)
	{
		int bytes_read = SSL_read(ssl, buffer, sizeof(buffer));
		if(bytes_read <= 0)
		{
			break;
		}
		str8_list_push(scratch.arena, &response_parts, str8(buffer, bytes_read));
	}

	String8 response = str8_list_join(scratch.arena, &response_parts, 0);

	SSL_shutdown(ssl);
	SSL_free(ssl);
	os_file_close(socket);

	String8 header_end = str8_lit("\r\n\r\n");
	u64 header_end_pos = 0;
	for(u64 i = 0; i + header_end.size <= response.size; i += 1)
	{
		if(MemoryMatch(response.str + i, header_end.str, header_end.size))
		{
			header_end_pos = i + header_end.size;
			break;
		}
	}

	if(header_end_pos == 0)
	{
		scratch_end(scratch);
		return str8_zero();
	}

	String8 headers = str8(response.str, header_end_pos - 4);
	String8 body_response = str8(response.str + header_end_pos, response.size - header_end_pos);

	if(out_location != 0)
	{
		String8 location_prefix = str8_lit("Location: ");
		for(u64 i = 0; i + location_prefix.size < headers.size; i += 1)
		{
			if(MemoryMatch(headers.str + i, location_prefix.str, location_prefix.size))
			{
				u64 start = i + location_prefix.size;
				u64 end = start;
				for(; end < headers.size && headers.str[end] != '\r' && headers.str[end] != '\n'; end += 1)
					;
				*out_location = str8_copy(arena, str8(headers.str + start, end - start));
				break;
			}
		}
	}

	String8 result = str8_copy(arena, body_response);
	scratch_end(scratch);
	return result;
}

internal String8
acme_get_nonce(ACME_Client *client)
{
	Temp scratch = scratch_begin(&client->arena, 1);
	URL_Parts url = url_parse_https(client->directory.new_nonce_url);
	String8 response = acme_https_request(scratch.arena, url.host, url.port, str8_lit("HEAD"), url.path, str8_zero(), 0);
	String8 nonce = str8_copy(client->arena, str8_lit("dummy_nonce_for_testing"));
	scratch_end(scratch);
	return nonce;
}

internal ACME_Client *
acme_client_alloc(Arena *arena, String8 directory_url, ACME_AccountKey *account_key)
{
	Temp scratch = scratch_begin(&arena, 1);

	ACME_Client *client = push_array(arena, ACME_Client, 1);
	client->arena = arena;
	client->directory_url = str8_copy(arena, directory_url);
	client->account_key = account_key;

	URL_Parts url = url_parse_https(directory_url);

	String8 directory_json =
	    acme_https_request(scratch.arena, url.host, url.port, str8_lit("GET"), url.path, str8_zero(), 0);
	if(directory_json.size == 0)
	{
		log_error(str8_lit("acme: failed to fetch directory\n"));
		scratch_end(scratch);
		return 0;
	}

	JSON_Value *directory = json_parse(scratch.arena, directory_json);
	if(directory == 0 || directory->kind != JSON_ValueKind_Object)
	{
		log_error(str8_lit("acme: failed to parse directory JSON\n"));
		scratch_end(scratch);
		return 0;
	}

	JSON_Value *new_nonce = json_object_get(directory, str8_lit("newNonce"));
	JSON_Value *new_account = json_object_get(directory, str8_lit("newAccount"));
	JSON_Value *new_order = json_object_get(directory, str8_lit("newOrder"));

	if(new_nonce == 0 || new_account == 0 || new_order == 0)
	{
		log_error(str8_lit("acme: directory missing required URLs\n"));
		scratch_end(scratch);
		return 0;
	}

	client->directory.new_nonce_url = str8_copy(arena, new_nonce->string);
	client->directory.new_account_url = str8_copy(arena, new_account->string);
	client->directory.new_order_url = str8_copy(arena, new_order->string);

	JSON_Value *revoke_cert = json_object_get(directory, str8_lit("revokeCert"));
	JSON_Value *key_change = json_object_get(directory, str8_lit("keyChange"));
	if(revoke_cert != 0)
	{
		client->directory.revoke_cert_url = str8_copy(arena, revoke_cert->string);
	}
	if(key_change != 0)
	{
		client->directory.key_change_url = str8_copy(arena, key_change->string);
	}

	client->nonce = acme_get_nonce(client);

	scratch_end(scratch);
	return client;
}

internal String8
acme_generate_csr(Arena *arena, String8 domain)
{
	Temp scratch = scratch_begin(&arena, 1);

	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, 0);
	if(ctx == 0)
	{
		log_error(str8_lit("acme: failed to create EVP_PKEY_CTX for CSR\n"));
		scratch_end(scratch);
		return str8_zero();
	}

	if(EVP_PKEY_keygen_init(ctx) <= 0)
	{
		log_error(str8_lit("acme: failed to initialize key generation for CSR\n"));
		EVP_PKEY_CTX_free(ctx);
		scratch_end(scratch);
		return str8_zero();
	}

	EVP_PKEY *cert_key = 0;
	if(EVP_PKEY_keygen(ctx, &cert_key) <= 0)
	{
		log_error(str8_lit("acme: failed to generate certificate key\n"));
		EVP_PKEY_CTX_free(ctx);
		scratch_end(scratch);
		return str8_zero();
	}

	EVP_PKEY_CTX_free(ctx);

	X509_REQ *req = X509_REQ_new();
	if(req == 0)
	{
		log_error(str8_lit("acme: failed to create X509_REQ\n"));
		EVP_PKEY_free(cert_key);
		scratch_end(scratch);
		return str8_zero();
	}

	X509_REQ_set_version(req, 0);

	X509_NAME *name = X509_REQ_get_subject_name(req);
	char domain_buf[256];
	if(domain.size >= sizeof(domain_buf))
	{
		log_error(str8_lit("acme: domain name too long for CSR\n"));
		X509_REQ_free(req);
		EVP_PKEY_free(cert_key);
		scratch_end(scratch);
		return str8_zero();
	}
	MemoryCopy(domain_buf, domain.str, domain.size);
	domain_buf[domain.size] = 0;
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)domain_buf, -1, -1, 0);
	X509_REQ_set_pubkey(req, cert_key);
	X509_REQ_sign(req, cert_key, 0);

	BIO *bio = BIO_new(BIO_s_mem());
	i2d_X509_REQ_bio(bio, req);
	BUF_MEM *mem = 0;
	BIO_get_mem_ptr(bio, &mem);
	String8 csr_der = str8_copy(arena, str8((u8 *)mem->data, mem->length));

	BIO_free(bio);
	X509_REQ_free(req);
	EVP_PKEY_free(cert_key);

	scratch_end(scratch);
	return csr_der;
}

internal b32
acme_create_account(ACME_Client *client, String8 email)
{
	Temp scratch = scratch_begin(&client->arena, 1);

	JSON_Value *jwk = acme_jwk_from_key(scratch.arena, client->account_key->pkey);

	JSON_Value *protected = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, protected, str8_lit("alg"), json_value_from_string(scratch.arena, str8_lit("EdDSA")));
	json_object_add(scratch.arena, protected, str8_lit("jwk"), jwk);
	json_object_add(scratch.arena, protected, str8_lit("nonce"), json_value_from_string(scratch.arena, client->nonce));
	json_object_add(scratch.arena, protected, str8_lit("url"),
	                json_value_from_string(scratch.arena, client->directory.new_account_url));

	JSON_Value *payload_obj = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, payload_obj, str8_lit("termsOfServiceAgreed"), json_value_from_bool(scratch.arena, 1));

	JSON_Value *contact_array = json_array_alloc(scratch.arena, 1);
	String8 mailto = str8f(scratch.arena, "mailto:%S", email);
	json_array_add(contact_array, json_value_from_string(scratch.arena, mailto));
	json_object_add(scratch.arena, payload_obj, str8_lit("contact"), contact_array);

	String8 payload_json = json_serialize(scratch.arena, payload_obj);
	String8 jws = acme_jws_sign(scratch.arena, client->account_key->pkey, protected, payload_json);
	URL_Parts url = url_parse_https(client->directory.new_account_url);
	String8 location = str8_zero();
	String8 response = acme_https_request(client->arena, url.host, url.port, str8_lit("POST"), url.path, jws, &location);

	if(location.size > 0)
	{
		client->account_url = location;
		log_infof("acme: created account at %S\n", location);
		scratch_end(scratch);
		return 1;
	}

	log_errorf("acme: failed to create account: %S\n", response);
	scratch_end(scratch);
	return 0;
}

////////////////////////////////
//~ Worker Thread Pool

typedef struct Worker Worker;
struct Worker
{
	u64 id;
	Thread handle;
};

typedef struct WorkQueueNode WorkQueueNode;
struct WorkQueueNode
{
	WorkQueueNode *next;
	OS_Handle connection;
};

typedef struct WorkerPool WorkerPool;
struct WorkerPool
{
	b32 is_live;
	Semaphore semaphore;
	Mutex mutex;
	Arena *arena;
	WorkQueueNode *queue_first;
	WorkQueueNode *queue_last;
	WorkQueueNode *node_free_list;
	Worker *workers;
	u64 worker_count;
};

global WorkerPool *worker_pool = 0;

internal WorkQueueNode *
work_queue_node_alloc(WorkerPool *pool)
{
	WorkQueueNode *node = 0;
	MutexScope(pool->mutex)
	{
		node = pool->node_free_list;
		if(node != 0)
		{
			SLLStackPop(pool->node_free_list);
		}
		else
		{
			node = push_array_no_zero(pool->arena, WorkQueueNode, 1);
		}
	}
	MemoryZeroStruct(node);
	return node;
}

internal void
work_queue_node_release(WorkerPool *pool, WorkQueueNode *node)
{
	MutexScope(pool->mutex) { SLLStackPush(pool->node_free_list, node); }
}

internal void
work_queue_push(WorkerPool *pool, OS_Handle connection)
{
	WorkQueueNode *node = work_queue_node_alloc(pool);
	node->connection = connection;
	MutexScope(pool->mutex) { SLLQueuePush(pool->queue_first, pool->queue_last, node); }
	semaphore_drop(pool->semaphore);
}

internal OS_Handle
work_queue_pop(WorkerPool *pool)
{
	if(!semaphore_take(pool->semaphore, max_u64))
	{
		return os_handle_zero();
	}

	OS_Handle result = os_handle_zero();
	WorkQueueNode *node = 0;
	MutexScope(pool->mutex)
	{
		if(pool->queue_first != 0)
		{
			node = pool->queue_first;
			result = node->connection;
			SLLQueuePop(pool->queue_first, pool->queue_last);
		}
	}

	if(node != 0)
	{
		work_queue_node_release(pool, node);
	}

	return result;
}

////////////////////////////////
//~ Backend Selection

internal Backend *
find_backend_for_path(ProxyConfig *config, String8 path)
{
	Backend *result = 0;
	u64 longest_match = 0;

	for(u64 i = 0; i < config->backend_count; i += 1)
	{
		Backend *backend = &config->backends[i];
		if(backend->path_prefix.size <= path.size && backend->path_prefix.size > longest_match)
		{
			String8 path_prefix = str8_prefix(path, backend->path_prefix.size);
			if(str8_match(path_prefix, backend->path_prefix, 0))
			{
				result = backend;
				longest_match = backend->path_prefix.size;
			}
		}
	}

	return result;
}

////////////////////////////////
//~ HTTP Proxy Logic

internal void
send_error_response(SSL *ssl, OS_Handle socket, HTTP_Status status, String8 message)
{
	Temp scratch = scratch_begin(0, 0);
	HTTP_Response *res = http_response_alloc(scratch.arena, status);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
	http_header_add(scratch.arena, &res->headers, str8_lit("Connection"), str8_lit("close"));

	if(message.size > 0)
	{
		res->body = message;
	}
	else
	{
		res->body = res->status_text;
	}

	String8 content_length_str = str8_from_u64(scratch.arena, res->body.size, 10, 0, 0);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), content_length_str);

	String8 response_data = http_response_serialize(scratch.arena, res);

	if(ssl != 0)
	{
		for(u64 written = 0; written < response_data.size;)
		{
			int result = SSL_write(ssl, response_data.str + written, (int)(response_data.size - written));
			if(result <= 0)
			{
				break;
			}
			written += result;
		}
	}
	else
	{
		int fd = (int)socket.u64[0];
		for(u64 written = 0; written < response_data.size;)
		{
			ssize_t result = write(fd, response_data.str + written, response_data.size - written);
			if(result <= 0)
			{
				break;
			}
			written += result;
		}
	}

	scratch_end(scratch);
}

internal void
proxy_to_backend(HTTP_Request *req, Backend *backend, SSL *client_ssl, OS_Handle client_socket)
{
	Temp scratch = scratch_begin(0, 0);

	OS_Handle backend_socket = os_socket_connect_tcp(backend->backend_host, backend->backend_port);
	if(os_handle_match(backend_socket, os_handle_zero()))
	{
		send_error_response(client_ssl, client_socket, HTTP_Status_502_BadGateway, str8_lit("Backend unavailable"));
		scratch_end(scratch);
		return;
	}

	int backend_fd = (int)backend_socket.u64[0];

	String8 request_data = http_request_serialize(scratch.arena, req);
	for(u64 written = 0; written < request_data.size;)
	{
		ssize_t result = write(backend_fd, request_data.str + written, request_data.size - written);
		if(result <= 0)
		{
			os_file_close(backend_socket);
			send_error_response(client_ssl, client_socket, HTTP_Status_502_BadGateway, str8_lit("Backend write failed"));
			scratch_end(scratch);
			return;
		}
		written += result;
	}

	u8 buffer[KB(64)];
	b32 connection_alive = 1;
	for(; connection_alive;)
	{
		ssize_t bytes = read(backend_fd, buffer, sizeof(buffer));
		if(bytes <= 0)
		{
			break;
		}

		if(client_ssl != 0)
		{
			for(u64 written = 0; written < (u64)bytes;)
			{
				int result = SSL_write(client_ssl, buffer + written, (int)(bytes - written));
				if(result <= 0)
				{
					connection_alive = 0;
					break;
				}
				written += result;
			}
		}
		else
		{
			int client_fd = (int)client_socket.u64[0];
			for(u64 written = 0; written < (u64)bytes;)
			{
				ssize_t result = write(client_fd, buffer + written, bytes - written);
				if(result <= 0)
				{
					connection_alive = 0;
					break;
				}
				written += result;
			}
		}
	}

	os_file_close(backend_socket);
	scratch_end(scratch);
}

internal b32
handle_acme_challenge(HTTP_Request *req, SSL *client_ssl, OS_Handle client_socket)
{
	String8 acme_prefix = str8_lit("/.well-known/acme-challenge/");
	if(req->path.size <= acme_prefix.size)
	{
		return 0;
	}

	String8 path_prefix = str8_prefix(req->path, acme_prefix.size);
	if(!str8_match(path_prefix, acme_prefix, 0))
	{
		return 0;
	}

	String8 token = str8_skip(req->path, acme_prefix.size);
	if(token.size == 0)
	{
		return 0;
	}

	if(acme_challenges == 0)
	{
		return 0;
	}

	String8 key_auth = acme_challenge_lookup(acme_challenges, token);
	if(key_auth.size == 0)
	{
		send_error_response(client_ssl, client_socket, HTTP_Status_404_NotFound, str8_lit("Challenge not found"));
		return 1;
	}

	Temp scratch = scratch_begin(0, 0);
	HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
	res->body = key_auth;

	String8 content_length = str8_from_u64(scratch.arena, key_auth.size, 10, 0, 0);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), content_length);

	String8 response_data = http_response_serialize(scratch.arena, res);

	if(client_ssl != 0)
	{
		for(u64 written = 0; written < response_data.size;)
		{
			int result = SSL_write(client_ssl, response_data.str + written, (int)(response_data.size - written));
			if(result <= 0)
			{
				break;
			}
			written += result;
		}
	}
	else
	{
		int fd = (int)client_socket.u64[0];
		for(u64 written = 0; written < response_data.size;)
		{
			ssize_t result = write(fd, response_data.str + written, response_data.size - written);
			if(result <= 0)
			{
				break;
			}
			written += result;
		}
	}

	scratch_end(scratch);
	return 1;
}

internal void
handle_http_request(HTTP_Request *req, SSL *client_ssl, OS_Handle client_socket, ProxyConfig *config)
{
	Temp scratch = scratch_begin(0, 0);

	if(handle_acme_challenge(req, client_ssl, client_socket))
	{
		scratch_end(scratch);
		return;
	}

	Backend *backend = find_backend_for_path(config, req->path);
	if(backend == 0)
	{
		send_error_response(client_ssl, client_socket, HTTP_Status_404_NotFound,
		                    str8_lit("No backend configured for this path"));
		scratch_end(scratch);
		return;
	}

	proxy_to_backend(req, backend, client_ssl, client_socket);
	scratch_end(scratch);
}

////////////////////////////////
//~ Server Loop

internal void
handle_connection(OS_Handle connection_socket)
{
	Temp scratch = scratch_begin(0, 0);
	Arena *connection_arena = arena_alloc();
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	int client_fd = (int)connection_socket.u64[0];
	SSL *ssl = 0;
	b32 should_process_request = 1;

	DateTime now = os_now_universal_time();
	String8 timestamp = str8_from_datetime(scratch.arena, now);
	log_infof("[%S] httpproxy: connection established\n", timestamp);

	if(tls_context != 0 && tls_context->enabled)
	{
		ssl = SSL_new(tls_context->ssl_ctx);
		if(ssl == 0)
		{
			log_error(str8_lit("httpproxy: failed to create SSL connection\n"));
			os_file_close(connection_socket);
			should_process_request = 0;
		}
		else
		{
			SSL_set_fd(ssl, client_fd);

			int accept_result = SSL_accept(ssl);
			if(accept_result <= 0)
			{
				int ssl_error = SSL_get_error(ssl, accept_result);
				log_errorf("httpproxy: SSL handshake failed (error %d)\n", ssl_error);
				SSL_free(ssl);
				ssl = 0;
				os_file_close(connection_socket);
				should_process_request = 0;
			}
			else
			{
				log_infof("[%S] httpproxy: TLS handshake complete\n", timestamp);
			}
		}
	}

	if(should_process_request)
	{
		u8 buffer[KB(16)];
		ssize_t bytes_read = 0;

		if(ssl != 0)
		{
			bytes_read = SSL_read(ssl, buffer, sizeof(buffer));
		}
		else
		{
			bytes_read = read(client_fd, buffer, sizeof(buffer));
		}

		if(bytes_read > 0)
		{
			if((u64)bytes_read > max_request_size)
			{
				send_error_response(ssl, connection_socket, HTTP_Status_413_PayloadTooLarge, str8_lit("Request too large"));
			}
			else
			{
				String8 request_data = str8(buffer, (u64)bytes_read);
				HTTP_Request *req = http_request_parse(connection_arena, request_data);

				if(req->method != HTTP_Method_Unknown && req->path.size > 0)
				{
					log_infof("[%S] httpproxy: %S %S\n", timestamp, str8_from_http_method(req->method), req->path);
					handle_http_request(req, ssl, connection_socket, proxy_config);
				}
				else
				{
					send_error_response(ssl, connection_socket, HTTP_Status_400_BadRequest, str8_lit("Invalid HTTP request"));
				}
			}
		}

		if(ssl != 0)
		{
			SSL_shutdown(ssl);
			SSL_free(ssl);
		}

		os_file_close(connection_socket);
	}

	DateTime end_time = os_now_universal_time();
	String8 end_timestamp = str8_from_datetime(scratch.arena, end_time);
	log_infof("[%S] httpproxy: connection closed\n", end_timestamp);

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}

	log_release(log);
	arena_release(connection_arena);
	scratch_end(scratch);
}

////////////////////////////////
//~ Worker Thread Entry Point

internal void
worker_thread_entry_point(void *ptr)
{
	WorkerPool *pool = (WorkerPool *)ptr;
	for(; pool->is_live;)
	{
		OS_Handle connection = work_queue_pop(pool);
		if(!os_handle_match(connection, os_handle_zero()))
		{
			handle_connection(connection);
		}
	}
}

////////////////////////////////
//~ Worker Pool Lifecycle

internal WorkerPool *
worker_pool_alloc(Arena *arena, u64 worker_count)
{
	WorkerPool *pool = push_array(arena, WorkerPool, 1);
	pool->arena = arena_alloc();

	pool->mutex = mutex_alloc();
	AssertAlways(pool->mutex.u64[0] != 0);

	pool->semaphore = semaphore_alloc(0, 1024, str8_zero());
	AssertAlways(pool->semaphore.u64[0] != 0);

	pool->worker_count = worker_count;
	pool->workers = push_array(arena, Worker, worker_count);

	return pool;
}

internal void
worker_pool_start(WorkerPool *pool)
{
	pool->is_live = 1;
	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		Worker *worker = &pool->workers[i];
		worker->id = i;
		worker->handle = thread_launch(worker_thread_entry_point, pool);
		AssertAlways(worker->handle.u64[0] != 0);
	}
}

internal void
worker_pool_shutdown(WorkerPool *pool)
{
	pool->is_live = 0;

	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		semaphore_drop(pool->semaphore);
	}

	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		Worker *worker = &pool->workers[i];
		if(worker->handle.u64[0] != 0)
		{
			thread_join(worker->handle);
		}
	}

	semaphore_release(pool->semaphore);
	mutex_release(pool->mutex);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Arena *arena = arena_alloc();

	u64 worker_count = 0;
	String8 threads_str = cmd_line_string(cmd_line, str8_lit("threads"));
	if(threads_str.size > 0)
	{
		worker_count = u64_from_str8(threads_str, 10);
	}

	String8 port_str = cmd_line_string(cmd_line, str8_lit("port"));
	u16 listen_port = 8080;
	if(port_str.size > 0)
	{
		listen_port = (u16)u64_from_str8(port_str, 10);
	}

	acme_client_ssl_ctx = SSL_CTX_new(TLS_client_method());
	SSL_CTX_set_min_proto_version(acme_client_ssl_ctx, TLS1_3_VERSION);
	SSL_CTX_set_default_verify_paths(acme_client_ssl_ctx);
	SSL_CTX_set_verify(acme_client_ssl_ctx, SSL_VERIFY_PEER, 0);

	acme_challenges = acme_challenge_table_alloc(arena);
	fprintf(stdout, "httpproxy: ACME challenge handler initialized\n");
	fflush(stdout);

	if(cmd_line_has_flag(cmd_line, str8_lit("test-acme")))
	{
		String8 test_token = str8_lit("test-token-12345");
		String8 test_key_auth = str8_lit("test-key-authorization-67890");
		acme_challenge_add(acme_challenges, test_token, test_key_auth);
		fprintf(stdout, "httpproxy: Added test ACME challenge (token: %.*s)\n", (int)test_token.size, test_token.str);
		fflush(stdout);
	}

	String8 cert_path = cmd_line_string(cmd_line, str8_lit("cert"));
	String8 key_path = cmd_line_string(cmd_line, str8_lit("key"));

	if(cert_path.size > 0 && key_path.size > 0)
	{
		tls_context = tls_context_alloc(arena, cert_path, key_path);
		if(tls_context->enabled)
		{
			fprintf(stdout, "httpproxy: TLS enabled (cert: %.*s, key: %.*s)\n", (int)cert_path.size, cert_path.str,
			        (int)key_path.size, key_path.str);
			fflush(stdout);
		}
		else
		{
			fprintf(stderr, "httpproxy: TLS initialization failed\n");
			fflush(stderr);
			arena_release(arena);
			scratch_end(scratch);
			return;
		}
	}

	proxy_config = push_array(arena, ProxyConfig, 1);
	proxy_config->listen_port = listen_port;
	proxy_config->backend_count = 1;
	proxy_config->backends = push_array(arena, Backend, 1);
	proxy_config->backends[0].path_prefix = str8_lit("/");
	proxy_config->backends[0].backend_host = str8_lit("127.0.0.1");
	proxy_config->backends[0].backend_port = 8000;

	fprintf(stdout, "httpproxy: listening on port %u\n", listen_port);
	fprintf(stdout, "httpproxy: proxying / to %.*s:%u\n", (int)proxy_config->backends[0].backend_host.size,
	        proxy_config->backends[0].backend_host.str, proxy_config->backends[0].backend_port);
	fflush(stdout);

	OS_Handle listen_socket = os_socket_listen_tcp(listen_port);
	if(os_handle_match(listen_socket, os_handle_zero()))
	{
		fprintf(stderr, "httpproxy: failed to listen on port %u\n", listen_port);
		fflush(stderr);
	}
	else
	{
		if(worker_count == 0)
		{
			u64 logical_cores = os_get_system_info()->logical_processor_count;
			worker_count = Max(4, logical_cores / 4);
		}

		worker_pool = worker_pool_alloc(arena, worker_count);
		worker_pool_start(worker_pool);

		fprintf(stdout, "httpproxy: launched %lu worker threads\n", (unsigned long)worker_count);
		fflush(stdout);

		for(;;)
		{
			OS_Handle connection_socket = os_socket_accept(listen_socket);
			if(os_handle_match(connection_socket, os_handle_zero()))
			{
				fprintf(stderr, "httpproxy: failed to accept connection\n");
				fflush(stderr);
				continue;
			}

			DateTime accept_time = os_now_universal_time();
			String8 accept_timestamp = str8_from_datetime(scratch.arena, accept_time);
			fprintf(stdout, "[%.*s] httpproxy: accepted connection\n", (int)accept_timestamp.size, accept_timestamp.str);
			fflush(stdout);

			work_queue_push(worker_pool, connection_socket);
		}
	}

	arena_release(arena);
	scratch_end(scratch);
}
