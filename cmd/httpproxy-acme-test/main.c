// Test harness for ACME implementation
#include <netdb.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <sys/socket.h>
#include <sys/types.h>

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
//~ Test Functions

internal b32
test_json_roundtrip(Arena *arena)
{
	JSON_Value *obj = json_object_alloc(arena);
	json_object_add(arena, obj, str8_lit("test"), json_value_from_string(arena, str8_lit("value")));
	json_object_add(arena, obj, str8_lit("number"), json_value_from_number(arena, 42.5));
	json_object_add(arena, obj, str8_lit("bool"), json_value_from_bool(arena, 1));

	String8 serialized = json_serialize(arena, obj);
	JSON_Value *parsed = json_parse(arena, serialized);

	if(parsed == 0 || parsed->kind != JSON_ValueKind_Object)
	{
		return 0;
	}

	JSON_Value *test_val = json_object_get(parsed, str8_lit("test"));
	JSON_Value *num_val = json_object_get(parsed, str8_lit("number"));
	JSON_Value *bool_val = json_object_get(parsed, str8_lit("bool"));

	return test_val != 0 && str8_match(test_val->string, str8_lit("value"), 0) && num_val != 0 &&
	       num_val->number == 42.5 && bool_val != 0 && bool_val->boolean == 1;
}

internal b32
test_base64url(Arena *arena)
{
	String8 input = str8_lit("Hello, World!");
	String8 encoded = base64url_encode(arena, input);

	// Expected: SGVsbG8sIFdvcmxkIQ (no padding)
	return str8_match(encoded, str8_lit("SGVsbG8sIFdvcmxkIQ"), 0);
}

internal b32
test_ed25519_key_generation(Arena *arena)
{
	EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, 0);
	if(pctx == 0)
	{
		return 0;
	}

	EVP_PKEY_keygen_init(pctx);

	EVP_PKEY *pkey = 0;
	b32 success = EVP_PKEY_keygen(pctx, &pkey) > 0;

	EVP_PKEY_CTX_free(pctx);

	if(!success || pkey == 0)
	{
		return 0;
	}

	size_t pubkey_len = 0;
	b32 result = EVP_PKEY_get_raw_public_key(pkey, 0, &pubkey_len) > 0 && pubkey_len == 32;

	EVP_PKEY_free(pkey);
	return result;
}

internal b32
test_jwk_creation(Arena *arena)
{
	EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, 0);
	EVP_PKEY_keygen_init(pctx);

	EVP_PKEY *pkey = 0;
	EVP_PKEY_keygen(pctx, &pkey);
	EVP_PKEY_CTX_free(pctx);

	JSON_Value *jwk = acme_jwk_from_key(arena, pkey);

	b32 result = jwk != 0 && jwk->kind == JSON_ValueKind_Object;
	if(result)
	{
		JSON_Value *kty = json_object_get(jwk, str8_lit("kty"));
		JSON_Value *crv = json_object_get(jwk, str8_lit("crv"));
		JSON_Value *x = json_object_get(jwk, str8_lit("x"));
		result = kty != 0 && str8_match(kty->string, str8_lit("OKP"), 0) && crv != 0 &&
		         str8_match(crv->string, str8_lit("Ed25519"), 0) && x != 0 && x->string.size > 0;
	}

	EVP_PKEY_free(pkey);
	return result;
}

internal b32
test_jws_signing(Arena *arena)
{
	EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, 0);
	EVP_PKEY_keygen_init(pctx);

	EVP_PKEY *pkey = 0;
	EVP_PKEY_keygen(pctx, &pkey);
	EVP_PKEY_CTX_free(pctx);

	JSON_Value *protected = json_object_alloc(arena);
	json_object_add(arena, protected, str8_lit("alg"), json_value_from_string(arena, str8_lit("EdDSA")));

	String8 payload = str8_lit("{\"test\":\"data\"}");

	String8 jws_json = acme_jws_sign(arena, pkey, protected, payload);

	b32 result = jws_json.size > 0 && str8_find_needle(jws_json, 0, str8_lit("\"signature\":"), 0) < jws_json.size;

	EVP_PKEY_free(pkey);
	return result;
}

internal b32
test_csr_generation(Arena *arena)
{
	EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, 0);
	EVP_PKEY_keygen_init(pctx);

	EVP_PKEY *cert_key = 0;
	EVP_PKEY_keygen(pctx, &cert_key);
	EVP_PKEY_CTX_free(pctx);

	X509_REQ *req = X509_REQ_new();
	X509_REQ_set_version(req, 0);

	X509_NAME *name = X509_REQ_get_subject_name(req);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)"dargs.dev", -1, -1, 0);

	X509_REQ_set_pubkey(req, cert_key);
	X509_REQ_sign(req, cert_key, 0);

	BIO *bio = BIO_new(BIO_s_mem());
	i2d_X509_REQ_bio(bio, req);

	BUF_MEM *mem = 0;
	BIO_get_mem_ptr(bio, &mem);

	b32 result = mem->length > 0;

	BIO_free(bio);
	X509_REQ_free(req);
	EVP_PKEY_free(cert_key);
	return result;
}

internal b32
test_http_parsing(Arena *arena)
{
	String8 request_str = str8_lit("GET /test HTTP/1.1\r\n"
	                               "Host: dargs.dev\r\n"
	                               "User-Agent: test\r\n"
	                               "\r\n");

	HTTP_Request *req = http_request_parse(arena, request_str);

	return req != 0 && req->method == HTTP_Method_GET && str8_match(req->path, str8_lit("/test"), 0) &&
	       req->headers.count > 0;
}

////////////////////////////////
//~ Test Runner

internal void
run_tests(Arena *arena)
{
	u32 passed = 0;
	u32 failed = 0;

	if(test_json_roundtrip(arena))
	{
		log_info(str8_lit("PASS: json_roundtrip\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: json_roundtrip\n"));
		failed += 1;
	}

	if(test_base64url(arena))
	{
		log_info(str8_lit("PASS: base64url\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: base64url\n"));
		failed += 1;
	}

	if(test_ed25519_key_generation(arena))
	{
		log_info(str8_lit("PASS: ed25519_key_generation\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: ed25519_key_generation\n"));
		failed += 1;
	}

	if(test_jwk_creation(arena))
	{
		log_info(str8_lit("PASS: jwk_creation\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: jwk_creation\n"));
		failed += 1;
	}

	if(test_jws_signing(arena))
	{
		log_info(str8_lit("PASS: jws_signing\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: jws_signing\n"));
		failed += 1;
	}

	if(test_csr_generation(arena))
	{
		log_info(str8_lit("PASS: csr_generation\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: csr_generation\n"));
		failed += 1;
	}

	if(test_http_parsing(arena))
	{
		log_info(str8_lit("PASS: http_parsing\n"));
		passed += 1;
	}
	else
	{
		log_error(str8_lit("FAIL: http_parsing\n"));
		failed += 1;
	}

	log_infof("test: %u passed, %u failed\n", passed, failed);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	run_tests(scratch.arena);

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
	scratch_end(scratch);
}
