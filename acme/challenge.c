////////////////////////////////
//~ ACME Challenge Functions

internal ACME_ChallengeInfo
acme_get_http01_challenge(ACME_Client *client, String8 authz_url)
{
	ACME_ChallengeInfo info = {0};
	Temp scratch = scratch_begin(&client->arena, 1);

	String8 response = acme_post_as_json(client, scratch.arena, authz_url, str8_zero());

	JSON_Value *authz = json_parse(scratch.arena, response);
	if(authz == 0 || authz->kind != JSON_ValueKind_Object)
	{
		log_error(str8_lit("acme: failed to parse authorization response\n"));
		scratch_end(scratch);
		return info;
	}

	JSON_Value *challenges = json_object_get(authz, str8_lit("challenges"));
	if(challenges == 0 || challenges->kind != JSON_ValueKind_Array)
	{
		log_error(str8_lit("acme: authorization has no challenges\n"));
		scratch_end(scratch);
		return info;
	}

	for(u64 i = 0; i < challenges->count; i += 1)
	{
		JSON_Value *ch = challenges->values[i];
		if(ch == 0 || ch->kind != JSON_ValueKind_Object)
		{
			continue;
		}

		JSON_Value *type = json_object_get(ch, str8_lit("type"));
		if(type != 0 && type->kind == JSON_ValueKind_String && str8_match(type->string, str8_lit("http-01"), 0))
		{
			JSON_Value *ch_url = json_object_get(ch, str8_lit("url"));
			JSON_Value *token = json_object_get(ch, str8_lit("token"));
			if(ch_url != 0 && ch_url->kind == JSON_ValueKind_String && token != 0 && token->kind == JSON_ValueKind_String)
			{
				info.type = str8_copy(client->arena, type->string);
				info.url = str8_copy(client->arena, ch_url->string);
				info.token = str8_copy(client->arena, token->string);
				break;
			}
		}
	}

	scratch_end(scratch);
	return info;
}

internal b32
acme_notify_challenge_ready(ACME_Client *client, String8 challenge_url)
{
	Temp scratch = scratch_begin(&client->arena, 1);

	String8 response = acme_post_as_json(client, scratch.arena, challenge_url, str8_lit("{}"));

	JSON_Value *result = json_parse(scratch.arena, response);
	if(result == 0 || result->kind != JSON_ValueKind_Object)
	{
		log_error(str8_lit("acme: failed to parse challenge notify response\n"));
		scratch_end(scratch);
		return 0;
	}

	JSON_Value *status = json_object_get(result, str8_lit("status"));
	if(status != 0 && status->kind == JSON_ValueKind_String)
	{
		log_infof("acme: challenge status: %S\n", status->string);
		scratch_end(scratch);
		return 1;
	}

	scratch_end(scratch);
	return 0;
}

internal b32
acme_poll_challenge_status(ACME_Client *client, String8 challenge_url, u64 max_attempts, u64 delay_ms)
{
	for(u64 attempt = 0; attempt < max_attempts; attempt += 1)
	{
		Temp scratch = scratch_begin(&client->arena, 1);

		String8 response = acme_post_as_json(client, scratch.arena, challenge_url, str8_zero());

		JSON_Value *result = json_parse(scratch.arena, response);
		if(result != 0 && result->kind == JSON_ValueKind_Object)
		{
			JSON_Value *status = json_object_get(result, str8_lit("status"));
			if(status != 0 && status->kind == JSON_ValueKind_String)
			{
				log_infof("acme: challenge status: %S (attempt %u/%u)\n", status->string, attempt + 1, max_attempts);

				if(str8_match(status->string, str8_lit("valid"), 0))
				{
					log_info(str8_lit("acme: challenge validation succeeded\n"));
					scratch_end(scratch);
					return 1;
				}

				if(str8_match(status->string, str8_lit("invalid"), 0))
				{
					JSON_Value *error_val = json_object_get(result, str8_lit("error"));
					if(error_val != 0 && error_val->kind == JSON_ValueKind_Object)
					{
						JSON_Value *detail = json_object_get(error_val, str8_lit("detail"));
						if(detail != 0 && detail->kind == JSON_ValueKind_String)
						{
							log_errorf("acme: challenge failed: %S\n", detail->string);
						}
						else
						{
							log_error(str8_lit("acme: challenge validation failed\n"));
						}
					}
					else
					{
						log_error(str8_lit("acme: challenge validation failed\n"));
					}
					scratch_end(scratch);
					return 0;
				}
			}
		}

		scratch_end(scratch);
		os_sleep_milliseconds(delay_ms);
	}

	log_error(str8_lit("acme: challenge validation timed out\n"));
	return 0;
}
