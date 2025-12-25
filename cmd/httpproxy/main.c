#include <openssl/core_names.h>
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
read_only global u64 challenge_table_initial_capacity = 16;
read_only global u64 cert_renewal_check_interval_ms = 24 * 60 * 60 * 1000;
read_only global u64 cert_renewal_days_threshold = 30;
read_only global u64 acme_poll_max_attempts = 30;
read_only global u64 acme_poll_delay_ms = 2000;
read_only global u64 http_read_buffer_size = KB(16);

////////////////////////////////
//~ Forward Declarations

typedef struct ProxyConfig ProxyConfig;
typedef struct TLS_Context TLS_Context;
typedef struct ACME_ChallengeTable ACME_ChallengeTable;
typedef struct ACME_AccountKey ACME_AccountKey;
typedef struct ACME_Client ACME_Client;
typedef struct ACME_RenewalConfig ACME_RenewalConfig;
typedef struct HTTP80_Config HTTP80_Config;
typedef struct WorkerPool WorkerPool;

////////////////////////////////
//~ Global Context

typedef struct HTTPProxy_Context HTTPProxy_Context;
struct HTTPProxy_Context
{
	ProxyConfig *config;
	TLS_Context *tls;
	ACME_ChallengeTable *challenges;
	ACME_Client *acme;
	ACME_AccountKey *acme_key;
	ACME_RenewalConfig *renewal;
	HTTP80_Config *http80;
	WorkerPool *workers;
	SSL_CTX *acme_ssl;
};

global HTTPProxy_Context *proxy_ctx = 0;

////////////////////////////////
//~ Proxy Configuration

typedef struct ProxyConfig ProxyConfig;
struct ProxyConfig
{
	String8 file_root;
	u16 listen_port;
};

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

