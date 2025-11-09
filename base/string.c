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
	if(base > 1 && base <= 16)
	{
		u8 val = integer_symbol_reverse[c];
		if(val < base)
		{
			result = 1;
		}
	}
	return result;
}

static u8
lower_from_char(u8 c)
{
	if(char_is_upper(c))
	{
		c += ('a' - 'A');
	}
	return c;
}

static u8
upper_from_char(u8 c)
{
	if(char_is_lower(c))
	{
		c += ('A' - 'a');
	}
	return c;
}

static u64
cstring8_length(u8 *c)
{
	u64 length = 0;
	if(c != 0)
	{
		u8 *ptr = c;
		for(; *ptr != 0; ptr += 1)
			;
		length = (u64)(ptr - c);
	}
	return length;
}

// String Constructors
static String8
str8(u8 *str, u64 size)
{
	String8 result = {str, size};
	return result;
}

static String8
str8_range(u8 *first, u8 *one_past_last)
{
	String8 result = {first, (u64)(one_past_last - first)};
	return result;
}

static String8
str8_zero(void)
{
	String8 result = {0};
	return result;
}

static String8
str8_cstring(char *c)
{
	String8 result = {(u8 *)c, cstring8_length((u8 *)c)};
	return result;
}

// String Stylization
static String8
upper_from_str8(Arena *arena, String8 string)
{
	string = str8_copy(arena, string);
	for(u64 i = 0; i < string.size; i += 1)
	{
		string.str[i] = upper_from_char(string.str[i]);
	}
	return string;
}

static String8
lower_from_str8(Arena *arena, String8 string)
{
	string = str8_copy(arena, string);
	for(u64 i = 0; i < string.size; i += 1)
	{
		string.str[i] = lower_from_char(string.str[i]);
	}
	return string;
}

// String Matching
static b32
str8_match(String8 a, String8 b, StringMatchFlags flags)
{
	b32 result = 0;
	if(a.size == b.size && flags == 0)
	{
		result = MemoryMatch(a.str, b.str, b.size);
	}
	else if(a.size == b.size || (flags & StringMatchFlag_RightSideSloppy))
	{
		b32 case_insensitive = (flags & StringMatchFlag_CaseInsensitive);
		u64 size = Min(a.size, b.size);
		result = 1;
		for(u64 i = 0; i < size; i += 1)
		{
			u8 at = a.str[i];
			u8 bt = b.str[i];
			if(case_insensitive)
			{
				at = upper_from_char(at);
				bt = upper_from_char(bt);
			}
			if(at != bt)
			{
				result = 0;
				break;
			}
		}
	}
	return result;
}

static u64
str8_find_needle(String8 string, u64 start_pos, String8 needle, StringMatchFlags flags)
{
	u8 *ptr = string.str + start_pos;
	u64 stop_offset = Max(string.size + 1, needle.size) - needle.size;
	u8 *stop_ptr = string.str + stop_offset;
	if(needle.size > 0)
	{
		u8 *string_opl = string.str + string.size;
		String8 needle_tail = str8_skip(needle, 1);
		StringMatchFlags adjusted_flags = flags | StringMatchFlag_RightSideSloppy;
		u8 needle_first_char_adjusted = needle.str[0];
		if(adjusted_flags & StringMatchFlag_CaseInsensitive)
		{
			needle_first_char_adjusted = upper_from_char(needle_first_char_adjusted);
		}
		for(; ptr < stop_ptr; ptr += 1)
		{
			u8 haystack_char_adjusted = *ptr;
			if(adjusted_flags & StringMatchFlag_CaseInsensitive)
			{
				haystack_char_adjusted = upper_from_char(haystack_char_adjusted);
			}
			if(haystack_char_adjusted == needle_first_char_adjusted)
			{
				if(str8_match(str8_range(ptr + 1, string_opl), needle_tail, adjusted_flags))
				{
					break;
				}
			}
		}
	}
	u64 result = string.size;
	if(ptr < stop_ptr)
	{
		result = (u64)(ptr - string.str);
	}
	return result;
}

