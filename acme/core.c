////////////////////////////////
//~ Base64 URL Encoding

internal String8
base64url_encode(Arena *arena, String8 data)
{
	u64 triple_count = data.size / 3;
	u64 remainder = data.size % 3;
	u64 out_size = triple_count * 4 + (remainder > 0 ? (remainder + 1) : 0);
	u8 *out = push_array(arena, u8, out_size);
	u64 out_pos = 0;

	u8 *ptr = data.str;
	u8 *opl = data.str + triple_count * 3;
	for(; ptr < opl; ptr += 3)
	{
		u32 triple = ((u32)ptr[0] << 16) | ((u32)ptr[1] << 8) | ((u32)ptr[2]);
		out[out_pos + 0] = base64[(triple >> 18) & 0x3F];
		out[out_pos + 1] = base64[(triple >> 12) & 0x3F];
		out[out_pos + 2] = base64[(triple >> 6) & 0x3F];
		out[out_pos + 3] = base64[triple & 0x3F];
		out_pos += 4;
	}

	if(remainder == 1)
	{
		u32 triple = ((u32)ptr[0] << 16);
		out[out_pos + 0] = base64[(triple >> 18) & 0x3F];
		out[out_pos + 1] = base64[(triple >> 12) & 0x3F];
		out_pos += 2;
	}
	else if(remainder == 2)
	{
		u32 triple = ((u32)ptr[0] << 16) | ((u32)ptr[1] << 8);
		out[out_pos + 0] = base64[(triple >> 18) & 0x3F];
		out[out_pos + 1] = base64[(triple >> 12) & 0x3F];
		out[out_pos + 2] = base64[(triple >> 6) & 0x3F];
		out_pos += 3;
	}

	return str8(out, out_pos);
}

////////////////////////////////
//~ JSON Web Key

internal JSON_Value *
acme_jwk_from_key(Arena *arena, EVP_PKEY *pkey)
{
	size_t pubkey_len = 0;
	if(EVP_PKEY_get_raw_public_key(pkey, 0, &pubkey_len) <= 0)
	{
		return json_value_null(arena);
	}

	u8 *pubkey_bytes = push_array(arena, u8, pubkey_len);
	if(EVP_PKEY_get_raw_public_key(pkey, pubkey_bytes, &pubkey_len) <= 0)
	{
		return json_value_null(arena);
	}

	String8 x_b64 = base64url_encode(arena, str8(pubkey_bytes, pubkey_len));

	JSON_Value *jwk = json_object_alloc(arena);
	json_object_add(arena, jwk, str8_lit("kty"), json_value_from_string(arena, str8_lit("OKP")));
	json_object_add(arena, jwk, str8_lit("crv"), json_value_from_string(arena, str8_lit("Ed25519")));
	json_object_add(arena, jwk, str8_lit("x"), json_value_from_string(arena, x_b64));

	return jwk;
}

////////////////////////////////
//~ JSON Web Signature

internal String8
acme_jws_sign(Arena *arena, EVP_PKEY *pkey, JSON_Value *protected_header, String8 payload)
{
	Temp scratch = scratch_begin(&arena, 1);
	String8 protected_json = json_serialize(scratch.arena, protected_header);
	String8 protected_b64 = base64url_encode(scratch.arena, protected_json);
	String8 payload_b64 = base64url_encode(scratch.arena, payload);
	String8 signing_input = str8f(scratch.arena, "%S.%S", protected_b64, payload_b64);

	EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
	if(md_ctx == 0)
	{
		scratch_end(scratch);
		return str8_zero();
	}

	if(EVP_DigestSignInit(md_ctx, 0, 0, 0, pkey) <= 0)
	{
		EVP_MD_CTX_free(md_ctx);
		scratch_end(scratch);
		return str8_zero();
	}

	size_t sig_len = 0;
	if(EVP_DigestSign(md_ctx, 0, &sig_len, signing_input.str, signing_input.size) <= 0)
	{
		EVP_MD_CTX_free(md_ctx);
		scratch_end(scratch);
		return str8_zero();
	}

	u8 *signature = push_array(scratch.arena, u8, sig_len);
	if(EVP_DigestSign(md_ctx, signature, &sig_len, signing_input.str, signing_input.size) <= 0)
	{
		EVP_MD_CTX_free(md_ctx);
		scratch_end(scratch);
		return str8_zero();
	}

	EVP_MD_CTX_free(md_ctx);
	String8 signature_b64 = base64url_encode(scratch.arena, str8(signature, sig_len));

	JSON_Value *jws = json_object_alloc(arena);
	json_object_add(arena, jws, str8_lit("protected"), json_value_from_string(arena, protected_b64));
	json_object_add(arena, jws, str8_lit("payload"), json_value_from_string(arena, payload_b64));
	json_object_add(arena, jws, str8_lit("signature"), json_value_from_string(arena, signature_b64));

	String8 result = json_serialize(arena, jws);
	scratch_end(scratch);
	return result;
}

