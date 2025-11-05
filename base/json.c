static Jsonvalue
jsonparse(Arena *a, String8 text)
{
	u64 pos = 0;
	return jsonparsevalue(a, text, &pos);
}

static Jsonvalue
jsonparsevalue(Arena *a, String8 text, u64 *pos)
{
	Jsonvalue result = {0};
	jsonskipwhitespace(text, pos);
	if (*pos >= text.size)
	{
		return result;
	}
	u8 c = text.str[*pos];
	switch (c)
	{
		case 'n':
		{
			return jsonparsenull(text, pos);
		}
		case 't':
		case 'f':
		{
			return jsonparsebool(text, pos);
		}
		case '"':
		{
			return jsonparsestring(a, text, pos);
		}
		case '[':
		{
			return jsonparsearray(a, text, pos);
		}
		case '{':
		{
			return jsonparseobject(a, text, pos);
		}
		case '-':
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		{
			return jsonparsenumber(a, text, pos);
		}
		default:
		{
			return result;
		}
	}
}

static void
jsonskipwhitespace(String8 text, u64 *pos)
{
	while (*pos < text.size)
	{
		u8 c = text.str[*pos];
		if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
		{
			(*pos)++;
		}
		else
		{
			break;
		}
	}
}

static Jsonvalue
jsonparsenull(String8 text, u64 *pos)
{
	Jsonvalue result = {0};
	if (*pos + 4 <= text.size && str8_match(str8_substr(text, rng_1u64(*pos, *pos + 4)), str8_lit("null"), 0))
	{
		result.type = JSON_NULL;
		*pos += 4;
	}
	return result;
}

static Jsonvalue
jsonparsebool(String8 text, u64 *pos)
{
	Jsonvalue result = {0};
	if (*pos + 4 <= text.size && str8_match(str8_substr(text, rng_1u64(*pos, *pos + 4)), str8_lit("true"), 0))
	{
		result.type = JSON_BOOL;
		result.bool = 1;
		*pos += 4;
	}
	else if (*pos + 5 <= text.size && str8_match(str8_substr(text, rng_1u64(*pos, *pos + 5)), str8_lit("false"), 0))
	{
		result.type = JSON_BOOL;
		result.bool = 0;
		*pos += 5;
	}
	return result;
}

static Jsonvalue
jsonparsestring(Arena *a, String8 text, u64 *pos)
{
	Jsonvalue result = {0};
	if (*pos >= text.size || text.str[*pos] != '"')
	{
		return result;
	}
	(*pos)++;
	u64 start = *pos;
	while (*pos < text.size && text.str[*pos] != '"')
	{
		if (text.str[*pos] == '\\')
		{
			(*pos)++;
		}
		if (*pos < text.size)
		{
			(*pos)++;
		}
	}
	if (*pos >= text.size)
	{
		return result;
	}
	u64 end = *pos;
	(*pos)++;
	String8 unescaped = str8_substr(text, rng_1u64(start, end));
	u8 *dst           = push_array(a, u8, unescaped.size);
	u64 dstpos        = 0;
	for (u64 i = 0; i < unescaped.size; i++)
	{
		if (unescaped.str[i] == '\\' && i + 1 < unescaped.size)
		{
			i++;
			switch (unescaped.str[i])
			{
				case '"':
				{
					dst[dstpos++] = '"';
				}
				break;
				case '\\':
				{
					dst[dstpos++] = '\\';
				}
				break;
				case '/':
				{
					dst[dstpos++] = '/';
				}
				break;
				case 'b':
				{
					dst[dstpos++] = '\b';
				}
				break;
				case 'f':
				{
					dst[dstpos++] = '\f';
				}
				break;
				case 'n':
				{
					dst[dstpos++] = '\n';
				}
				break;
				case 'r':
				{
					dst[dstpos++] = '\r';
				}
				break;
				case 't':
				{
					dst[dstpos++] = '\t';
				}
				break;
				default:
				{
					dst[dstpos++] = unescaped.str[i];
				}
				break;
			}
		}
		else
		{
			dst[dstpos++] = unescaped.str[i];
		}
	}
	result.type   = JSON_STRING;
	result.string = str8(dst, dstpos);
	return result;
}

