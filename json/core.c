////////////////////////////////
//~ JSON Value Constructors

internal JSON_Value *
json_value_null(Arena *arena)
{
	JSON_Value *val = push_array(arena, JSON_Value, 1);
	val->kind = JSON_ValueKind_Null;
	return val;
}

internal JSON_Value *
json_value_from_bool(Arena *arena, b32 value)
{
	JSON_Value *val = push_array(arena, JSON_Value, 1);
	val->kind = JSON_ValueKind_Bool;
	val->boolean = value;
	return val;
}

internal JSON_Value *
json_value_from_number(Arena *arena, f64 number)
{
	JSON_Value *val = push_array(arena, JSON_Value, 1);
	val->kind = JSON_ValueKind_Number;
	val->number = number;
	return val;
}

internal JSON_Value *
json_value_from_string(Arena *arena, String8 string)
{
	JSON_Value *val = push_array(arena, JSON_Value, 1);
	val->kind = JSON_ValueKind_String;
	val->string = str8_copy(arena, string);
	return val;
}

////////////////////////////////
//~ JSON Object Operations

internal JSON_Value *
json_object_alloc(Arena *arena)
{
	JSON_Value *val = push_array(arena, JSON_Value, 1);
	val->kind = JSON_ValueKind_Object;
	return val;
}

internal void
json_object_add(Arena *arena, JSON_Value *obj, String8 name, JSON_Value *value)
{
	Assert(obj->kind == JSON_ValueKind_Object);
	JSON_Member *member = push_array(arena, JSON_Member, 1);
	member->name = str8_copy(arena, name);
	member->value = value;
	if(obj->last != 0)
	{
		obj->last->next = member;
	}
	else
	{
		obj->first = member;
	}
	obj->last = member;
	obj->count += 1;
}

internal JSON_Value *
json_object_get(JSON_Value *obj, String8 name)
{
	if(obj->kind != JSON_ValueKind_Object)
	{
		return 0;
	}
	for(JSON_Member *member = obj->first; member != 0; member = member->next)
	{
		if(str8_match(member->name, name, 0))
		{
			return member->value;
		}
	}
	return 0;
}

////////////////////////////////
//~ JSON Array Operations

internal JSON_Value *
json_array_alloc(Arena *arena, u64 capacity)
{
	JSON_Value *val = push_array(arena, JSON_Value, 1);
	val->kind = JSON_ValueKind_Array;
	val->values = push_array(arena, JSON_Value *, capacity);
	return val;
}

internal void
json_array_add(JSON_Value *arr, JSON_Value *value)
{
	Assert(arr->kind == JSON_ValueKind_Array);
	arr->values[arr->count] = value;
	arr->count += 1;
}

////////////////////////////////
//~ JSON Serialization