////////////////////////////////
//~ ACME Key Authorization

internal String8
acme_key_authorization(Arena *arena, EVP_PKEY *pkey, String8 token)
{
	Temp scratch = scratch_begin(&arena, 1);

	JSON_Value *jwk = acme_jwk_from_key(scratch.arena, pkey);
	String8 jwk_json = json_serialize(scratch.arena, jwk);

	EVP_MD_CTX *md_ctx = EVP_MD_CTX_new();
	if(md_ctx == 0)
	{
		scratch_end(scratch);
		return str8_zero();
	}

	if(EVP_DigestInit_ex(md_ctx, EVP_sha256(), 0) <= 0)
	{
		EVP_MD_CTX_free(md_ctx);
		scratch_end(scratch);
		return str8_zero();
	}

	if(EVP_DigestUpdate(md_ctx, jwk_json.str, jwk_json.size) <= 0)
	{
		EVP_MD_CTX_free(md_ctx);
		scratch_end(scratch);
		return str8_zero();
	}

	u8 hash[32];
	unsigned int hash_len = 0;
	if(EVP_DigestFinal_ex(md_ctx, hash, &hash_len) <= 0)
	{
		EVP_MD_CTX_free(md_ctx);
		scratch_end(scratch);
		return str8_zero();
	}

	EVP_MD_CTX_free(md_ctx);

	String8 thumbprint = base64url_encode(scratch.arena, str8(hash, hash_len));
	String8 result = str8f(arena, "%S.%S", token, thumbprint);

	scratch_end(scratch);
	return result;
}

////////////////////////////////
//~ ACME Certificate Operations

internal EVP_PKEY *
acme_generate_cert_key(void)
{
	EVP_PKEY *key = EVP_PKEY_new();
	if(key == 0)
	{
		return 0;
	}

	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, 0);
	if(ctx == 0)
	{
		EVP_PKEY_free(key);
		return 0;
	}

	if(EVP_PKEY_keygen_init(ctx) <= 0)
	{
		EVP_PKEY_CTX_free(ctx);
		EVP_PKEY_free(key);
		return 0;
	}

	EVP_PKEY *result = 0;
	if(EVP_PKEY_keygen(ctx, &result) <= 0)
	{
		EVP_PKEY_CTX_free(ctx);
		EVP_PKEY_free(key);
		return 0;
	}

	EVP_PKEY_CTX_free(ctx);
	EVP_PKEY_free(key);
	return result;
}

internal String8
acme_generate_csr(Arena *arena, EVP_PKEY *key, String8 domain)
{
	X509_REQ *req = X509_REQ_new();
	X509_REQ_set_version(req, 0);

	X509_NAME *name = X509_REQ_get_subject_name(req);
	X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char *)domain.str, (int)domain.size, -1, 0);

	X509_REQ_set_pubkey(req, key);
	X509_REQ_sign(req, key, 0);

	BIO *bio = BIO_new(BIO_s_mem());
	i2d_X509_REQ_bio(bio, req);

	BUF_MEM *mem = 0;
	BIO_get_mem_ptr(bio, &mem);

	String8 csr_der = str8((u8 *)mem->data, mem->length);
	String8 csr_b64 = base64url_encode(arena, csr_der);

	BIO_free(bio);
	X509_REQ_free(req);

	return csr_b64;
}

internal u64
acme_cert_days_until_expiry(Arena *arena, String8 cert_pem_path)
{
	Temp scratch = scratch_begin(&arena, 1);

	OS_Handle file = os_file_open(OS_AccessFlag_Read, cert_pem_path);
	if(os_handle_match(file, os_handle_zero()))
	{
		scratch_end(scratch);
		return 0;
	}

	OS_FileProperties props = os_properties_from_file(file);
	u8 *cert_pem_data = push_array(scratch.arena, u8, props.size);
	String8 cert_pem = str8(cert_pem_data, props.size);
	os_file_read(file, rng_1u64(0, props.size), cert_pem.str);
	os_file_close(file);

	BIO *bio = BIO_new_mem_buf(cert_pem.str, (int)cert_pem.size);
	if(bio == 0)
	{
		scratch_end(scratch);
		return 0;
	}

	X509 *cert = PEM_read_bio_X509(bio, 0, 0, 0);
	BIO_free(bio);

	if(cert == 0)
	{
		scratch_end(scratch);
		return 0;
	}

	const ASN1_TIME *not_after = X509_get0_notAfter(cert);
	if(not_after == 0)
	{
		X509_free(cert);
		scratch_end(scratch);
		return 0;
	}

	ASN1_TIME *now = ASN1_TIME_new();
	X509_gmtime_adj(now, 0);

	int day_diff = 0;
	int sec_diff = 0;
	ASN1_TIME_diff(&day_diff, &sec_diff, now, not_after);

	ASN1_TIME_free(now);
	X509_free(cert);
	scratch_end(scratch);

	if(day_diff < 0)
	{
		return 0;
	}

	return (u64)day_diff;
}