static Jsonvalue
jsonparsenumber(Arena *a, String8 text, u64 *pos)
{
	Jsonvalue result = {0};
	u64 start        = *pos;
	if (*pos < text.size && text.str[*pos] == '-')
	{
		(*pos)++;
	}
	while (*pos < text.size && text.str[*pos] >= '0' && text.str[*pos] <= '9')
	{
		(*pos)++;
	}
	if (*pos < text.size && text.str[*pos] == '.')
	{
		(*pos)++;
		while (*pos < text.size && text.str[*pos] >= '0' && text.str[*pos] <= '9')
		{
			(*pos)++;
		}
	}
	if (*pos < text.size && (text.str[*pos] == 'e' || text.str[*pos] == 'E'))
	{
		(*pos)++;
		if (*pos < text.size && (text.str[*pos] == '+' || text.str[*pos] == '-'))
		{
			(*pos)++;
		}
		while (*pos < text.size && text.str[*pos] >= '0' && text.str[*pos] <= '9')
		{
			(*pos)++;
		}
	}
	u64 end        = *pos;
	String8 numstr = str8_substr(text, rng_1u64(start, end));
	char *numcstr  = (char *)push_array(a, u8, numstr.size + 1);
	memcpy(numcstr, numstr.str, numstr.size);
	numcstr[numstr.size] = '\0';
	char *endptr         = NULL;
	f64 value            = strtod(numcstr, &endptr);
	if (endptr != numcstr)
	{
		result.type   = JSON_NUMBER;
		result.number = value;
	}
	return result;
}

static Jsonvalue
jsonparsearray(Arena *a, String8 text, u64 *pos)
{
	Jsonvalue result = {0};
	if (*pos >= text.size || text.str[*pos] != '[')
	{
		return result;
	}
	(*pos)++;
	jsonskipwhitespace(text, pos);
	if (*pos < text.size && text.str[*pos] == ']')
	{
		(*pos)++;
		result.type = JSON_ARRAY;
		return result;
	}
	u64 capacity    = 16;
	Jsonvalue *vals = push_array(a, Jsonvalue, capacity);
	u64 count       = 0;
	while (*pos < text.size)
	{
		Jsonvalue elem = jsonparsevalue(a, text, pos);
		if (elem.type == JSON_NULL && text.str[*pos - 1] != 'l')
		{
			break;
		}
		if (count >= capacity)
		{
			capacity *= 2;
			vals = push_array(a, Jsonvalue, capacity);
			memcpy(vals, result.arrvals, count * sizeof(Jsonvalue));
		}
		vals[count++] = elem;
		jsonskipwhitespace(text, pos);
		if (*pos >= text.size)
		{
			break;
		}
		if (text.str[*pos] == ']')
		{
			(*pos)++;
			break;
		}
		if (text.str[*pos] == ',')
		{
			(*pos)++;
			jsonskipwhitespace(text, pos);
		}
		else
		{
			break;
		}
	}
	result.type    = JSON_ARRAY;
	result.arrvals = vals;
	result.arrcnt  = count;
	return result;
}

static Jsonvalue
jsonparseobject(Arena *a, String8 text, u64 *pos)
{
	Jsonvalue result = {0};
	if (*pos >= text.size || text.str[*pos] != '{')
	{
		return result;
	}
	(*pos)++;
	jsonskipwhitespace(text, pos);
	if (*pos < text.size && text.str[*pos] == '}')
	{
		(*pos)++;
		result.type = JSON_OBJECT;
		return result;
	}
	u64 capacity    = 16;
	String8 *keys   = push_array(a, String8, capacity);
	Jsonvalue *vals = push_array(a, Jsonvalue, capacity);
	u64 count       = 0;
	while (*pos < text.size)
	{
		Jsonvalue key = jsonparsevalue(a, text, pos);
		if (key.type != JSON_STRING)
		{
			break;
		}
		jsonskipwhitespace(text, pos);
		if (*pos >= text.size || text.str[*pos] != ':')
		{
			break;
		}
		(*pos)++;
		jsonskipwhitespace(text, pos);
		Jsonvalue val = jsonparsevalue(a, text, pos);
		if (count >= capacity)
		{
			capacity *= 2;
			keys = push_array(a, String8, capacity);
			vals = push_array(a, Jsonvalue, capacity);
			memcpy(keys, result.objkeys, count * sizeof(String8));
			memcpy(vals, result.objvals, count * sizeof(Jsonvalue));
		}
		keys[count] = key.string;
		vals[count] = val;
		count++;
		jsonskipwhitespace(text, pos);
		if (*pos >= text.size)
		{
			break;
		}
		if (text.str[*pos] == '}')
		{
			(*pos)++;
			break;
		}
		if (text.str[*pos] == ',')
		{
			(*pos)++;
			jsonskipwhitespace(text, pos);
		}
		else
		{
			break;
		}
	}
	result.type    = JSON_OBJECT;
	result.objkeys = keys;
	result.objvals = vals;
	result.objcnt  = count;
	return result;
}

