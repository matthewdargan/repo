////////////////////////////////
//~ URL Parsing

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
//~ HTTPS Requests

internal String8
http_header_find(Arena *arena, String8 headers, String8 header_name)
{
	u64 name_len = header_name.size;
	for(u64 i = 0; i + name_len < headers.size; i += 1)
	{
		if(MemoryMatch(headers.str + i, header_name.str, name_len))
		{
			u64 start = i + name_len;
			u64 end = start;
			for(; end < headers.size && headers.str[end] != '\r' && headers.str[end] != '\n'; end += 1)
				;
			return str8_copy(arena, str8(headers.str + start, end - start));
		}
	}
	return str8_zero();
}

internal String8
acme_https_request(ACME_Client *client, Arena *arena, String8 host, u16 port, String8 method, String8 path,
                   String8 body, String8 *out_location, String8 *out_nonce)
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

	SSL *ssl = SSL_new(client->ssl_ctx);
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
		*out_location = http_header_find(arena, headers, str8_lit("Location: "));
	}

	if(out_nonce != 0)
	{
		*out_nonce = http_header_find(arena, headers, str8_lit("Replay-Nonce: "));
	}

	String8 result = str8_copy(arena, body_response);
	scratch_end(scratch);
	return result;
}

////////////////////////////////
//~ ACME Client Functions

internal String8
acme_get_nonce(ACME_Client *client)
{
	Temp scratch = scratch_begin(&client->arena, 1);
	URL_Parts url = url_parse_https(client->directory.new_nonce_url);
	String8 nonce = str8_zero();
	acme_https_request(client, client->arena, url.host, url.port, str8_lit("HEAD"), url.path, str8_zero(), 0, &nonce);
	scratch_end(scratch);
	return nonce;
}

internal ACME_Client *
acme_client_alloc(Arena *arena, SSL_CTX *ssl_ctx, String8 directory_url, EVP_PKEY *account_key)
{
	Temp scratch = scratch_begin(&arena, 1);

	ACME_Client *client = push_array(arena, ACME_Client, 1);
	client->arena = arena;
	client->ssl_ctx = ssl_ctx;
	client->directory_url = str8_copy(arena, directory_url);
	client->account_key = account_key;

	URL_Parts url = url_parse_https(directory_url);

	String8 directory_json =
	    acme_https_request(client, scratch.arena, url.host, url.port, str8_lit("GET"), url.path, str8_zero(), 0, 0);
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

	JSON_Value *protected_reg = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, protected_reg, str8_lit("alg"),
	                json_value_from_string(scratch.arena, str8_lit("ES256")));
	json_object_add(scratch.arena, protected_reg, str8_lit("nonce"),
	                json_value_from_string(scratch.arena, client->nonce));
	json_object_add(scratch.arena, protected_reg, str8_lit("url"),
	                json_value_from_string(scratch.arena, client->directory.new_account_url));

	JSON_Value *jwk = acme_jwk_from_key(scratch.arena, account_key);
	json_object_add(scratch.arena, protected_reg, str8_lit("jwk"), jwk);

	JSON_Value *payload_obj = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, payload_obj, str8_lit("termsOfServiceAgreed"), json_value_from_bool(scratch.arena, 1));

	String8 payload_json = json_serialize(scratch.arena, payload_obj);
	String8 jws = acme_jws_sign(scratch.arena, account_key, protected_reg, payload_json);

	URL_Parts reg_url = url_parse_https(client->directory.new_account_url);
	String8 account_location = str8_zero();
	String8 reg_response = acme_https_request(client, scratch.arena, reg_url.host, reg_url.port, str8_lit("POST"),
	                                          reg_url.path, jws, &account_location, 0);

	if(account_location.size > 0)
	{
		client->account_url = str8_copy(arena, account_location);
		log_infof("acme: registered account at %S\n", client->account_url);
	}
	else
	{
		log_errorf("acme: failed to register account: %S\n", reg_response);
		scratch_end(scratch);
		return 0;
	}

	scratch_end(scratch);
	return client;
}
