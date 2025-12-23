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
	int key_type = EVP_PKEY_id(pkey);
	if(key_type == EVP_PKEY_EC)
	{
		BIGNUM *x = 0;
		BIGNUM *y = 0;
		if(!EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_EC_PUB_X, &x) ||
		   !EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_EC_PUB_Y, &y))
		{
			BN_free(x);
			BN_free(y);
			return json_value_null(arena);
		}

		u8 x_bytes[32];
		u8 y_bytes[32];
		MemoryZero(x_bytes, sizeof(x_bytes));
		MemoryZero(y_bytes, sizeof(y_bytes));

		BN_bn2binpad(x, x_bytes, 32);
		BN_bn2binpad(y, y_bytes, 32);

		BN_free(x);
		BN_free(y);

		String8 x_b64 = base64url_encode(arena, str8(x_bytes, 32));
		String8 y_b64 = base64url_encode(arena, str8(y_bytes, 32));

		JSON_Value *jwk = json_object_alloc(arena);
		json_object_add(arena, jwk, str8_lit("crv"), json_value_from_string(arena, str8_lit("P-256")));
		json_object_add(arena, jwk, str8_lit("kty"), json_value_from_string(arena, str8_lit("EC")));
		json_object_add(arena, jwk, str8_lit("x"), json_value_from_string(arena, x_b64));
		json_object_add(arena, jwk, str8_lit("y"), json_value_from_string(arena, y_b64));

		return jwk;
	}

	return json_value_null(arena);
}

////////////////////////////////
//~ ECDSA Signature Conversion

internal String8
ecdsa_raw_from_der(Arena *arena, String8 der_sig, u64 coord_size)
{
	if(der_sig.size < 8)
	{
		return str8_zero();
	}

	u8 *p = der_sig.str;
	if(p[0] != 0x30)
	{
		return str8_zero();
	}

	u64 pos = 1;
	u64 seq_len = p[pos];
	pos += 1;
	if(seq_len & 0x80)
	{
		u64 len_bytes = seq_len & 0x7F;
		if(pos + len_bytes > der_sig.size)
		{
			return str8_zero();
		}
		pos += len_bytes;
	}

	if(pos >= der_sig.size || p[pos] != 0x02)
	{
		return str8_zero();
	}
	pos += 1;

	u64 r_len = p[pos];
	pos += 1;
	if(r_len & 0x80)
	{
		u64 len_bytes = r_len & 0x7F;
		if(pos + len_bytes > der_sig.size)
		{
			return str8_zero();
		}
		r_len = 0;
		for(u64 i = 0; i < len_bytes; i += 1)
		{
			r_len = (r_len << 8) | p[pos + i];
		}
		pos += len_bytes;
	}

	if(pos + r_len > der_sig.size)
	{
		return str8_zero();
	}

	u8 *r_bytes = p + pos;
	pos += r_len;

	if(pos >= der_sig.size || p[pos] != 0x02)
	{
		return str8_zero();
	}
	pos += 1;

	u64 s_len = p[pos];
	pos += 1;
	if(s_len & 0x80)
	{
		u64 len_bytes = s_len & 0x7F;
		if(pos + len_bytes > der_sig.size)
		{
			return str8_zero();
		}
		s_len = 0;
		for(u64 i = 0; i < len_bytes; i += 1)
		{
			s_len = (s_len << 8) | p[pos + i];
		}
		pos += len_bytes;
	}

	if(pos + s_len > der_sig.size)
	{
		return str8_zero();
	}

	u8 *s_bytes = p + pos;

	if(r_len == coord_size + 1 && r_bytes[0] == 0)
	{
		r_bytes += 1;
		r_len -= 1;
	}

	if(s_len == coord_size + 1 && s_bytes[0] == 0)
	{
		s_bytes += 1;
		s_len -= 1;
	}

	u8 *raw = push_array(arena, u8, coord_size * 2);
	MemoryZero(raw, coord_size * 2);

	u64 r_offset = (r_len < coord_size) ? (coord_size - r_len) : 0;
	u64 s_offset = (s_len < coord_size) ? (coord_size - s_len) : 0;

	MemoryCopy(raw + r_offset, r_bytes, Min(r_len, coord_size));
	MemoryCopy(raw + coord_size + s_offset, s_bytes, Min(s_len, coord_size));

	return str8(raw, coord_size * 2);
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

	if(EVP_DigestSignInit(md_ctx, 0, EVP_sha256(), 0, pkey) <= 0)
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

	String8 sig_to_encode = str8(signature, sig_len);
	int key_type = EVP_PKEY_id(pkey);
	if(key_type == EVP_PKEY_EC)
	{
		sig_to_encode = ecdsa_raw_from_der(scratch.arena, str8(signature, sig_len), 32);
		if(sig_to_encode.size == 0)
		{
			log_error(str8_lit("acme: failed to convert ECDSA signature from DER to raw format\n"));
			scratch_end(scratch);
			return str8_zero();
		}
	}

	String8 signature_b64 = base64url_encode(scratch.arena, sig_to_encode);

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

	EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_EC, 0);
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

	if(EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx, NID_X9_62_prime256v1) <= 0)
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
