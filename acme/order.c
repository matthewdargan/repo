////////////////////////////////
//~ ACME Order Functions

internal String8
acme_create_order(ACME_Client *client, String8 domain)
{
	Temp scratch = scratch_begin(&client->arena, 1);
	client->nonce = acme_get_nonce(client);

	JSON_Value *protected = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, protected, str8_lit("alg"), json_value_from_string(scratch.arena, str8_lit("ES256")));
	json_object_add(scratch.arena, protected, str8_lit("kid"),
	                json_value_from_string(scratch.arena, client->account_url));
	json_object_add(scratch.arena, protected, str8_lit("nonce"), json_value_from_string(scratch.arena, client->nonce));
	json_object_add(scratch.arena, protected, str8_lit("url"),
	                json_value_from_string(scratch.arena, client->directory.new_order_url));

	JSON_Value *identifiers = json_array_alloc(scratch.arena, 1);
	JSON_Value *identifier = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, identifier, str8_lit("type"), json_value_from_string(scratch.arena, str8_lit("dns")));
	json_object_add(scratch.arena, identifier, str8_lit("value"), json_value_from_string(scratch.arena, domain));
	json_array_add(identifiers, identifier);

	JSON_Value *payload_obj = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, payload_obj, str8_lit("identifiers"), identifiers);

	String8 payload_json = json_serialize(scratch.arena, payload_obj);
	String8 jws = acme_jws_sign(scratch.arena, client->account_key, protected, payload_json);

	URL_Parts url = url_parse_https(client->directory.new_order_url);
	String8 location = str8_zero();
	String8 response =
	    acme_https_request(client, client->arena, url.host, url.port, str8_lit("POST"), url.path, jws, &location, 0);

	if(location.size > 0)
	{
		log_infof("acme: created order at %S\n", location);
		scratch_end(scratch);
		return str8_copy(client->arena, location);
	}

	log_errorf("acme: failed to create order: %S\n", response);
	scratch_end(scratch);
	return str8_zero();
}

internal String8
acme_get_authorization_url(ACME_Client *client, String8 order_url)
{
	Temp scratch = scratch_begin(&client->arena, 1);
	client->nonce = acme_get_nonce(client);

	JSON_Value *protected = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, protected, str8_lit("alg"), json_value_from_string(scratch.arena, str8_lit("ES256")));
	json_object_add(scratch.arena, protected, str8_lit("kid"),
	                json_value_from_string(scratch.arena, client->account_url));
	json_object_add(scratch.arena, protected, str8_lit("nonce"), json_value_from_string(scratch.arena, client->nonce));
	json_object_add(scratch.arena, protected, str8_lit("url"), json_value_from_string(scratch.arena, order_url));

	String8 jws = acme_jws_sign(scratch.arena, client->account_key, protected, str8_zero());

	URL_Parts url = url_parse_https(order_url);
	String8 response =
	    acme_https_request(client, scratch.arena, url.host, url.port, str8_lit("POST"), url.path, jws, 0, 0);

	JSON_Value *order = json_parse(scratch.arena, response);
	if(order == 0 || order->kind != JSON_ValueKind_Object)
	{
		log_error(str8_lit("acme: failed to parse order response\n"));
		scratch_end(scratch);
		return str8_zero();
	}

	JSON_Value *authorizations = json_object_get(order, str8_lit("authorizations"));
	if(authorizations == 0 || authorizations->kind != JSON_ValueKind_Array || authorizations->count == 0)
	{
		log_error(str8_lit("acme: order has no authorizations\n"));
		scratch_end(scratch);
		return str8_zero();
	}

	JSON_Value *authz_url_value = authorizations->values[0];
	if(authz_url_value == 0 || authz_url_value->kind != JSON_ValueKind_String)
	{
		log_error(str8_lit("acme: invalid authorization URL\n"));
		scratch_end(scratch);
		return str8_zero();
	}

	String8 authz_url = str8_copy(client->arena, authz_url_value->string);
	scratch_end(scratch);
	return authz_url;
}