internal void
json_serialize_string(Arena *arena, String8List *list, String8 string)
{
	u64 escaped_size = 2;
	for(u64 i = 0; i < string.size; i += 1)
	{
		u8 c = string.str[i];
		if(c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t')
		{
			escaped_size += 2;
		}
		else
		{
			escaped_size += 1;
		}
	}
	u8 *buf = push_array(arena, u8, escaped_size);
	u64 pos = 0;
	buf[pos] = '"';
	pos += 1;
	for(u64 i = 0; i < string.size; i += 1)
	{
		u8 c = string.str[i];
		if(c == '"')
		{
			buf[pos + 0] = '\\';
			buf[pos + 1] = '"';
			pos += 2;
		}
		else if(c == '\\')
		{
			buf[pos + 0] = '\\';
			buf[pos + 1] = '\\';
			pos += 2;
		}
		else if(c == '\n')
		{
			buf[pos + 0] = '\\';
			buf[pos + 1] = 'n';
			pos += 2;
		}
		else if(c == '\r')
		{
			buf[pos + 0] = '\\';
			buf[pos + 1] = 'r';
			pos += 2;
		}
		else if(c == '\t')
		{
			buf[pos + 0] = '\\';
			buf[pos + 1] = 't';
			pos += 2;
		}
		else
		{
			buf[pos] = c;
			pos += 1;
		}
	}
	buf[pos] = '"';
	pos += 1;
	str8_list_push(arena, list, str8(buf, pos));
}

internal void
json_serialize_value(Arena *arena, String8List *list, JSON_Value *value)
{
	switch(value->kind)
	{
		case JSON_ValueKind_Null:
		{
			str8_list_push(arena, list, str8_lit("null"));
		}
		break;
		case JSON_ValueKind_Bool:
		{
			str8_list_push(arena, list, value->boolean ? str8_lit("true") : str8_lit("false"));
		}
		break;
		case JSON_ValueKind_Number:
		{
			String8 num_str = str8f(arena, "%g", value->number);
			str8_list_push(arena, list, num_str);
		}
		break;
		case JSON_ValueKind_String:
		{
			json_serialize_string(arena, list, value->string);
		}
		break;
		case JSON_ValueKind_Object:
		{
			str8_list_push(arena, list, str8_lit("{"));
			b32 first = 1;
			for(JSON_Member *member = value->first; member != 0; member = member->next)
			{
				if(!first)
				{
					str8_list_push(arena, list, str8_lit(","));
				}
				first = 0;
				json_serialize_string(arena, list, member->name);
				str8_list_push(arena, list, str8_lit(":"));
				json_serialize_value(arena, list, member->value);
			}
			str8_list_push(arena, list, str8_lit("}"));
		}
		break;
		case JSON_ValueKind_Array:
		{
			str8_list_push(arena, list, str8_lit("["));
			for(u64 i = 0; i < value->count; i += 1)
			{
				if(i > 0)
				{
					str8_list_push(arena, list, str8_lit(","));
				}
				json_serialize_value(arena, list, value->values[i]);
			}
			str8_list_push(arena, list, str8_lit("]"));
		}
		break;
	}
}

internal String8
json_serialize(Arena *arena, JSON_Value *value)
{
	String8List list = {0};
	json_serialize_value(arena, &list, value);
	return str8_list_join(arena, &list, 0);
}

////////////////////////////////
//~ JSON Parsing

internal void
json_skip_whitespace(JSON_Parser *parser)
{
	for(; parser->pos < parser->text.size;)
	{
		u8 c = parser->text.str[parser->pos];
		if(c == ' ' || c == '\t' || c == '\n' || c == '\r')
		{
			parser->pos += 1;
		}
		else
		{
			break;
		}
	}
}

internal JSON_Value *json_parse_value(JSON_Parser *parser);

internal JSON_Value *
json_parse_string(JSON_Parser *parser)
{
	Assert(parser->text.str[parser->pos] == '"');
	parser->pos += 1;
	u64 start = parser->pos;
	for(; parser->pos < parser->text.size && parser->text.str[parser->pos] != '"';)
	{
		if(parser->text.str[parser->pos] == '\\')
		{
			parser->pos += 2;
		}
		else
		{
			parser->pos += 1;
		}
	}
	String8 string = str8(parser->text.str + start, parser->pos - start);
	parser->pos += 1;
	return json_value_from_string(parser->arena, string);
}

internal JSON_Value *
json_parse_number(JSON_Parser *parser)
{
	u64 start = parser->pos;
	for(; parser->pos < parser->text.size;)
	{
		u8 c = parser->text.str[parser->pos];
		if((c >= '0' && c <= '9') || c == '-' || c == '.' || c == 'e' || c == 'E' || c == '+')
		{
			parser->pos += 1;
		}
		else
		{
			break;
		}
	}
	String8 num_str = str8(parser->text.str + start, parser->pos - start);
	char buf[256];
	u64 len = Min(num_str.size, sizeof(buf) - 1);
	MemoryCopy(buf, num_str.str, len);
	buf[len] = 0;
	f64 number = strtod(buf, 0);
	return json_value_from_number(parser->arena, number);
}

internal JSON_Value *
json_parse_object(JSON_Parser *parser)
{
	Assert(parser->text.str[parser->pos] == '{');
	parser->pos += 1;

	JSON_Value *obj = json_object_alloc(parser->arena);

	json_skip_whitespace(parser);
	if(parser->pos < parser->text.size && parser->text.str[parser->pos] == '}')
	{
		parser->pos += 1;
		return obj;
	}

	for(;;)
	{
		json_skip_whitespace(parser);
		if(parser->pos >= parser->text.size || parser->text.str[parser->pos] != '"')
		{
			break;
		}

		JSON_Value *key = json_parse_string(parser);
		json_skip_whitespace(parser);

		if(parser->pos >= parser->text.size || parser->text.str[parser->pos] != ':')
		{
			break;
		}
		parser->pos += 1;

		json_skip_whitespace(parser);
		JSON_Value *value = json_parse_value(parser);

		json_object_add(parser->arena, obj, key->string, value);

		json_skip_whitespace(parser);
		if(parser->pos >= parser->text.size)
		{
			break;
		}

		if(parser->text.str[parser->pos] == ',')
		{
			parser->pos += 1;
		}
		else if(parser->text.str[parser->pos] == '}')
		{
			parser->pos += 1;
			break;
		}
		else
		{
			break;
		}
	}

	return obj;
}

internal JSON_Value *
json_parse_array(JSON_Parser *parser)
{
	Assert(parser->text.str[parser->pos] == '[');
	parser->pos += 1;

	JSON_Value *arr = json_array_alloc(parser->arena, 16);

	json_skip_whitespace(parser);
	if(parser->pos < parser->text.size && parser->text.str[parser->pos] == ']')
	{
		parser->pos += 1;
		return arr;
	}

	for(;;)
	{
		json_skip_whitespace(parser);
		JSON_Value *value = json_parse_value(parser);
		json_array_add(arr, value);

		json_skip_whitespace(parser);
		if(parser->pos >= parser->text.size)
		{
			break;
		}

		if(parser->text.str[parser->pos] == ',')
		{
			parser->pos += 1;
		}
		else if(parser->text.str[parser->pos] == ']')
		{
			parser->pos += 1;
			break;
		}
		else
		{
			break;
		}
	}

	return arr;
}

internal JSON_Value *
json_parse_value(JSON_Parser *parser)
{
	json_skip_whitespace(parser);

	if(parser->pos >= parser->text.size)
	{
		return json_value_null(parser->arena);
	}

	u8 c = parser->text.str[parser->pos];

	if(c == '"')
	{
		return json_parse_string(parser);
	}
	else if(c == '{')
	{
		return json_parse_object(parser);
	}
	else if(c == '[')
	{
		return json_parse_array(parser);
	}
	else if(c == 't' && parser->pos + 4 <= parser->text.size && MemoryMatch(parser->text.str + parser->pos, "true", 4))
	{
		parser->pos += 4;
		return json_value_from_bool(parser->arena, 1);
	}
	else if(c == 'f' && parser->pos + 5 <= parser->text.size && MemoryMatch(parser->text.str + parser->pos, "false", 5))
	{
		parser->pos += 5;
		return json_value_from_bool(parser->arena, 0);
	}
	else if(c == 'n' && parser->pos + 4 <= parser->text.size && MemoryMatch(parser->text.str + parser->pos, "null", 4))
	{
		parser->pos += 4;
		return json_value_null(parser->arena);
	}
	else if((c >= '0' && c <= '9') || c == '-')
	{
		return json_parse_number(parser);
	}

	return json_value_null(parser->arena);
}

internal JSON_Value *
json_parse(Arena *arena, String8 text)
{
	JSON_Parser parser = {0};
	parser.arena = arena;
	parser.text = text;
	parser.pos = 0;

	return json_parse_value(&parser);
}