internal ACME_ChallengeTable *
acme_challenge_table_alloc(Arena *arena)
{
	ACME_ChallengeTable *table = push_array(arena, ACME_ChallengeTable, 1);
	table->arena = arena_alloc();
	table->mutex = mutex_alloc();
	table->capacity = challenge_table_initial_capacity;
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

internal ACME_Challenge *
acme_challenge_find(ACME_ChallengeTable *table, String8 token)
{
	for(u64 i = 0; i < table->capacity; i += 1)
	{
		ACME_Challenge *ch = &table->challenges[i];
		if(ch->token.size > 0 && str8_match(ch->token, token, 0))
		{
			return ch;
		}
	}
	return 0;
}

internal String8
acme_challenge_lookup(ACME_ChallengeTable *table, String8 token)
{
	String8 result = str8_zero();
	MutexScope(table->mutex)
	{
		ACME_Challenge *ch = acme_challenge_find(table, token);
		if(ch != 0)
		{
			result = ch->key_authorization;
		}
	}
	return result;
}

internal void
acme_challenge_remove(ACME_ChallengeTable *table, String8 token)
{
	MutexScope(table->mutex)
	{
		ACME_Challenge *ch = acme_challenge_find(table, token);
		if(ch != 0)
		{
			MemoryZeroStruct(ch);
			table->count -= 1;
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

internal b32
tls_context_load_cert(SSL_CTX *ssl_ctx, String8 cert_path, String8 key_path)
{
	char cert_buf[PATH_MAX];
	char key_buf[PATH_MAX];

	if(cert_path.size >= sizeof(cert_buf) || key_path.size >= sizeof(key_buf))
	{
		log_error(str8_lit("httpproxy: certificate or key path too long\n"));
		return 0;
	}

	MemoryCopy(cert_buf, cert_path.str, cert_path.size);
	cert_buf[cert_path.size] = 0;
	MemoryCopy(key_buf, key_path.str, key_path.size);
	key_buf[key_path.size] = 0;

	if(SSL_CTX_use_certificate_chain_file(ssl_ctx, cert_buf) <= 0)
	{
		log_errorf("httpproxy: failed to load certificate from %S\n", cert_path);
		ERR_print_errors_fp(stderr);
		return 0;
	}

	if(SSL_CTX_use_PrivateKey_file(ssl_ctx, key_buf, SSL_FILETYPE_PEM) <= 0)
	{
		log_errorf("httpproxy: failed to load private key from %S\n", key_path);
		ERR_print_errors_fp(stderr);
		return 0;
	}

	if(!SSL_CTX_check_private_key(ssl_ctx))
	{
		log_error(str8_lit("httpproxy: private key does not match certificate\n"));
		return 0;
	}

	return 1;
}

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
		return 0;
	}

	SSL_CTX_set_min_proto_version(ctx->ssl_ctx, TLS1_3_VERSION);

	if(!tls_context_load_cert(ctx->ssl_ctx, cert_path, key_path))
	{
		SSL_CTX_free(ctx->ssl_ctx);
		return 0;
	}

	ctx->enabled = 1;
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

internal ACME_AccountKey *
acme_account_key_alloc(Arena *arena, String8 key_path)
{
	ACME_AccountKey *key = push_array(arena, ACME_AccountKey, 1);
	key->arena = arena;
	key->key_path = str8_copy(arena, key_path);

	OS_Handle file = os_file_open(OS_AccessFlag_Read, key_path);
	if(!os_handle_match(file, os_handle_zero()))
	{
		Temp scratch = scratch_begin(&arena, 1);
		OS_FileProperties props = os_properties_from_file(file);
		u8 *key_buffer = push_array(scratch.arena, u8, props.size);
		u64 bytes_read = os_file_read(file, rng_1u64(0, props.size), key_buffer);
		String8 key_data = str8(key_buffer, bytes_read);
		os_file_close(file);

		BIO *bio = BIO_new_mem_buf(key_data.str, (int)key_data.size);
		key->pkey = PEM_read_bio_PrivateKey(bio, 0, 0, 0);
		BIO_free(bio);
		scratch_end(scratch);

		if(key->pkey != 0)
		{
			log_infof("httpproxy: loaded existing ACME account key from %S\n", key_path);
			return key;
		}
		else
		{
			log_error(str8_lit("httpproxy: failed to parse existing ACME account key, generating new one\n"));
		}
	}

	log_infof("httpproxy: generating new ACME account key at %S\n", key_path);

	key->pkey = acme_generate_cert_key();
	if(key->pkey == 0)
	{
		log_error(str8_lit("httpproxy: failed to generate ACME account key\n"));
		return key;
	}

	BIO *bio = BIO_new(BIO_s_mem());
	PEM_write_bio_PrivateKey(bio, key->pkey, 0, 0, 0, 0, 0);

	BUF_MEM *mem = 0;
	BIO_get_mem_ptr(bio, &mem);

	char path_buf[PATH_MAX];
	if(key_path.size >= sizeof(path_buf))
	{
		log_error(str8_lit("httpproxy: key path too long\n"));
		BIO_free(bio);
		return key;
	}

	MemoryCopy(path_buf, key_path.str, key_path.size);
	path_buf[key_path.size] = 0;

	OS_Handle new_file = os_file_open(OS_AccessFlag_Write, key_path);
	os_file_write(new_file, rng_1u64(0, mem->length), mem->data);
	os_file_close(new_file);

	BIO_free(bio);

	log_infof("httpproxy: saved new ACME account key to %S\n", key_path);
	return key;
}

////////////////////////////////
//~ Socket Write Helper

internal void
socket_write_all(SSL *ssl, OS_Handle socket, String8 data)
{
	if(ssl != 0)
	{
		for(u64 written = 0; written < data.size;)
		{
			int result = SSL_write(ssl, data.str + written, (int)(data.size - written));
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
		for(u64 written = 0; written < data.size;)
		{
			ssize_t result = write(fd, data.str + written, data.size - written);
			if(result <= 0)
			{
				break;
			}
			written += result;
		}
	}
}

////////////////////////////////
//~ Log Scope Helper

internal void
log_scope_flush(Arena *arena)
{
	LogScopeResult result = log_scope_end(arena);
	if(result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}
	if(result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}
}

////////////////////////////////
//~ ACME Orchestration

internal b32
acme_provision_certificate(Arena *arena, ACME_Client *client, String8 domain, EVP_PKEY *cert_key,
                           String8 cert_output_path, String8 key_output_path)
{
	log_infof("httpproxy: acme: starting certificate provisioning for %S\n", domain);

	String8 order_url = acme_create_order(client, domain);
	if(order_url.size == 0)
	{
		log_errorf("httpproxy: acme: failed to create order for %S\n", domain);
		return 0;
	}

	String8 authz_url = acme_get_authorization_url(client, order_url);
	if(authz_url.size == 0)
	{
		log_error(str8_lit("httpproxy: acme: failed to get authorization URL\n"));
		return 0;
	}

	ACME_ChallengeInfo challenge = acme_get_http01_challenge(client, authz_url);
	if(challenge.token.size == 0 || challenge.url.size == 0)
	{
		log_error(str8_lit("httpproxy: acme: failed to get http-01 challenge\n"));
		return 0;
	}

	Temp scratch = scratch_begin(&client->arena, 1);
	String8 key_auth = acme_key_authorization(scratch.arena, client->account_key, challenge.token);
	scratch_end(scratch);

	if(key_auth.size == 0)
	{
		log_error(str8_lit("httpproxy: acme: failed to compute key authorization\n"));
		return 0;
	}

	if(proxy_ctx->challenges != 0)
	{
		acme_challenge_add(proxy_ctx->challenges, challenge.token, key_auth);
		log_infof("httpproxy: acme: stored challenge response for token %S\n", challenge.token);
	}
	else
	{
		log_error(str8_lit("httpproxy: acme: challenge table not initialized\n"));
		return 0;
	}

	if(!acme_notify_challenge_ready(client, challenge.url))
	{
		log_error(str8_lit("httpproxy: acme: failed to notify challenge ready\n"));
		if(proxy_ctx->challenges != 0)
		{
			acme_challenge_remove(proxy_ctx->challenges, challenge.token);
		}
		return 0;
	}

	log_info(str8_lit("httpproxy: acme: polling challenge status...\n"));
	b32 challenge_valid = acme_poll_challenge_status(client, challenge.url, acme_poll_max_attempts, acme_poll_delay_ms);

	if(proxy_ctx->challenges != 0)
	{
		acme_challenge_remove(proxy_ctx->challenges, challenge.token);
	}

	if(!challenge_valid)
	{
		log_error(str8_lit("httpproxy: acme: challenge validation failed\n"));
		return 0;
	}

	log_info(str8_lit("httpproxy: acme: challenge validated successfully\n"));

	String8 finalize_result = acme_finalize_order(client, order_url, cert_key, domain);
	if(finalize_result.size == 0)
	{
		log_error(str8_lit("httpproxy: acme: failed to finalize order\n"));
		return 0;
	}

	log_info(str8_lit("httpproxy: acme: polling order status...\n"));
	if(!acme_poll_order_status(client, order_url, acme_poll_max_attempts, acme_poll_delay_ms))
	{
		log_error(str8_lit("httpproxy: acme: order validation failed\n"));
		return 0;
	}

	String8 cert_pem = acme_download_certificate(client, order_url);
	if(cert_pem.size == 0)
	{
		log_error(str8_lit("httpproxy: acme: failed to download certificate\n"));
		return 0;
	}

	log_infof("httpproxy: acme: saving certificate to %S\n", cert_output_path);
	OS_Handle cert_file = os_file_open(OS_AccessFlag_Write, cert_output_path);
	if(os_handle_match(cert_file, os_handle_zero()))
	{
		log_errorf("httpproxy: acme: failed to open certificate file %S\n", cert_output_path);
		return 0;
	}
	os_file_write(cert_file, rng_1u64(0, cert_pem.size), cert_pem.str);
	os_file_close(cert_file);

	log_infof("httpproxy: acme: saving private key to %S\n", key_output_path);
	BIO *key_bio = BIO_new(BIO_s_mem());
	PEM_write_bio_PrivateKey(key_bio, cert_key, 0, 0, 0, 0, 0);
	BUF_MEM *key_mem = 0;
	BIO_get_mem_ptr(key_bio, &key_mem);

	OS_Handle key_file = os_file_open(OS_AccessFlag_Write, key_output_path);
	if(os_handle_match(key_file, os_handle_zero()))
	{
		log_errorf("httpproxy: acme: failed to open key file %S\n", key_output_path);
		BIO_free(key_bio);
		return 0;
	}
	os_file_write(key_file, rng_1u64(0, key_mem->length), key_mem->data);
	os_file_close(key_file);
	BIO_free(key_bio);

	log_infof("httpproxy: acme: certificate provisioning complete for %S\n", domain);

	if(proxy_ctx->tls == 0)
	{
		log_info(str8_lit("httpproxy: acme: initializing TLS context with new certificate\n"));
		proxy_ctx->tls = tls_context_alloc(arena, cert_output_path, key_output_path);
		if(proxy_ctx->tls == 0)
		{
			log_error(str8_lit("httpproxy: acme: failed to initialize TLS context\n"));
			return 0;
		}
		log_info(str8_lit("httpproxy: acme: TLS context initialized successfully\n"));
	}
	else
	{
		log_info(str8_lit("httpproxy: acme: reloading TLS context with new certificate\n"));

		SSL_CTX *new_ctx = SSL_CTX_new(TLS_server_method());
		if(new_ctx == 0)
		{
			log_error(str8_lit("httpproxy: acme: failed to create new SSL context\n"));
			return 0;
		}

		SSL_CTX_set_min_proto_version(new_ctx, TLS1_3_VERSION);

		if(!tls_context_load_cert(new_ctx, cert_output_path, key_output_path))
		{
			SSL_CTX_free(new_ctx);
			return 0;
		}

		SSL_CTX *old_ctx = proxy_ctx->tls->ssl_ctx;
		proxy_ctx->tls->ssl_ctx = new_ctx;
		proxy_ctx->tls->cert_path = str8_copy(proxy_ctx->tls->arena, cert_output_path);
		proxy_ctx->tls->key_path = str8_copy(proxy_ctx->tls->arena, key_output_path);

		if(old_ctx != 0)
		{
			SSL_CTX_free(old_ctx);
		}

		log_info(str8_lit("httpproxy: acme: TLS context reloaded successfully\n"));
	}

	return 1;
}

////////////////////////////////
//~ ACME Certificate Renewal

typedef struct ACME_RenewalConfig ACME_RenewalConfig;
struct ACME_RenewalConfig
{
	Arena *arena;
	String8 domain;
	String8 cert_path;
	String8 key_path;
	u64 renew_days_before_expiry;
	b32 is_live;
};

internal void
acme_renewal_thread(void *ptr)
{
	ACME_RenewalConfig *config = (ACME_RenewalConfig *)ptr;
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	log_info(str8_lit("httpproxy: acme: renewal thread started\n"));

	if(proxy_ctx->acme != 0 && config->cert_path.size > 0)
	{
		OS_Handle cert_file = os_file_open(OS_AccessFlag_Read, config->cert_path);
		if(!os_handle_match(cert_file, os_handle_zero()))
		{
			os_file_close(cert_file);
			log_infof("httpproxy: acme: found existing certificate at %S\n", config->cert_path);
		}
		else
		{
			log_infof("httpproxy: acme: no certificate found at %S, provisioning new certificate\n", config->cert_path);
			EVP_PKEY *cert_key = acme_generate_cert_key();
			if(cert_key != 0)
			{
				b32 success = acme_provision_certificate(config->arena, proxy_ctx->acme, config->domain, cert_key,
				                                         config->cert_path, config->key_path);

				if(success)
				{
					log_info(str8_lit("httpproxy: acme: initial certificate provisioning complete\n"));
				}
				else
				{
					log_error(str8_lit("httpproxy: acme: initial certificate provisioning failed\n"));
					EVP_PKEY_free(cert_key);
				}
			}
			else
			{
				log_error(str8_lit("httpproxy: acme: failed to generate certificate key for initial provisioning\n"));
			}
		}
	}

	log_scope_flush(scratch.arena);
	log_release(log);
	scratch_end(scratch);

	for(;;)
	{
		os_sleep_milliseconds(cert_renewal_check_interval_ms);

		if(!config->is_live)
		{
			break;
		}

		if(proxy_ctx->acme == 0 || config->cert_path.size == 0)
		{
			continue;
		}

		Temp renewal_scratch = scratch_begin(0, 0);
		Log *renewal_log = log_alloc();
		log_select(renewal_log);
		log_scope_begin();

		u64 days_left = acme_cert_days_until_expiry(config->arena, config->cert_path);
		log_infof("httpproxy: acme: certificate expires in %u days\n", days_left);

		b32 should_renew = 0;
		if(days_left == 0)
		{
			log_error(str8_lit("httpproxy: acme: certificate has expired, attempting renewal\n"));
			should_renew = 1;
		}
		else if(days_left <= config->renew_days_before_expiry)
		{
			log_infof("httpproxy: acme: renewing certificate (%u days until expiry)\n", days_left);
			should_renew = 1;
		}

		if(should_renew)
		{
			EVP_PKEY *cert_key = acme_generate_cert_key();
			if(cert_key == 0)
			{
				log_error(str8_lit("httpproxy: acme: failed to generate certificate key for renewal\n"));
				log_scope_flush(renewal_scratch.arena);
				log_release(renewal_log);
				scratch_end(renewal_scratch);
				continue;
			}

			b32 success = acme_provision_certificate(config->arena, proxy_ctx->acme, config->domain, cert_key,
			                                         config->cert_path, config->key_path);

			if(success)
			{
				log_info(str8_lit("httpproxy: acme: certificate renewed successfully\n"));
			}
			else
			{
				log_error(str8_lit("httpproxy: acme: certificate renewal failed\n"));
				EVP_PKEY_free(cert_key);
			}
		}

		log_scope_flush(renewal_scratch.arena);
		log_release(renewal_log);
		scratch_end(renewal_scratch);
	}

	fprintf(stdout, "httpproxy: acme: renewal thread stopped\n");
	fflush(stdout);
}

////////////////////////////////
//~ ACME Challenge Handler

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

	if(proxy_ctx->challenges == 0)
	{
		return 0;
	}

	String8 key_auth = acme_challenge_lookup(proxy_ctx->challenges, token);

	Temp scratch = scratch_begin(0, 0);
	HTTP_Response *res;
	if(key_auth.size == 0)
	{
		res = http_response_alloc(scratch.arena, HTTP_Status_404_NotFound);
		http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
		http_header_add(scratch.arena, &res->headers, str8_lit("Connection"), str8_lit("close"));
		res->body = str8_lit("Challenge not found");
	}
	else
	{
		res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
		http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
		res->body = key_auth;
	}

	String8 content_length = str8_from_u64(scratch.arena, res->body.size, 10, 0, 0);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), content_length);
	String8 response_data = http_response_serialize(scratch.arena, res);

	socket_write_all(client_ssl, client_socket, response_data);

	scratch_end(scratch);
	return 1;
}

////////////////////////////////
//~ HTTP Port 80 Handler (ACME challenges + HTTPS redirect)

typedef struct HTTP80_Config HTTP80_Config;
struct HTTP80_Config
{
	String8 domain;
	b32 is_live;
};

internal void
http80_handler_thread(void *ptr)
{
	HTTP80_Config *config = (HTTP80_Config *)ptr;
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	log_info(str8_lit("http80: handler thread started\n"));

	OS_Handle listen_socket = os_socket_listen_tcp(80);
	if(os_handle_match(listen_socket, os_handle_zero()))
	{
		log_error(str8_lit("http80: failed to listen on port 80\n"));
		log_scope_flush(scratch.arena);
		log_release(log);
		scratch_end(scratch);
		return;
	}

	log_info(str8_lit("http80: listening on port 80 for ACME challenges and redirects\n"));
	log_scope_flush(scratch.arena);
	log_release(log);
	scratch_end(scratch);

	for(;;)
	{
		if(!config->is_live)
		{
			break;
		}

		OS_Handle client_socket = os_socket_accept(listen_socket);
		if(os_handle_match(client_socket, os_handle_zero()))
		{
			continue;
		}

		Temp scratch = scratch_begin(0, 0);

		u8 buffer[http_read_buffer_size];
		int client_fd = (int)client_socket.u64[0];
		ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

		if(bytes_read > 0)
		{
			String8 request_data = str8(buffer, (u64)bytes_read);
			HTTP_Request *req = http_request_parse(scratch.arena, request_data);

			if(req != 0)
			{
				if(handle_acme_challenge(req, 0, client_socket))
				{
					os_file_close(client_socket);
					scratch_end(scratch);
					continue;
				}

				String8 redirect_location = str8f(scratch.arena, "https://%S%S", config->domain, req->path);
				String8 response = str8f(scratch.arena,
				                         "HTTP/1.1 301 Moved Permanently\r\n"
				                         "Location: %S\r\n"
				                         "Content-Length: 0\r\n"
				                         "Connection: close\r\n"
				                         "\r\n",
				                         redirect_location);

				socket_write_all(0, client_socket, response);
			}
		}

		os_file_close(client_socket);
		scratch_end(scratch);
	}

	os_file_close(listen_socket);
	fprintf(stdout, "http80: handler thread stopped\n");
	fflush(stdout);
}

////////////////////////////////
//~ Worker Thread Pool

// TODO: replace with shared base/thread_pool implementation

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
//~ MIME Type Detection

internal String8
mime_type_from_extension(String8 path)
{
	String8 result = str8_lit("application/octet-stream");

	u64 dot_pos = 0;
	for(u64 i = path.size; i > 0; i -= 1)
	{
		if(path.str[i - 1] == '.')
		{
			dot_pos = i;
			break;
		}
		if(path.str[i - 1] == '/')
		{
			break;
		}
	}

	if(dot_pos == 0)
	{
		return result;
	}

	String8 ext = str8_skip(path, dot_pos);

	if(str8_match(ext, str8_lit("html"), StringMatchFlag_CaseInsensitive) ||
	   str8_match(ext, str8_lit("htm"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("text/html; charset=utf-8");
	}
	else if(str8_match(ext, str8_lit("css"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("text/css; charset=utf-8");
	}
	else if(str8_match(ext, str8_lit("js"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("application/javascript; charset=utf-8");
	}
	else if(str8_match(ext, str8_lit("json"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("application/json; charset=utf-8");
	}
	else if(str8_match(ext, str8_lit("xml"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("application/xml; charset=utf-8");
	}
	else if(str8_match(ext, str8_lit("txt"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("text/plain; charset=utf-8");
	}
	else if(str8_match(ext, str8_lit("png"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("image/png");
	}
	else if(str8_match(ext, str8_lit("jpg"), StringMatchFlag_CaseInsensitive) ||
	        str8_match(ext, str8_lit("jpeg"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("image/jpeg");
	}
	else if(str8_match(ext, str8_lit("gif"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("image/gif");
	}
	else if(str8_match(ext, str8_lit("svg"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("image/svg+xml");
	}
	else if(str8_match(ext, str8_lit("webp"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("image/webp");
	}
	else if(str8_match(ext, str8_lit("ico"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("image/x-icon");
	}
	else if(str8_match(ext, str8_lit("woff"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("font/woff");
	}
	else if(str8_match(ext, str8_lit("woff2"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("font/woff2");
	}
	else if(str8_match(ext, str8_lit("ttf"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("font/ttf");
	}
	else if(str8_match(ext, str8_lit("pdf"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("application/pdf");
	}
	else if(str8_match(ext, str8_lit("zip"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("application/zip");
	}
	else if(str8_match(ext, str8_lit("mp4"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("video/mp4");
	}
	else if(str8_match(ext, str8_lit("webm"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("video/webm");
	}
	else if(str8_match(ext, str8_lit("mp3"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("audio/mpeg");
	}
	else if(str8_match(ext, str8_lit("wav"), StringMatchFlag_CaseInsensitive))
	{
		result = str8_lit("audio/wav");
	}

	return result;
}

////////////////////////////////
//~ HTTP Proxy Logic

internal void
send_error_response(SSL *ssl, OS_Handle socket, HTTP_Status status, String8 message)
{
	Temp scratch = scratch_begin(0, 0);

	log_infof("httpproxy: error response %u %S\n", (u32)status, message.size > 0 ? message : str8_zero());

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

	socket_write_all(ssl, socket, response_data);

	scratch_end(scratch);
}

internal void
serve_file(HTTP_Request *req, String8 file_root, SSL *client_ssl, OS_Handle client_socket)
{
	Temp scratch = scratch_begin(0, 0);

	String8 request_path = req->path;
	if(request_path.size > 0 && request_path.str[0] == '/')
	{
		request_path = str8_skip(request_path, 1);
	}

	for(u64 i = 0; i + 1 < request_path.size; i += 1)
	{
		if(request_path.str[i] == '.' && request_path.str[i + 1] == '.')
		{
			send_error_response(client_ssl, client_socket, HTTP_Status_400_BadRequest, str8_lit("Invalid path"));
			scratch_end(scratch);
			return;
		}
	}

	String8 file_path = str8f(scratch.arena, "%S/%S", file_root, request_path);
	String8 canonical_root = os_full_path_from_path(scratch.arena, file_root);
	String8 canonical_file = os_full_path_from_path(scratch.arena, file_path);

	if(canonical_file.size < canonical_root.size ||
	   !str8_match(str8_prefix(canonical_file, canonical_root.size), canonical_root, 0))
	{
		send_error_response(client_ssl, client_socket, HTTP_Status_403_Forbidden, str8_lit("Access denied"));
		scratch_end(scratch);
		return;
	}

	file_path = canonical_file;

	OS_FileProperties props = os_properties_from_file_path(file_path);
	if(props.flags & OS_FilePropertyFlag_Directory)
	{
		String8 index_path = str8f(scratch.arena, "%S/index.html", file_path);
		OS_FileProperties index_props = os_properties_from_file_path(index_path);
		if(!(index_props.flags & OS_FilePropertyFlag_Directory) && index_props.size > 0)
		{
			file_path = index_path;
			props = index_props;
		}
		else
		{
			send_error_response(client_ssl, client_socket, HTTP_Status_403_Forbidden,
			                    str8_lit("Directory listing not allowed"));
			scratch_end(scratch);
			return;
		}
	}

	OS_Handle file = os_file_open(OS_AccessFlag_Read, file_path);
	if(os_handle_match(file, os_handle_zero()))
	{
		send_error_response(client_ssl, client_socket, HTTP_Status_404_NotFound, str8_lit("File not found"));
		scratch_end(scratch);
		return;
	}

	u8 *file_data = push_array(scratch.arena, u8, props.size);
	u64 bytes_read = os_file_read(file, rng_1u64(0, props.size), file_data);
	os_file_close(file);

	if(bytes_read != props.size)
	{
		send_error_response(client_ssl, client_socket, HTTP_Status_500_InternalServerError,
		                    str8_lit("Failed to read file"));
		scratch_end(scratch);
		return;
	}

	String8 mime_type = mime_type_from_extension(file_path);

	HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), mime_type);
	String8 content_length = str8_from_u64(scratch.arena, bytes_read, 10, 0, 0);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), content_length);
	res->body = str8(file_data, bytes_read);

	String8 response_data = http_response_serialize(scratch.arena, res);
	socket_write_all(client_ssl, client_socket, response_data);

	log_infof("httpproxy: served file %S (%u bytes, %S)\n", file_path, bytes_read, mime_type);

	scratch_end(scratch);
}

internal void
handle_http_request(HTTP_Request *req, SSL *client_ssl, OS_Handle client_socket, ProxyConfig *config)
{
	if(handle_acme_challenge(req, client_ssl, client_socket))
	{
		log_info(str8_lit("httpproxy: served ACME challenge\n"));
		return;
	}

	if(config->file_root.size == 0)
	{
		send_error_response(client_ssl, client_socket, HTTP_Status_500_InternalServerError,
		                    str8_lit("No file root configured"));
		return;
	}

	u64 request_start_us = os_now_microseconds();
	serve_file(req, config->file_root, client_ssl, client_socket);
	u64 request_end_us = os_now_microseconds();
	u64 duration_us = request_end_us - request_start_us;
	log_infof("httpproxy: request complete (%u μs)\n", duration_us);
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

	struct sockaddr_storage client_addr_storage;
	socklen_t client_addr_len = sizeof(client_addr_storage);
	String8 client_ip = str8_lit("unknown");
	u16 client_port = 0;

	if(getpeername(client_fd, (struct sockaddr *)&client_addr_storage, &client_addr_len) == 0)
	{
		if(client_addr_storage.ss_family == AF_INET)
		{
			struct sockaddr_in *addr_in = (struct sockaddr_in *)&client_addr_storage;
			u8 *ip_bytes = (u8 *)&addr_in->sin_addr.s_addr;
			client_ip = str8f(scratch.arena, "%u.%u.%u.%u", ip_bytes[0], ip_bytes[1], ip_bytes[2], ip_bytes[3]);
			client_port = ntohs(addr_in->sin_port);
		}
		else if(client_addr_storage.ss_family == AF_INET6)
		{
			struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&client_addr_storage;
			u8 *ip_bytes = addr_in6->sin6_addr.s6_addr;
			client_ip =
			    str8f(scratch.arena, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", ip_bytes[0],
			          ip_bytes[1], ip_bytes[2], ip_bytes[3], ip_bytes[4], ip_bytes[5], ip_bytes[6], ip_bytes[7], ip_bytes[8],
			          ip_bytes[9], ip_bytes[10], ip_bytes[11], ip_bytes[12], ip_bytes[13], ip_bytes[14], ip_bytes[15]);
			client_port = ntohs(addr_in6->sin6_port);
		}
	}

	log_infof("httpproxy: connection from %S:%u\n", client_ip, client_port);

	if(proxy_ctx->tls == 0)
	{
		log_info(str8_lit("httpproxy: TLS not available\n"));
		os_file_close(connection_socket);
		should_process_request = 0;
	}
	else
	{
		ssl = SSL_new(proxy_ctx->tls->ssl_ctx);
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
				log_info(str8_lit("httpproxy: TLS handshake complete\n"));
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
					String8 user_agent = http_header_get(&req->headers, str8_lit("User-Agent"));
					String8 referer = http_header_get(&req->headers, str8_lit("Referer"));
					log_infof("httpproxy: %S:%u %S %S%S%S%S%S%S%S\n", client_ip, client_port, str8_from_http_method(req->method),
					          req->path, user_agent.size > 0 ? str8_lit(" UA=\"") : str8_zero(),
					          user_agent.size > 0 ? user_agent : str8_zero(), user_agent.size > 0 ? str8_lit("\"") : str8_zero(),
					          referer.size > 0 ? str8_lit(" Referer=\"") : str8_zero(), referer.size > 0 ? referer : str8_zero(),
					          referer.size > 0 ? str8_lit("\"") : str8_zero());

					handle_http_request(req, ssl, connection_socket, proxy_ctx->config);
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

	log_info(str8_lit("httpproxy: connection closed\n"));
	log_scope_flush(scratch.arena);
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
	Log *setup_log = log_alloc();
	log_select(setup_log);
	log_scope_begin();

	proxy_ctx = push_array(arena, HTTPProxy_Context, 1);

	u64 worker_count = 0;
	String8 threads_str = cmd_line_string(cmd_line, str8_lit("threads"));
	if(threads_str.size > 0)
	{
		worker_count = u64_from_str8(threads_str, 10);
	}

	String8 acme_domain = cmd_line_string(cmd_line, str8_lit("acme-domain"));
	b32 acme_enabled = (acme_domain.size > 0);

	String8 port_str = cmd_line_string(cmd_line, str8_lit("port"));
	u16 listen_port = 8080;
	if(acme_enabled)
	{
		listen_port = 443;
	}
	if(port_str.size > 0)
	{
		listen_port = (u16)u64_from_str8(port_str, 10);
	}

	proxy_ctx->acme_ssl = SSL_CTX_new(TLS_client_method());
	SSL_CTX_set_min_proto_version(proxy_ctx->acme_ssl, TLS1_3_VERSION);
	SSL_CTX_set_default_verify_paths(proxy_ctx->acme_ssl);
	SSL_CTX_set_verify(proxy_ctx->acme_ssl, SSL_VERIFY_PEER, 0);

	proxy_ctx->challenges = acme_challenge_table_alloc(arena);
	log_info(str8_lit("httpproxy: ACME challenge handler initialized\n"));

	String8 cert_path = cmd_line_string(cmd_line, str8_lit("cert"));
	String8 key_path = cmd_line_string(cmd_line, str8_lit("key"));
	String8 acme_account_key_path = cmd_line_string(cmd_line, str8_lit("acme-account-key"));
	String8 acme_directory = cmd_line_string(cmd_line, str8_lit("acme-directory"));

	if(acme_enabled)
	{
		if(cert_path.size == 0)
		{
			cert_path = str8_lit("/var/lib/httpproxy/cert.pem");
		}
		if(key_path.size == 0)
		{
			key_path = str8_lit("/var/lib/httpproxy/key.pem");
		}
		if(acme_account_key_path.size == 0)
		{
			acme_account_key_path = str8_lit("/var/lib/httpproxy/acme-account.key");
		}
		if(acme_directory.size == 0)
		{
			acme_directory = str8_lit("https://acme-v02.api.letsencrypt.org/directory");
		}

		log_infof("httpproxy: ACME enabled for domain %S\n", acme_domain);
	}

	if(cert_path.size > 0 && key_path.size > 0)
	{
		OS_Handle cert_check = os_file_open(OS_AccessFlag_Read, cert_path);
		if(!os_handle_match(cert_check, os_handle_zero()))
		{
			os_file_close(cert_check);
			proxy_ctx->tls = tls_context_alloc(arena, cert_path, key_path);
			if(proxy_ctx->tls != 0)
			{
				log_infof("httpproxy: TLS enabled (cert: %S, key: %S)\n", cert_path, key_path);
			}
			else
			{
				log_error(str8_lit("httpproxy: TLS initialization failed\n"));
				log_scope_flush(scratch.arena);
				log_release(setup_log);
				arena_release(arena);
				scratch_end(scratch);
				return;
			}
		}
		else if(acme_enabled)
		{
			log_info(str8_lit("httpproxy: TLS certificates not found, waiting for ACME provisioning\n"));
		}
		else
		{
			log_error(str8_lit("httpproxy: TLS certificate paths specified but files not found\n"));
			log_scope_flush(scratch.arena);
			log_release(setup_log);
			arena_release(arena);
			scratch_end(scratch);
			return;
		}
	}

	if(acme_enabled)
	{
		proxy_ctx->acme_key = acme_account_key_alloc(arena, acme_account_key_path);
		if(proxy_ctx->acme_key == 0 || proxy_ctx->acme_key->pkey == 0)
		{
			log_error(str8_lit("httpproxy: failed to load or generate ACME account key\n"));
			log_scope_flush(scratch.arena);
			log_release(setup_log);
			arena_release(arena);
			scratch_end(scratch);
			return;
		}

		proxy_ctx->acme = acme_client_alloc(arena, proxy_ctx->acme_ssl, acme_directory, proxy_ctx->acme_key->pkey);
		if(proxy_ctx->acme == 0)
		{
			log_error(str8_lit("httpproxy: failed to initialize ACME client\n"));
			log_scope_flush(scratch.arena);
			log_release(setup_log);
			arena_release(arena);
			scratch_end(scratch);
			return;
		}

		log_infof("httpproxy: ACME client initialized (directory: %S)\n", acme_directory);

		proxy_ctx->renewal = push_array(arena, ACME_RenewalConfig, 1);
		proxy_ctx->renewal->arena = arena;
		proxy_ctx->renewal->domain = str8_copy(arena, acme_domain);
		proxy_ctx->renewal->cert_path = str8_copy(arena, cert_path);
		proxy_ctx->renewal->key_path = str8_copy(arena, key_path);
		proxy_ctx->renewal->renew_days_before_expiry = cert_renewal_days_threshold;
		proxy_ctx->renewal->is_live = 1;
		thread_launch(acme_renewal_thread, proxy_ctx->renewal);

		proxy_ctx->http80 = push_array(arena, HTTP80_Config, 1);
		proxy_ctx->http80->domain = str8_copy(arena, acme_domain);
		proxy_ctx->http80->is_live = 1;
		thread_launch(http80_handler_thread, proxy_ctx->http80);
	}

	proxy_ctx->config = push_array(arena, ProxyConfig, 1);
	proxy_ctx->config->file_root = cmd_line_string(cmd_line, str8_lit("file-root"));
	proxy_ctx->config->listen_port = listen_port;

	if(proxy_ctx->config->file_root.size > 0)
	{
		log_infof("httpproxy: serving files from %S\n", proxy_ctx->config->file_root);
		log_infof("httpproxy: listening on port %u\n", listen_port);
	}
	else
	{
		log_error(str8_lit("httpproxy: --file-root required\n"));
		log_scope_flush(scratch.arena);
		log_release(setup_log);
		arena_release(arena);
		scratch_end(scratch);
		return;
	}

	OS_Handle listen_socket = os_socket_listen_tcp(listen_port);
	if(os_handle_match(listen_socket, os_handle_zero()))
	{
		log_errorf("httpproxy: failed to listen on port %u\n", listen_port);
		log_scope_flush(scratch.arena);
		log_release(setup_log);
		arena_release(arena);
		scratch_end(scratch);
		return;
	}

	if(worker_count == 0)
	{
		u64 logical_cores = os_get_system_info()->logical_processor_count;
		worker_count = Max(4, logical_cores / 4);
	}

	proxy_ctx->workers = worker_pool_alloc(arena, worker_count);
	worker_pool_start(proxy_ctx->workers);

	log_infof("httpproxy: launched %lu worker threads\n", (unsigned long)worker_count);
	log_scope_flush(scratch.arena);

	for(;;)
	{
		Temp accept_scratch = scratch_begin(0, 0);
		Log *accept_log = log_alloc();
		log_select(accept_log);
		log_scope_begin();

		OS_Handle connection_socket = os_socket_accept(listen_socket);
		if(os_handle_match(connection_socket, os_handle_zero()))
		{
			log_error(str8_lit("httpproxy: failed to accept connection\n"));
			log_scope_flush(accept_scratch.arena);
			log_release(accept_log);
			scratch_end(accept_scratch);
			continue;
		}

		log_info(str8_lit("httpproxy: accepted connection\n"));
		log_scope_flush(accept_scratch.arena);
		log_release(accept_log);
		scratch_end(accept_scratch);

		work_queue_push(proxy_ctx->workers, connection_socket);
	}
}