internal String8
acme_finalize_order(ACME_Client *client, String8 order_url, EVP_PKEY *cert_key, String8 domain)
{
	Temp scratch = scratch_begin(&client->arena, 1);

	String8 csr_b64 = acme_generate_csr(scratch.arena, cert_key, domain);

	client->nonce = acme_get_nonce(client);

	JSON_Value *protected = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, protected, str8_lit("alg"), json_value_from_string(scratch.arena, str8_lit("ES256")));
	json_object_add(scratch.arena, protected, str8_lit("kid"),
	                json_value_from_string(scratch.arena, client->account_url));
	json_object_add(scratch.arena, protected, str8_lit("nonce"), json_value_from_string(scratch.arena, client->nonce));

	JSON_Value *order = json_parse(scratch.arena, order_url);
	JSON_Value *finalize_val = json_object_get(order, str8_lit("finalize"));
	if(finalize_val == 0 || finalize_val->kind != JSON_ValueKind_String)
	{
		client->nonce = acme_get_nonce(client);

		JSON_Value *protected_get = json_object_alloc(scratch.arena);
		json_object_add(scratch.arena, protected_get, str8_lit("alg"),
		                json_value_from_string(scratch.arena, str8_lit("ES256")));
		json_object_add(scratch.arena, protected_get, str8_lit("kid"),
		                json_value_from_string(scratch.arena, client->account_url));
		json_object_add(scratch.arena, protected_get, str8_lit("nonce"),
		                json_value_from_string(scratch.arena, client->nonce));
		json_object_add(scratch.arena, protected_get, str8_lit("url"), json_value_from_string(scratch.arena, order_url));

		String8 jws = acme_jws_sign(scratch.arena, client->account_key, protected_get, str8_zero());

		URL_Parts url = url_parse_https(order_url);
		String8 order_response =
		    acme_https_request(client, scratch.arena, url.host, url.port, str8_lit("POST"), url.path, jws, 0, 0);

		order = json_parse(scratch.arena, order_response);
		finalize_val = json_object_get(order, str8_lit("finalize"));
	}

	if(finalize_val == 0 || finalize_val->kind != JSON_ValueKind_String)
	{
		log_error(str8_lit("acme: failed to get finalize URL from order\n"));
		scratch_end(scratch);
		return str8_zero();
	}

	String8 finalize_url = finalize_val->string;
	json_object_add(scratch.arena, protected, str8_lit("url"), json_value_from_string(scratch.arena, finalize_url));

	JSON_Value *payload_obj = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, payload_obj, str8_lit("csr"), json_value_from_string(scratch.arena, csr_b64));

	String8 payload_json = json_serialize(scratch.arena, payload_obj);
	String8 jws = acme_jws_sign(scratch.arena, client->account_key, protected, payload_json);

	URL_Parts url = url_parse_https(finalize_url);
	String8 response =
	    acme_https_request(client, scratch.arena, url.host, url.port, str8_lit("POST"), url.path, jws, 0, 0);

	JSON_Value *result = json_parse(scratch.arena, response);
	if(result == 0 || result->kind != JSON_ValueKind_Object)
	{
		log_errorf("acme: failed to parse finalize response: %S\n", response);
		scratch_end(scratch);
		return str8_zero();
	}

	JSON_Value *error = json_object_get(result, str8_lit("type"));
	if(error != 0 && error->kind == JSON_ValueKind_String)
	{
		JSON_Value *detail = json_object_get(result, str8_lit("detail"));
		if(detail != 0 && detail->kind == JSON_ValueKind_String)
		{
			log_errorf("acme: finalize error: %S - %S\n", error->string, detail->string);
		}
		else
		{
			log_errorf("acme: finalize error: %S\n", error->string);
		}
		scratch_end(scratch);
		return str8_zero();
	}

	log_info(str8_lit("acme: order finalized\n"));
	scratch_end(scratch);
	return str8_copy(client->arena, order_url);
}