static u64
str8_find_needle_reverse(String8 string, u64 start_pos, String8 needle, StringMatchFlags flags)
{
	u64 result = 0;
	for(s64 i = string.size - start_pos - needle.size; i >= 0; i -= 1)
	{
		String8 haystack = str8_substr(string, rng_1u64(i, i + needle.size));
		if(str8_match(haystack, needle, flags))
		{
			result = (u64)i + needle.size;
			break;
		}
	}
	return result;
}

// String Slicing
static String8
str8_substr(String8 str, Rng1U64 range)
{
	range.min = Min(range.min, str.size);
	range.max = Min(range.max, str.size);
	str.str += range.min;
	str.size = dim_1u64(range);
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

static String8
str8_skip_chop_whitespace(String8 string)
{
	u8 *first = string.str;
	u8 *opl = first + string.size;
	for(; first < opl; first += 1)
	{
		if(!char_is_space(*first))
		{
			break;
		}
	}
	for(; opl > first;)
	{
		opl -= 1;
		if(!char_is_space(*opl))
		{
			opl += 1;
			break;
		}
	}
	String8 result = str8_range(first, opl);
	return result;
}

// String Formatting/Copying
static String8
str8_cat(Arena *arena, String8 s1, String8 s2)
{
	String8 str;
	str.size = s1.size + s2.size;
	str.str = push_array_no_zero(arena, u8, str.size + 1);
	MemoryCopy(str.str, s1.str, s1.size);
	MemoryCopy(str.str + s1.size, s2.str, s2.size);
	str.str[str.size] = 0;
	return str;
}

static String8
str8_copy(Arena *arena, String8 s)
{
	String8 str;
	str.size = s.size;
	str.str = push_array_no_zero(arena, u8, str.size + 1);
	MemoryCopy(str.str, s.str, s.size);
	str.str[str.size] = 0;
	return str;
}

static String8
str8fv(Arena *arena, char *fmt, va_list args)
{
	va_list args2;
	va_copy(args2, args);
	u32 needed_bytes = base_vsnprintf(0, 0, fmt, args) + 1;
	String8 result = str8_zero();
	result.str = push_array_no_zero(arena, u8, needed_bytes);
	result.size = base_vsnprintf((char *)result.str, needed_bytes, fmt, args2);
	result.str[result.size] = 0;
	va_end(args2);
	return result;
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
	if(string.size > 0)
	{
		if(radix > 1 && radix <= 16)
		{
			result = 1;
			for(u64 i = 0; i < string.size; i += 1)
			{
				u8 c = string.str[i];
				if(!(c < 0x80) || integer_symbol_reverse[c] >= radix)
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
	if(radix > 1 && radix <= 16)
	{
		for(u64 i = 0; i < string.size; i += 1)
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
	// unpack radix / prefix size based on string prefix
	u64 radix = 0;
	u64 prefix_size = 0;
	{
		// hex
		if(str8_match(str8_prefix(string, 2), str8_lit("0x"), StringMatchFlag_CaseInsensitive))
		{
			radix = 0x10, prefix_size = 2;
		}
		// binary
		else if(str8_match(str8_prefix(string, 2), str8_lit("0b"), StringMatchFlag_CaseInsensitive))
		{
			radix = 2, prefix_size = 2;
		}
		// octal
		else if(str8_match(str8_prefix(string, 1), str8_lit("0"), StringMatchFlag_CaseInsensitive) && string.size > 1)
		{
			radix = 010, prefix_size = 1;
		}
		// decimal
		else
		{
			radix = 10, prefix_size = 0;
		}
	}

	// convert if we can
	String8 integer = str8_skip(string, prefix_size);
	b32 is_integer = str8_is_integer(integer, radix);
	if(is_integer)
	{
		*x = u64_from_str8(integer, radix);
	}

	return is_integer;
}

static String8
str8_from_u64(Arena *arena, u64 value, u32 radix, u8 min_digits, u8 digit_group_separator)
{
	String8 result = str8_zero();
	{
		String8 prefix = str8_zero();
		switch(radix)
		{
			case 16:
			{
				prefix = str8_lit("0x");
			}
			break;
			case 8:
			{
				prefix = str8_lit("0o");
			}
			break;
			case 2:
			{
				prefix = str8_lit("0b");
			}
			break;
		}

		// determine # of chars between separators
		u8 digit_group_size = 3;
		switch(radix)
		{
			case 2:
			case 8:
			case 16:
			{
				digit_group_size = 4;
			}
			break;
			default:
				break;
		}

		// prep
		u64 needed_leading_0s = 0;
		{
			u64 needed_digits = 1;
			{
				u64 u64_reduce = value;
				for(;;)
				{
					u64_reduce /= radix;
					if(u64_reduce == 0)
					{
						break;
					}
					needed_digits += 1;
				}
			}
			needed_leading_0s = (min_digits > needed_digits) ? min_digits - needed_digits : 0;
			u64 needed_separators = 0;
			if(digit_group_separator != 0)
			{
				needed_separators = (needed_digits + needed_leading_0s) / digit_group_size;
				if(needed_separators > 0 && (needed_digits + needed_leading_0s) % digit_group_size == 0)
				{
					needed_separators -= 1;
				}
			}
			result.size = prefix.size + needed_leading_0s + needed_separators + needed_digits;
			result.str = push_array_no_zero(arena, u8, result.size + 1);
			result.str[result.size] = 0;
		}

		// fill contents
		{
			u64 u64_reduce = value;
			u64 digits_until_separator = digit_group_size;
			for(u64 i = 0; i < result.size; i += 1)
			{
				if(digits_until_separator == 0 && digit_group_separator != 0)
				{
					result.str[result.size - i - 1] = digit_group_separator;
					digits_until_separator = digit_group_size + 1;
				}
				else
				{
					result.str[result.size - i - 1] = lower_from_char(integer_symbols[u64_reduce % radix]);
					u64_reduce /= radix;
				}
				digits_until_separator -= 1;
				if(u64_reduce == 0)
				{
					break;
				}
			}
			for(u64 i = 0; i < needed_leading_0s; i += 1)
			{
				result.str[prefix.size + i] = '0';
			}
		}

		// fill prefix
		if(prefix.size != 0)
		{
			MemoryCopy(result.str, prefix.str, prefix.size);
		}
	}
	return result;
}

// String List Construction Functions
static String8Node *
str8_list_push_node(String8List *list, String8Node *node, String8 string)
{
	SLLQueuePush(list->first, list->last, node);
	list->node_count += 1;
	list->total_size += string.size;
	node->string = string;
	return node;
}

static String8Node *
str8_list_push(Arena *arena, String8List *list, String8 string)
{
	String8Node *node = push_array_no_zero(arena, String8Node, 1);
	str8_list_push_node(list, node, string);
	return node;
}

static String8Node *
str8_list_pushf(Arena *arena, String8List *list, char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	String8 string = str8fv(arena, fmt, args);
	String8Node *result = str8_list_push(arena, list, string);
	va_end(args);
	return result;
}

// String Splitting/Joining
static String8List
str8_split(Arena *arena, String8 string, u8 *split_chars, u64 split_char_count, StringSplitFlags flags)
{
	String8List list = {0};
	u8 *opl = string.str + string.size;
	for(u8 *ptr = string.str; ptr < opl;)
	{
		u8 *first = ptr;
		for(; ptr < opl; ptr += 1)
		{
			u8 c = *ptr;
			b32 issplit = 0;
			for(u64 i = 0; i < split_char_count; i += 1)
			{
				if(split_chars[i] == c)
				{
					issplit = 1;
					break;
				}
			}
			if(issplit)
			{
				break;
			}
		}
		String8 ss = str8_range(first, ptr);
		if((flags & StringSplitFlag_KeepEmpties) || ss.size > 0)
		{
			str8_list_push(arena, &list, ss);
		}
		ptr += 1;
	}
	return list;
}

static String8
str8_list_join(Arena *arena, String8List *list, StringJoin *optional_params)
{
	StringJoin join = {0};
	if(optional_params != 0)
	{
		MemoryCopyStruct(&join, optional_params);
	}
	u64 sep_count = 0;
	if(list->node_count > 0)
	{
		sep_count = list->node_count - 1;
	}
	String8 result;
	result.size = join.pre.size + join.post.size + sep_count * join.sep.size + list->total_size;
	u8 *ptr = result.str = push_array_no_zero(arena, u8, result.size + 1);
	MemoryCopy(ptr, join.pre.str, join.pre.size);
	ptr += join.pre.size;
	for(String8Node *node = list->first; node != 0; node = node->next)
	{
		MemoryCopy(ptr, node->string.str, node->string.size);
		ptr += node->string.size;
		if(node->next != 0)
		{
			MemoryCopy(ptr, join.sep.str, join.sep.size);
			ptr += join.sep.size;
		}
	}
	MemoryCopy(ptr, join.post.str, join.post.size);
	ptr += join.post.size;
	*ptr = 0;
	return result;
}

// String Arrays
static String8Array
str8_array_from_list(Arena *arena, String8List *list)
{
	String8Array array;
	array.count = list->node_count;
	array.v = push_array_no_zero(arena, String8, array.count);
	u64 i = 0;
	for(String8Node *node = list->first; node != 0; node = node->next)
	{
		array.v[i] = node->string;
		i += 1;
	}
	return array;
}

static String8Array
str8_array_reserve(Arena *arena, u64 count)
{
	String8Array array;
	array.count = 0;
	array.v = push_array(arena, String8, count);
	return array;
}

// String Path Helpers
static String8
str8_chop_last_slash(String8 string)
{
	if(string.size > 0)
	{
		u8 *ptr = string.str + string.size - 1;
		for(; ptr >= string.str; ptr -= 1)
		{
			if(*ptr == '/')
			{
				break;
			}
		}
		if(ptr >= string.str)
		{
			string.size = ptr - string.str;
		}
		else
		{
			string.size = 0;
		}
	}
	return string;
}

static String8
str8_skip_last_slash(String8 string)
{
	if(string.size > 0)
	{
		u8 *ptr = string.str + string.size - 1;
		for(; ptr >= string.str; ptr -= 1)
		{
			if(*ptr == '/')
			{
				break;
			}
		}
		if(ptr >= string.str)
		{
			ptr += 1;
			string.size = string.str + string.size - ptr;
			string.str = ptr;
		}
	}
	return string;
}

static String8
str8_chop_last_dot(String8 string)
{
	String8 result = string;
	u64 p = string.size;
	for(; p > 0;)
	{
		p -= 1;
		if(string.str[p] == '.')
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
	for(; p > 0;)
	{
		p -= 1;
		if(string.str[p] == '.')
		{
			result = str8_skip(string, p + 1);
			break;
		}
	}
	return result;
}

// Basic Text Indentation
static String8
indented_from_string(Arena *arena, String8 string)
{
	Temp scratch = scratch_begin(&arena, 1);
	read_only static u8 indentation_bytes[] =
	    "                                                                                                                "
	    "                ";
	String8List indented_strings = {0};
	s64 depth = 0;
	s64 next_depth = 0;
	u64 line_begin_off = 0;
	for(u64 off = 0; off <= string.size; off += 1)
	{
		u8 byte = off < string.size ? string.str[off] : 0;
		switch(byte)
		{
			case '{':
			case '[':
			case '(':
			{
				next_depth += 1;
				next_depth = Max(0, next_depth);
			}
			break;
			case '}':
			case ']':
			case ')':
			{
				next_depth -= 1;
				next_depth = Max(0, next_depth);
				depth = next_depth;
			}
			break;
			case '\n':
			case 0:
			{
				String8 line = str8_skip_chop_whitespace(str8_substr(string, rng_1u64(line_begin_off, off)));
				if(line.size != 0)
				{
					str8_list_pushf(scratch.arena, &indented_strings, "%.*s%S\n", (int)depth * 2, indentation_bytes, line);
				}
				if(line.size == 0 && indented_strings.node_count != 0 && off < string.size)
				{
					str8_list_pushf(scratch.arena, &indented_strings, "\n");
				}
				line_begin_off = off + 1;
				depth = next_depth;
			}
			break;
			default:
				break;
		}
	}
	String8 result = str8_list_join(arena, &indented_strings, 0);
	scratch_end(scratch);
	return result;
}

// Basic String Hashes
static u64
u64_hash_from_str8(String8 string)
{
	u64 hash = 5381;
	for(u64 i = 0; i < string.size; i += 1)
	{
		hash = ((hash << 5) + hash) + string.str[i];
	}
	return hash;
}