static Jsonvalue
jsonget(Jsonvalue obj, String8 key)
{
	Jsonvalue result = {0};
	if (obj.type != JSON_OBJECT)
	{
		return result;
	}
	for (u64 i = 0; i < obj.objcnt; i++)
	{
		if (str8_match(obj.objkeys[i], key, 0))
		{
			return obj.objvals[i];
		}
	}
	return result;
}

static Jsonvalue
jsonindex(Jsonvalue arr, u64 idx)
{
	Jsonvalue result = {0};
	if (arr.type != JSON_ARRAY || idx >= arr.arrcnt)
	{
		return result;
	}
	return arr.arrvals[idx];
}

static Jsonbuilder
jsonbuilder(Arena *a, u64 estsize)
{
	if (estsize == 0)
	{
		estsize = 1024;
	}
	Jsonbuilder b = {
	    .data = push_array(a, u8, estsize),
	    .pos  = 0,
	    .cap  = estsize,
	};
	return b;
}

static void
jsonbwrite(Jsonbuilder *b, String8 s)
{
	for (u64 i = 0; i < s.size && b->pos < b->cap; i++)
	{
		b->data[b->pos++] = s.str[i];
	}
}

static void
jsonbwritec(Jsonbuilder *b, u8 c)
{
	if (b->pos < b->cap)
	{
		b->data[b->pos++] = c;
	}
}

static void
jsonbwritestr(Jsonbuilder *b, String8 s)
{
	jsonbwritec(b, '"');
	for (u64 i = 0; i < s.size; i++)
	{
		u8 c = s.str[i];
		switch (c)
		{
			case '"':
			{
				jsonbwrite(b, str8_lit("\\\""));
			}
			break;
			case '\\':
			{
				jsonbwrite(b, str8_lit("\\\\"));
			}
			break;
			case '\b':
			{
				jsonbwrite(b, str8_lit("\\b"));
			}
			break;
			case '\f':
			{
				jsonbwrite(b, str8_lit("\\f"));
			}
			break;
			case '\n':
			{
				jsonbwrite(b, str8_lit("\\n"));
			}
			break;
			case '\r':
			{
				jsonbwrite(b, str8_lit("\\r"));
			}
			break;
			case '\t':
			{
				jsonbwrite(b, str8_lit("\\t"));
			}
			break;
			default:
			{
				if (c >= 32 && c <= 126)
				{
					jsonbwritec(b, c);
				}
				else
				{
					u8 hexbuf[6];
					u8 hexlen        = 0;
					hexbuf[hexlen++] = '\\';
					hexbuf[hexlen++] = 'u';
					hexbuf[hexlen++] = "0123456789abcdef"[(c >> 12) & 0xf];
					hexbuf[hexlen++] = "0123456789abcdef"[(c >> 8) & 0xf];
					hexbuf[hexlen++] = "0123456789abcdef"[(c >> 4) & 0xf];
					hexbuf[hexlen++] = "0123456789abcdef"[c & 0xf];
					jsonbwrite(b, str8(hexbuf, hexlen));
				}
			}
			break;
		}
	}
	jsonbwritec(b, '"');
}

static void
jsonbwritenum(Jsonbuilder *b, f64 n)
{
	u8 numbuf[32];
	int written = snprintf((char *)numbuf, sizeof(numbuf), "%.17g", n);
	if (written > 0 && written < (int)sizeof(numbuf))
	{
		jsonbwrite(b, str8(numbuf, written));
	}
}

static void
jsonbwritebool(Jsonbuilder *b, b32 val)
{
	if (val)
	{
		jsonbwrite(b, str8_lit("true"));
	}
	else
	{
		jsonbwrite(b, str8_lit("false"));
	}
}

static void
jsonbwritenull(Jsonbuilder *b)
{
	jsonbwrite(b, str8_lit("null"));
}

static void
jsonbobjstart(Jsonbuilder *b)
{
	jsonbwritec(b, '{');
}

static void
jsonbobjend(Jsonbuilder *b)
{
	jsonbwritec(b, '}');
}

static void
jsonbarrstart(Jsonbuilder *b)
{
	jsonbwritec(b, '[');
}

static void
jsonbarrend(Jsonbuilder *b)
{
	jsonbwritec(b, ']');
}

static void
jsonbobjkey(Jsonbuilder *b, String8 key)
{
	jsonbwritestr(b, key);
	jsonbwritec(b, ':');
}

static void
jsonbarrcomma(Jsonbuilder *b)
{
	jsonbwritec(b, ',');
}

static void
jsonbobjcomma(Jsonbuilder *b)
{
	jsonbwritec(b, ',');
}

static String8
jsonbfinish(Jsonbuilder *b)
{
	return str8(b->data, b->pos);
}