internal b32
acme_poll_order_status(ACME_Client *client, String8 order_url, u64 max_attempts, u64 delay_ms)
{
	for(u64 attempt = 0; attempt < max_attempts; attempt += 1)
	{
		Temp scratch = scratch_begin(&client->arena, 1);
		client->nonce = acme_get_nonce(client);

		JSON_Value *protected = json_object_alloc(scratch.arena);
		json_object_add(scratch.arena, protected, str8_lit("alg"),
		                json_value_from_string(scratch.arena, str8_lit("ES256")));
		json_object_add(scratch.arena, protected, str8_lit("kid"),
		                json_value_from_string(scratch.arena, client->account_url));
		json_object_add(scratch.arena, protected, str8_lit("nonce"), json_value_from_string(scratch.arena, client->nonce));
		json_object_add(scratch.arena, protected, str8_lit("url"), json_value_from_string(scratch.arena, order_url));

		String8 jws = acme_jws_sign(scratch.arena, client->account_key, protected, str8_zero());

		URL_Parts url = url_parse_https(order_url);
		String8 response =
		    acme_https_request(client, scratch.arena, url.host, url.port, str8_lit("POST"), url.path, jws, 0, 0);

		JSON_Value *result = json_parse(scratch.arena, response);
		if(result != 0 && result->kind == JSON_ValueKind_Object)
		{
			JSON_Value *status = json_object_get(result, str8_lit("status"));
			if(status != 0 && status->kind == JSON_ValueKind_String)
			{
				log_infof("acme: order status: %S (attempt %u/%u)\n", status->string, attempt + 1, max_attempts);

				if(str8_match(status->string, str8_lit("valid"), 0))
				{
					scratch_end(scratch);
					return 1;
				}

				if(str8_match(status->string, str8_lit("invalid"), 0))
				{
					log_error(str8_lit("acme: order failed\n"));
					scratch_end(scratch);
					return 0;
				}
			}
		}

		scratch_end(scratch);
		os_sleep_milliseconds(delay_ms);
	}

	log_error(str8_lit("acme: order processing timed out\n"));
	return 0;
}

internal String8
acme_download_certificate(ACME_Client *client, String8 order_url)
{
	Temp scratch = scratch_begin(&client->arena, 1);
	client->nonce = acme_get_nonce(client);

	JSON_Value *protected = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, protected, str8_lit("alg"), json_value_from_string(scratch.arena, str8_lit("ES256")));
	json_object_add(scratch.arena, protected, str8_lit("kid"),
	                json_value_from_string(scratch.arena, client->account_url));
	json_object_add(scratch.arena, protected, str8_lit("nonce"), json_value_from_string(scratch.arena, client->nonce));
	json_object_add(scratch.arena, protected, str8_lit("url"), json_value_from_string(scratch.arena, order_url));

	String8 jws = acme_jws_sign(scratch.arena, client->account_key, protected, str8_zero());

	URL_Parts url = url_parse_https(order_url);
	String8 response =
	    acme_https_request(client, scratch.arena, url.host, url.port, str8_lit("POST"), url.path, jws, 0, 0);

	JSON_Value *order = json_parse(scratch.arena, response);
	if(order == 0 || order->kind != JSON_ValueKind_Object)
	{
		log_error(str8_lit("acme: failed to parse order response\n"));
		scratch_end(scratch);
		return str8_zero();
	}

	JSON_Value *cert_url_val = json_object_get(order, str8_lit("certificate"));
	if(cert_url_val == 0 || cert_url_val->kind != JSON_ValueKind_String)
	{
		log_error(str8_lit("acme: order has no certificate URL\n"));
		scratch_end(scratch);
		return str8_zero();
	}

	String8 cert_url = cert_url_val->string;

	client->nonce = acme_get_nonce(client);

	JSON_Value *protected_cert = json_object_alloc(scratch.arena);
	json_object_add(scratch.arena, protected_cert, str8_lit("alg"),
	                json_value_from_string(scratch.arena, str8_lit("ES256")));
	json_object_add(scratch.arena, protected_cert, str8_lit("kid"),
	                json_value_from_string(scratch.arena, client->account_url));
	json_object_add(scratch.arena, protected_cert, str8_lit("nonce"),
	                json_value_from_string(scratch.arena, client->nonce));
	json_object_add(scratch.arena, protected_cert, str8_lit("url"), json_value_from_string(scratch.arena, cert_url));

	String8 jws_cert = acme_jws_sign(scratch.arena, client->account_key, protected_cert, str8_zero());

	URL_Parts cert_url_parts = url_parse_https(cert_url);
	String8 cert_pem = acme_https_request(client, client->arena, cert_url_parts.host, cert_url_parts.port,
	                                      str8_lit("POST"), cert_url_parts.path, jws_cert, 0, 0);

	log_info(str8_lit("acme: certificate downloaded\n"));
	scratch_end(scratch);
	return cert_pem;
}
