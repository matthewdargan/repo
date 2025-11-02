// Character Classification/Conversion Functions
static b32
char_is_space(u8 c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static b32
char_is_upper(u8 c)
{
	return 'A' <= c && c <= 'Z';
}

static b32
char_is_lower(u8 c)
{
	return 'a' <= c && c <= 'z';
}

static b32
char_is_alpha(u8 c)
{
	return char_is_upper(c) || char_is_lower(c);
}

static b32
char_is_digit(u8 c, u32 base)
{
	b32 result = 0;
	if (base > 1 && base <= 16)
	{
		u8 val = integer_symbol_reverse[c];
		if (val < base)
		{
			result = 1;
		}
	}
	return result;
}

static u8
lower_from_char(u8 c)
{
	if (char_is_upper(c))
	{
		c += ('a' - 'A');
	}
	return c;
}

static u8
upper_from_char(u8 c)
{
	if (char_is_lower(c))
	{
		c += ('A' - 'a');
	}
	return c;
}

static u64
cstring8_length(u8 *c)
{
	u8 *p = c;
	while (*p != 0)
	{
		p++;
	}
	return p - c;
}

// String Constructors
static String8
str8(u8 *str, u64 size)
{
	String8 s = {.str = str, .size = size};
	return s;
}

static String8
str8_range(u8 *first, u8 *one_past_last)
{
	String8 s = {.str = first, .size = one_past_last - first};
	return s;
}

static String8
str8_zero(void)
{
	String8 s = {0};
	return s;
}

static String8
str8_cstring(char *c)
{
	u8 *str = (u8 *)c;
	String8 s = {.str = str, .size = cstring8_length(str)};
	return s;
}

static Rng1U64
rng1u64(u64 min, u64 max)
{
	Rng1U64 r = {.min = min, .max = max};
	if (r.min > r.max)
	{
		u64 t = r.min;
		r.min = r.max;
		r.max = t;
	}
	return r;
}

static u64
dim1u64(Rng1U64 range)
{
	return range.max > range.min ? (range.max - range.min) : 0;
}

// String Stylization
static String8
upper_from_str8(Arena *arena, String8 string)
{
	string = str8_copy(arena, string);
	for (u64 i = 0; i < string.size; i++)
	{
		string.str[i] = upper_from_char(string.str[i]);
	}
	return string;
}

static String8
lower_from_str8(Arena *arena, String8 string)
{
	string = str8_copy(arena, string);
	for (u64 i = 0; i < string.size; i++)
	{
		string.str[i] = lower_from_char(string.str[i]);
	}
	return string;
}

// String Matching
static b32
str8_match(String8 a, String8 b, StringMatchFlags flags)
{
	b32 ok = 0;
	if (a.size == b.size && flags == 0)
	{
		ok = memcmp(a.str, b.str, b.size) == 0;
	}
	else if (a.size == b.size || (flags & StringMatchFlag_RightSideSloppy))
	{
		u64 size = Min(a.size, b.size);
		ok = 1;
		for (u64 i = 0; i < size; i++)
		{
			u8 at = a.str[i];
			u8 bt = b.str[i];
			if (flags & StringMatchFlag_CaseInsensitive)
			{
				at = upper_from_char(at);
				bt = upper_from_char(bt);
			}
			if (at != bt)
			{
				ok = 0;
				break;
			}
		}
	}
	return ok;
}

static u64
str8_find_needle(String8 string, u64 start_pos, String8 needle, StringMatchFlags flags)
{
	if (needle.size == 0)
	{
		return string.size;
	}
	u8 *p = string.str + start_pos;
	u64 stoplen = string.size - needle.size;
	u8 *end = string.str + string.size;
	String8 tail = str8_skip(needle, 1);
	StringMatchFlags adjflags = flags | StringMatchFlag_RightSideSloppy;
	u8 n0adj = needle.str[0];
	if (adjflags & StringMatchFlag_CaseInsensitive)
	{
		n0adj = upper_from_char(n0adj);
	}
	for (; (u64)(p - string.str) <= stoplen; p++)
	{
		u8 c = *p;
		if (adjflags & StringMatchFlag_CaseInsensitive)
		{
			c = upper_from_char(c);
		}
		if (c == n0adj)
		{
			String8 hay = str8_range(p + 1, end);
			if (str8_match(hay, tail, adjflags))
			{
				break;
			}
		}
	}
	u64 i = string.size;
	if ((u64)(p - string.str) <= stoplen)
	{
		i = p - string.str;
	}
	return i;
}

static u64
str8_find_needle_reverse(String8 string, u64 start_pos, String8 needle, StringMatchFlags flags)
{
	for (s64 i = string.size - start_pos - needle.size; i >= 0; i--)
	{
		String8 hay = str8(string.str + i, needle.size);
		if (str8_match(hay, needle, flags))
		{
			return i;
		}
	}
	return 0;
}

// String Slicing
static String8
str8_substr(String8 str, Rng1U64 range)
{
	range.min = Min(range.min, str.size);
	range.max = Min(range.max, str.size);
	str.str += range.min;
	str.size = dim1u64(range);
	return str;
}

static String8
str8_prefix(String8 str, u64 size)
{
	str.size = Min(size, str.size);
	return str;
}

static String8
str8_skip(String8 str, u64 amt)
{
	amt = Min(amt, str.size);
	str.str += amt;
	str.size -= amt;
	return str;
}

static String8
str8_postfix(String8 str, u64 size)
{
	size = Min(size, str.size);
	str.str = str.str + str.size - size;
	str.size = size;
	return str;
}

static String8
str8_chop(String8 str, u64 amt)
{
	amt = Min(amt, str.size);
	str.size -= amt;
	return str;
}

// String Formatting/Copying
static String8
str8_cat(Arena *arena, String8 s1, String8 s2)
{
	String8 str = {
	    .str = push_array_no_zero(arena, u8, s1.size + s2.size + 1),
	    .size = s1.size + s2.size,
	};
	memmove(str.str, s1.str, s1.size);
	memmove(str.str + s1.size, s2.str, s2.size);
	str.str[str.size] = 0;
	return str;
}

static String8
str8_copy(Arena *arena, String8 s)
{
	String8 str = {
	    .str = push_array_no_zero(arena, u8, s.size + 1),
	    .size = s.size,
	};
	memcpy(str.str, s.str, s.size);
	str.str[str.size] = 0;
	return str;
}

static String8
str8fv(Arena *arena, char *fmt, va_list args)
{
	va_list args2;
	va_copy(args2, args);
	u32 nbytes = vsnprintf(0, 0, fmt, args) + 1;
	u8 *str = push_array_no_zero(arena, u8, nbytes);
	String8 s = {
	    .str = str,
	    .size = vsnprintf((char *)str, nbytes, fmt, args2),
	};
	s.str[s.size] = 0;
	va_end(args2);
	return s;
}

static String8
str8f(Arena *arena, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	String8 s = str8fv(arena, fmt, args);
	va_end(args);
	return s;
}

// String <-> Integer Conversions
static b32
str8_is_integer(String8 string, u32 radix)
{
	b32 result = 0;
	if (string.size > 0)
	{
		if (radix > 1 && radix <= 16)
		{
			result = 1;
			for (u64 i = 0; i < string.size; i++)
			{
				u8 c = string.str[i];
				if (!(c < 0x80) || integer_symbol_reverse[c] >= radix)
				{
					result = 0;
					break;
				}
			}
		}
	}
	return result;
}

static u64
u64_from_str8(String8 string, u32 radix)
{
	u64 x = 0;
	if (radix > 1 && radix <= 16)
	{
		for (u64 i = 0; i < string.size; i++)
		{
			x *= radix;
			x += integer_symbol_reverse[string.str[i] & 0x7f];
		}
	}
	return x;
}

static u32
u32_from_str8(String8 string, u32 radix)
{
	u64 x64 = u64_from_str8(string, radix);
	u32 x32 = (u32)x64;
	return x32;
}

static b32
try_u64_from_str8(String8 string, u64 *x)
{
	b32 ok = 0;
	if (str8_is_integer(string, 10))
	{
		ok = 1;
		*x = u64_from_str8(string, 10);
	}
	else
	{
		if (string.size >= 2 && str8_match(str8_prefix(string, 2), str8_lit("0x"), 0))
		{
			String8 hex = str8_skip(string, 2);
			if (str8_is_integer(hex, 16))
			{
				ok = 1;
				*x = u64_from_str8(hex, 16);
			}
		}
		else if (string.size >= 2 && str8_match(str8_prefix(string, 2), str8_lit("0b"), 0))
		{
			String8 bin = str8_skip(string, 2);
			if (str8_is_integer(bin, 2))
			{
				ok = 1;
				*x = u64_from_str8(bin, 2);
			}
		}
		else if (string.size >= 1 && string.str[0] == '0')
		{
			String8 oct = str8_skip(string, 1);
			if (str8_is_integer(oct, 8))
			{
				ok = 1;
				*x = u64_from_str8(oct, 8);
			}
		}
	}
	return ok;
}

static String8
str8_from_u64(Arena *arena, u64 value, u32 radix, u8 min_digits, u8 digit_group_separator)
{
	String8 pre = str8_zero();
	switch (radix)
	{
		case 2:
		{
			pre = str8_lit("0b");
		}
		break;
		case 8:
		{
			pre = str8_lit("0");
		}
		break;
		case 16:
		{
			pre = str8_lit("0x");
		}
		break;
		default:
			break;
	}
	u64 group = 3;
	switch (radix)
	{
		case 2:
		case 8:
		case 16:
		{
			group = 4;
		}
		break;
		default:
			break;
	}
	u64 ndigits = 1;
	u64 rem = value;
	for (;;)
	{
		rem /= radix;
		if (rem == 0)
		{
			break;
		}
		ndigits++;
	}
	u64 nleadz = (min_digits > ndigits) ? min_digits - ndigits : 0;
	u64 nseps = 0;
	if (digit_group_separator != 0)
	{
		nseps = (ndigits + nleadz) / group;
		if (nseps > 0 && (ndigits + nleadz) % group == 0)
		{
			nseps--;
		}
	}
	String8 s = {
	    .str = push_array_no_zero(arena, u8, pre.size + nleadz + nseps + ndigits + 1),
	    .size = pre.size + nleadz + nseps + ndigits,
	};
	s.str[s.size] = 0;
	rem = value;
	u64 digtosep = group;
	for (u64 i = 0; i < s.size; i++)
	{
		if (digtosep == 0 && digit_group_separator != 0)
		{
			s.str[s.size - i - 1] = digit_group_separator;
			digtosep = group + 1;
		}
		else
		{
			s.str[s.size - i - 1] = lower_from_char(integer_symbols[rem % radix]);
			rem /= radix;
		}
		digtosep--;
		if (rem == 0)
		{
			break;
		}
	}
	for (u64 leadzidx = 0; leadzidx < nleadz; leadzidx++)
	{
		s.str[pre.size + leadzidx] = '0';
	}
	if (pre.size != 0)
	{
		memcpy(s.str, pre.str, pre.size);
	}
	return s;
}

// String List Construction Functions
static String8Node *
str8_list_push_node(String8List *list, String8Node *node, String8 s)
{
	if (list->first == NULL)
	{
		list->first = node;
		list->last = node;
	}
	else
	{
		list->last->next = node;
		list->last = node;
	}
	node->next = NULL;
	list->node_count++;
	list->total_size += s.size;
	node->string = s;
	return node;
}

static String8Node *
str8_list_push(Arena *arena, String8List *list, String8 string)
{
	String8Node *node = push_array_no_zero(arena, String8Node, 1);
	str8_list_push_node(list, node, string);
	return node;
}

// String Splitting/Joining
static String8List
str8_split(Arena *arena, String8 string, u8 *split_chars, u64 split_char_count, StringSplitFlags flags)
{
	String8List list = {0};
	u8 *end = string.str + string.size;
	for (u8 *p = string.str; p < end;)
	{
		u8 *start = p;
		for (; p < end; p++)
		{
			u8 c = *p;
			b32 issplit = 0;
			for (u64 i = 0; i < split_char_count; i++)
			{
				if (split_chars[i] == c)
				{
					issplit = 1;
					break;
				}
			}
			if (issplit)
			{
				break;
			}
		}
		String8 ss = str8_range(start, p);
		if ((flags & StringSplitFlag_KeepEmpties) || ss.size > 0)
		{
			str8_list_push(arena, &list, ss);
		}
		p++;
	}
	return list;
}

static String8
str8_list_join(Arena *arena, String8List *list, StringJoin *optional_params)
{
	StringJoin join = {0};
	if (optional_params != NULL)
	{
		memcpy(&join, optional_params, sizeof join);
	}
	u64 nsep = 0;
	if (list->node_count > 0)
	{
		nsep = list->node_count - 1;
	}
	String8 s = {
	    .str =
	        push_array_no_zero(arena, u8, join.pre.size + join.post.size + nsep * join.sep.size + list->total_size + 1),
	    .size = join.pre.size + join.post.size + nsep * join.sep.size + list->total_size,
	};
	u8 *p = s.str;
	memcpy(p, join.pre.str, join.pre.size);
	p += join.pre.size;
	for (String8Node *node = list->first; node != NULL; node = node->next)
	{
		memcpy(p, node->string.str, node->string.size);
		p += node->string.size;
		if (node->next != NULL)
		{
			memcpy(p, join.sep.str, join.sep.size);
			p += join.sep.size;
		}
	}
	memcpy(p, join.post.str, join.post.size);
	p += join.post.size;
	*p = 0;
	return s;
}

// String Arrays
static String8Array
str8_array_from_list(Arena *arena, String8List *list)
{
	String8Array array = {
	    .v = push_array_no_zero(arena, String8, list->node_count),
	    .count = list->node_count,
	};
	u64 i = 0;
	for (String8Node *node = list->first; node != NULL; node = node->next, i++)
	{
		array.v[i] = node->string;
	}
	return array;
}

static String8Array
str8_array_reserve(Arena *arena, u64 count)
{
	String8Array array = {
	    .v = push_array(arena, String8, count),
	    .count = 0,
	};
	return array;
}

// String Path Helpers
static String8
str8_chop_last_slash(String8 string)
{
	if (string.size > 0)
	{
		u8 *p = string.str + string.size - 1;
		for (; p >= string.str; p--)
		{
			if (*p == '/')
			{
				break;
			}
		}
		if (p >= string.str)
		{
			string.size = p - string.str;
		}
	}
	return string;
}

static String8
str8_skip_last_slash(String8 string)
{
	if (string.size > 0)
	{
		u8 *p = string.str + string.size - 1;
		for (; p >= string.str; p--)
		{
			if (*p == '/')
			{
				break;
			}
		}
		if (p >= string.str)
		{
			p++;
			string.size = string.str + string.size - p;
			string.str = p;
		}
	}
	return string;
}

static String8
str8_chop_last_dot(String8 string)
{
	String8 result = string;
	u64 p = string.size;
	while (p > 0)
	{
		p--;
		if (string.str[p] == '.')
		{
			result = str8_prefix(string, p);
			break;
		}
	}
	return result;
}

static String8
str8_skip_last_dot(String8 string)
{
	String8 result = string;
	u64 p = string.size;
	while (p > 0)
	{
		p--;
		if (string.str[p] == '.')
		{
			result = str8_skip(string, p + 1);
			break;
		}
	}
	return result;
}
