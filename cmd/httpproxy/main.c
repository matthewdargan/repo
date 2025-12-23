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
//~ ACME Integration (uses acme/ layer)

global ACME_Client *acme_client = 0;

////////////////////////////////
//~ ACME Orchestration

internal b32
acme_provision_certificate(ACME_Client *client, String8 domain, EVP_PKEY *cert_key, String8 cert_output_path,
                           String8 key_output_path)
{
	Temp scratch = scratch_begin(&client->arena, 1);

	log_infof("acme: starting certificate provisioning for %S\n", domain);

	String8 order_url = acme_create_order(client, domain);
	if(order_url.size == 0)
	{
		log_errorf("acme: failed to create order for %S\n", domain);
		scratch_end(scratch);
		return 0;
	}

	String8 authz_url = acme_get_authorization_url(client, order_url);
	if(authz_url.size == 0)
	{
		log_error(str8_lit("acme: failed to get authorization URL\n"));
		scratch_end(scratch);
		return 0;
	}

	ACME_ChallengeInfo challenge = acme_get_http01_challenge(client, authz_url);
	if(challenge.token.size == 0 || challenge.url.size == 0)
	{
		log_error(str8_lit("acme: failed to get http-01 challenge\n"));
		scratch_end(scratch);
		return 0;
	}

	String8 key_auth = acme_key_authorization(scratch.arena, client->account_key, challenge.token);
	if(key_auth.size == 0)
	{
		log_error(str8_lit("acme: failed to compute key authorization\n"));
		scratch_end(scratch);
		return 0;
	}

	if(acme_challenges != 0)
	{
		acme_challenge_add(acme_challenges, challenge.token, key_auth);
		log_infof("acme: stored challenge response for token %S\n", challenge.token);
	}
	else
	{
		log_error(str8_lit("acme: challenge table not initialized\n"));
		scratch_end(scratch);
		return 0;
	}

	if(!acme_notify_challenge_ready(client, challenge.url))
	{
		log_error(str8_lit("acme: failed to notify challenge ready\n"));
		if(acme_challenges != 0)
		{
			acme_challenge_remove(acme_challenges, challenge.token);
		}
		scratch_end(scratch);
		return 0;
	}

	log_info(str8_lit("acme: polling challenge status...\n"));
	b32 challenge_valid = acme_poll_challenge_status(client, challenge.url, 30, 2000);

	if(acme_challenges != 0)
	{
		acme_challenge_remove(acme_challenges, challenge.token);
	}

	if(!challenge_valid)
	{
		log_error(str8_lit("acme: challenge validation failed\n"));
		scratch_end(scratch);
		return 0;
	}

	log_info(str8_lit("acme: challenge validated successfully\n"));

	String8 finalize_result = acme_finalize_order(client, order_url, cert_key, domain);
	if(finalize_result.size == 0)
	{
		log_error(str8_lit("acme: failed to finalize order\n"));
		scratch_end(scratch);
		return 0;
	}

	log_info(str8_lit("acme: polling order status...\n"));
	if(!acme_poll_order_status(client, order_url, 30, 2000))
	{
		log_error(str8_lit("acme: order validation failed\n"));
		scratch_end(scratch);
		return 0;
	}

	String8 cert_pem = acme_download_certificate(client, order_url);
	if(cert_pem.size == 0)
	{
		log_error(str8_lit("acme: failed to download certificate\n"));
		scratch_end(scratch);
		return 0;
	}

	log_infof("acme: saving certificate to %S\n", cert_output_path);
	OS_Handle cert_file = os_file_open(OS_AccessFlag_Write, cert_output_path);
	if(os_handle_match(cert_file, os_handle_zero()))
	{
		log_errorf("acme: failed to open certificate file %S\n", cert_output_path);
		scratch_end(scratch);
		return 0;
	}
	os_file_write(cert_file, rng_1u64(0, cert_pem.size), cert_pem.str);
	os_file_close(cert_file);

	log_infof("acme: saving private key to %S\n", key_output_path);
	BIO *key_bio = BIO_new(BIO_s_mem());
	PEM_write_bio_PrivateKey(key_bio, cert_key, 0, 0, 0, 0, 0);
	BUF_MEM *key_mem = 0;
	BIO_get_mem_ptr(key_bio, &key_mem);

	OS_Handle key_file = os_file_open(OS_AccessFlag_Write, key_output_path);
	if(os_handle_match(key_file, os_handle_zero()))
	{
		log_errorf("acme: failed to open key file %S\n", key_output_path);
		BIO_free(key_bio);
		scratch_end(scratch);
		return 0;
	}
	os_file_write(key_file, rng_1u64(0, key_mem->length), key_mem->data);
	os_file_close(key_file);
	BIO_free(key_bio);

	log_infof("acme: certificate provisioning complete for %S\n", domain);

	if(tls_context != 0 && tls_context->ssl_ctx != 0)
	{
		log_info(str8_lit("acme: reloading TLS context with new certificate\n"));

		char cert_buf[4096];
		char key_buf[4096];

		if(cert_output_path.size >= sizeof(cert_buf) || key_output_path.size >= sizeof(key_buf))
		{
			log_error(str8_lit("acme: certificate or key path too long for reload\n"));
			scratch_end(scratch);
			return 1;
		}

		MemoryCopy(cert_buf, cert_output_path.str, cert_output_path.size);
		cert_buf[cert_output_path.size] = 0;
		MemoryCopy(key_buf, key_output_path.str, key_output_path.size);
		key_buf[key_output_path.size] = 0;

		SSL_CTX *new_ctx = SSL_CTX_new(TLS_server_method());
		if(new_ctx == 0)
		{
			log_error(str8_lit("acme: failed to create new SSL context\n"));
			scratch_end(scratch);
			return 1;
		}

		SSL_CTX_set_min_proto_version(new_ctx, TLS1_3_VERSION);

		if(SSL_CTX_use_certificate_chain_file(new_ctx, cert_buf) <= 0)
		{
			log_errorf("acme: failed to load new certificate from %S\n", cert_output_path);
			ERR_print_errors_fp(stderr);
			SSL_CTX_free(new_ctx);
			scratch_end(scratch);
			return 1;
		}

		if(SSL_CTX_use_PrivateKey_file(new_ctx, key_buf, SSL_FILETYPE_PEM) <= 0)
		{
			log_errorf("acme: failed to load new private key from %S\n", key_output_path);
			ERR_print_errors_fp(stderr);
			SSL_CTX_free(new_ctx);
			scratch_end(scratch);
			return 1;
		}

		if(!SSL_CTX_check_private_key(new_ctx))
		{
			log_error(str8_lit("acme: new private key does not match certificate\n"));
			SSL_CTX_free(new_ctx);
			scratch_end(scratch);
			return 1;
		}

		SSL_CTX *old_ctx = tls_context->ssl_ctx;
		tls_context->ssl_ctx = new_ctx;
		tls_context->cert_path = str8_copy(tls_context->arena, cert_output_path);
		tls_context->key_path = str8_copy(tls_context->arena, key_output_path);

		if(old_ctx != 0)
		{
			SSL_CTX_free(old_ctx);
		}

		log_info(str8_lit("acme: TLS context reloaded successfully\n"));
	}

	scratch_end(scratch);
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

global ACME_RenewalConfig *renewal_config = 0;

internal void
acme_renewal_thread(void *ptr)
{
	ACME_RenewalConfig *config = (ACME_RenewalConfig *)ptr;
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	log_info(str8_lit("acme: renewal thread started\n"));

	if(acme_client != 0 && config->cert_path.size > 0)
	{
		OS_Handle cert_file = os_file_open(OS_AccessFlag_Read, config->cert_path);
		if(!os_handle_match(cert_file, os_handle_zero()))
		{
			os_file_close(cert_file);
			log_infof("acme: found existing certificate at %S\n", config->cert_path);
		}
		else
		{
			log_infof("acme: no certificate found at %S, provisioning new certificate\n", config->cert_path);
			EVP_PKEY *cert_key = acme_generate_cert_key();
			if(cert_key != 0)
			{
				b32 success =
				    acme_provision_certificate(acme_client, config->domain, cert_key, config->cert_path, config->key_path);

				if(success)
				{
					log_info(str8_lit("acme: initial certificate provisioning complete\n"));
				}
				else
				{
					log_error(str8_lit("acme: initial certificate provisioning failed\n"));
					EVP_PKEY_free(cert_key);
				}
			}
			else
			{
				log_error(str8_lit("acme: failed to generate certificate key for initial provisioning\n"));
			}
		}
	}

	LogScopeResult result = log_scope_end(scratch.arena);
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

	log_release(log);
	scratch_end(scratch);

	for(;;)
	{
		os_sleep_milliseconds(24 * 60 * 60 * 1000);

		if(!config->is_live)
		{
			break;
		}

		if(acme_client == 0 || config->cert_path.size == 0)
		{
			continue;
		}

		Temp renewal_scratch = scratch_begin(0, 0);
		Log *renewal_log = log_alloc();
		log_select(renewal_log);
		log_scope_begin();

		u64 days_left = acme_cert_days_until_expiry(config->arena, config->cert_path);
		log_infof("acme: certificate expires in %u days\n", days_left);

		b32 should_renew = 0;
		if(days_left == 0)
		{
			log_error(str8_lit("acme: certificate has expired, attempting renewal\n"));
			should_renew = 1;
		}
		else if(days_left <= config->renew_days_before_expiry)
		{
			log_infof("acme: renewing certificate (%u days until expiry)\n", days_left);
			should_renew = 1;
		}

		if(should_renew)
		{
			EVP_PKEY *cert_key = acme_generate_cert_key();
			if(cert_key == 0)
			{
				log_error(str8_lit("acme: failed to generate certificate key for renewal\n"));

				LogScopeResult renewal_result = log_scope_end(renewal_scratch.arena);
				if(renewal_result.strings[LogMsgKind_Info].size > 0)
				{
					fwrite(renewal_result.strings[LogMsgKind_Info].str, 1, renewal_result.strings[LogMsgKind_Info].size, stdout);
					fflush(stdout);
				}
				if(renewal_result.strings[LogMsgKind_Error].size > 0)
				{
					fwrite(renewal_result.strings[LogMsgKind_Error].str, 1, renewal_result.strings[LogMsgKind_Error].size,
					       stderr);
					fflush(stderr);
				}

				log_release(renewal_log);
				scratch_end(renewal_scratch);
				continue;
			}

			b32 success =
			    acme_provision_certificate(acme_client, config->domain, cert_key, config->cert_path, config->key_path);

			if(success)
			{
				log_info(str8_lit("acme: certificate renewed successfully\n"));
			}
			else
			{
				log_error(str8_lit("acme: certificate renewal failed\n"));
				EVP_PKEY_free(cert_key);
			}
		}

		LogScopeResult renewal_result = log_scope_end(renewal_scratch.arena);
		if(renewal_result.strings[LogMsgKind_Info].size > 0)
		{
			fwrite(renewal_result.strings[LogMsgKind_Info].str, 1, renewal_result.strings[LogMsgKind_Info].size, stdout);
			fflush(stdout);
		}
		if(renewal_result.strings[LogMsgKind_Error].size > 0)
		{
			fwrite(renewal_result.strings[LogMsgKind_Error].str, 1, renewal_result.strings[LogMsgKind_Error].size, stderr);
			fflush(stderr);
		}

		log_release(renewal_log);
		scratch_end(renewal_scratch);
	}

	fprintf(stdout, "acme: renewal thread stopped\n");
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

	if(acme_challenges == 0)
	{
		return 0;
	}

	String8 key_auth = acme_challenge_lookup(acme_challenges, token);
	if(key_auth.size == 0)
	{
		Temp scratch = scratch_begin(0, 0);
		HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_404_NotFound);
		http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
		http_header_add(scratch.arena, &res->headers, str8_lit("Connection"), str8_lit("close"));
		res->body = str8_lit("Challenge not found");
		String8 content_length = str8_from_u64(scratch.arena, res->body.size, 10, 0, 0);
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

////////////////////////////////
//~ HTTP Port 80 Handler (ACME challenges + HTTPS redirect)

typedef struct HTTP80_Config HTTP80_Config;
struct HTTP80_Config
{
	String8 domain;
	b32 is_live;
};

global HTTP80_Config *http80_config = 0;

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

		LogScopeResult result = log_scope_end(scratch.arena);
		if(result.strings[LogMsgKind_Error].size > 0)
		{
			fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
			fflush(stderr);
		}

		log_release(log);
		scratch_end(scratch);
		return;
	}

	log_info(str8_lit("http80: listening on port 80 for ACME challenges and redirects\n"));

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}

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

		u8 buffer[KB(16)];
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

				for(u64 written = 0; written < response.size;)
				{
					ssize_t result = write(client_fd, response.str + written, response.size - written);
					if(result <= 0)
					{
						break;
					}
					written += result;
				}
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

	DateTime now = os_now_universal_time();
	String8 timestamp = str8_from_datetime(scratch.arena, now);
	log_infof("[%S] httpproxy: error response %u %S\n", timestamp, (u32)status, message.size > 0 ? message : str8_zero());

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

