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