internal void
handle_http_request(HTTP_Request *req, SSL *client_ssl, OS_Handle client_socket, ProxyConfig *config)
{
	Temp scratch = scratch_begin(0, 0);

	if(handle_acme_challenge(req, client_ssl, client_socket))
	{
		DateTime now = os_now_universal_time();
		String8 timestamp = str8_from_datetime(scratch.arena, now);
		log_infof("[%S] httpproxy: served ACME challenge\n", timestamp);
		scratch_end(scratch);
		return;
	}

	Backend *backend = find_backend_for_path(config, req->path);
	if(backend == 0)
	{
		DateTime now = os_now_universal_time();
		String8 timestamp = str8_from_datetime(scratch.arena, now);
		log_infof("[%S] httpproxy: no backend for path %S\n", timestamp, req->path);
		send_error_response(client_ssl, client_socket, HTTP_Status_404_NotFound,
		                    str8_lit("No backend configured for this path"));
		scratch_end(scratch);
		return;
	}

	u64 proxy_start_us = os_now_microseconds();
	log_infof("httpproxy: proxy to %S:%u\n", backend->backend_host, backend->backend_port);

	proxy_to_backend(req, backend, client_ssl, client_socket);

	u64 proxy_end_us = os_now_microseconds();
	u64 duration_us = proxy_end_us - proxy_start_us;
	log_infof("httpproxy: proxy complete (%u μs)\n", duration_us);

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

	DateTime now = os_now_universal_time();
	String8 timestamp = str8_from_datetime(scratch.arena, now);
	log_infof("[%S] httpproxy: connection from %S:%u\n", timestamp, client_ip, client_port);

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
					String8 user_agent = http_header_get(&req->headers, str8_lit("User-Agent"));
					String8 referer = http_header_get(&req->headers, str8_lit("Referer"));
					log_infof("[%S] %S:%u %S %S%S%S%S%S%S%S\n", timestamp, client_ip, client_port,
					          str8_from_http_method(req->method), req->path,
					          user_agent.size > 0 ? str8_lit(" UA=\"") : str8_zero(),
					          user_agent.size > 0 ? user_agent : str8_zero(), user_agent.size > 0 ? str8_lit("\"") : str8_zero(),
					          referer.size > 0 ? str8_lit(" Referer=\"") : str8_zero(), referer.size > 0 ? referer : str8_zero(),
					          referer.size > 0 ? str8_lit("\"") : str8_zero());

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

	acme_client_ssl_ctx = SSL_CTX_new(TLS_client_method());
	SSL_CTX_set_min_proto_version(acme_client_ssl_ctx, TLS1_3_VERSION);
	SSL_CTX_set_default_verify_paths(acme_client_ssl_ctx);
	SSL_CTX_set_verify(acme_client_ssl_ctx, SSL_VERIFY_PEER, 0);

	acme_challenges = acme_challenge_table_alloc(arena);
	fprintf(stdout, "httpproxy: ACME challenge handler initialized\n");
	fflush(stdout);

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

		fprintf(stdout, "httpproxy: ACME enabled for domain %.*s\n", (int)acme_domain.size, acme_domain.str);
		fflush(stdout);
	}

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
			if(acme_enabled)
			{
				fprintf(stdout, "httpproxy: TLS certificates not found, waiting for ACME provisioning\n");
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
	}

	if(acme_enabled)
	{
		acme_account_key = acme_account_key_alloc(arena, acme_account_key_path);
		if(acme_account_key == 0 || acme_account_key->pkey == 0)
		{
			fprintf(stderr, "httpproxy: failed to load or generate ACME account key\n");
			fflush(stderr);
			arena_release(arena);
			scratch_end(scratch);
			return;
		}

		acme_client = acme_client_alloc(arena, acme_client_ssl_ctx, acme_directory, acme_account_key->pkey);
		if(acme_client == 0)
		{
			fprintf(stderr, "httpproxy: failed to initialize ACME client\n");
			fflush(stderr);
			arena_release(arena);
			scratch_end(scratch);
			return;
		}

		fprintf(stdout, "httpproxy: ACME client initialized (directory: %.*s)\n", (int)acme_directory.size,
		        acme_directory.str);
		fflush(stdout);

		renewal_config = push_array(arena, ACME_RenewalConfig, 1);
		renewal_config->arena = arena;
		renewal_config->domain = str8_copy(arena, acme_domain);
		renewal_config->cert_path = str8_copy(arena, cert_path);
		renewal_config->key_path = str8_copy(arena, key_path);
		renewal_config->renew_days_before_expiry = 30;
		renewal_config->is_live = 1;

		Thread renewal_thread = thread_launch(acme_renewal_thread, renewal_config);
		thread_detach(renewal_thread);

		http80_config = push_array(arena, HTTP80_Config, 1);
		http80_config->domain = str8_copy(arena, acme_domain);
		http80_config->is_live = 1;

		Thread http80_thread = thread_launch(http80_handler_thread, http80_config);
		thread_detach(http80_thread);
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
			Temp accept_scratch = scratch_begin(0, 0);
			Log *accept_log = log_alloc();
			log_select(accept_log);
			log_scope_begin();

			OS_Handle connection_socket = os_socket_accept(listen_socket);
			if(os_handle_match(connection_socket, os_handle_zero()))
			{
				log_error(str8_lit("httpproxy: failed to accept connection\n"));

				LogScopeResult result = log_scope_end(accept_scratch.arena);
				if(result.strings[LogMsgKind_Error].size > 0)
				{
					fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
					fflush(stderr);
				}

				log_release(accept_log);
				scratch_end(accept_scratch);
				continue;
			}

			DateTime accept_time = os_now_universal_time();
			String8 accept_timestamp = str8_from_datetime(accept_scratch.arena, accept_time);
			log_infof("[%S] httpproxy: accepted connection\n", accept_timestamp);

			LogScopeResult result = log_scope_end(accept_scratch.arena);
			if(result.strings[LogMsgKind_Info].size > 0)
			{
				fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
				fflush(stdout);
			}

			log_release(accept_log);
			scratch_end(accept_scratch);

			work_queue_push(worker_pool, connection_socket);
		}
	}

	arena_release(arena);
	scratch_end(scratch);
}
