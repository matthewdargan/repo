////////////////////////////////
//~ Character Classification/Conversion Functions

internal b32 char_is_space(u8 c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
internal b32 char_is_upper(u8 c) { return 'A' <= c && c <= 'Z'; }
internal b32 char_is_lower(u8 c) { return 'a' <= c && c <= 'z'; }
internal b32 char_is_alpha(u8 c) { return char_is_upper(c) || char_is_lower(c); }

internal b32
char_is_digit(u8 c, u32 base)
{
  b32 result = 0;
  if(base > 1 && base <= 16)
  {
    u8 val = integer_symbol_reverse[c];
    if(val < base) { result = 1; }
  }
  return result;
}

internal u8
lower_from_char(u8 c)
{
  if(char_is_upper(c)) { c += ('a' - 'A'); }
  return c;
}

internal u8
upper_from_char(u8 c)
{
  if(char_is_lower(c)) { c += ('A' - 'a'); }
  return c;
}

internal u64
cstring8_length(u8 *c)
{
  u64 length = 0;
  if(c != 0)
  {
    u8 *ptr = c;
    for(; *ptr != 0; ptr += 1);
    length = (u64)(ptr - c);
  }
  return length;
}

////////////////////////////////
//~ String Constructors

internal String8
str8(u8 *str, u64 size)
{
  String8 result = {str, size};
  return result;
}

internal String8
str8_range(u8 *first, u8 *one_past_last)
{
  String8 result = {first, (u64)(one_past_last - first)};
  return result;
}

internal String8
str8_zero(void)
{
  String8 result = {0};
  return result;
}

internal String8
str8_cstring(char *c)
{
  String8 result = {(u8 *)c, cstring8_length((u8 *)c)};
  return result;
}

////////////////////////////////
//~ String Stylization

internal String8
upper_from_str8(Arena *arena, String8 string)
{
  string = str8_copy(arena, string);
  for(u64 i = 0; i < string.size; i += 1) { string.str[i] = upper_from_char(string.str[i]); }
  return string;
}

internal String8
lower_from_str8(Arena *arena, String8 string)
{
  string = str8_copy(arena, string);
  for(u64 i = 0; i < string.size; i += 1) { string.str[i] = lower_from_char(string.str[i]); }
  return string;
}

////////////////////////////////
//~ String Matching

internal b32
str8_match(String8 a, String8 b, StringMatchFlags flags)
{
  b32 result = 0;
  if(a.size == b.size && flags == 0) { result = MemoryMatch(a.str, b.str, b.size); }
  else if(a.size == b.size || (flags & StringMatchFlag_RightSideSloppy))
  {
    b32 case_insensitive = (flags & StringMatchFlag_CaseInsensitive);
    u64 size             = Min(a.size, b.size);
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

internal u64
str8_find_needle(String8 string, u64 start_pos, String8 needle, StringMatchFlags flags)
{
  u8 *ptr         = string.str + start_pos;
  u64 stop_offset = Max(string.size + 1, needle.size) - needle.size;
  u8 *stop_ptr    = string.str + stop_offset;
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
        if(str8_match(str8_range(ptr + 1, string_opl), needle_tail, adjusted_flags)) { break; }
      }
    }
  }
  u64 result = string.size;
  if(ptr < stop_ptr) { result = (u64)(ptr - string.str); }
  return result;
}

internal u64
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

////////////////////////////////
//~ String Slicing

internal String8
str8_substr(String8 str, Rng1U64 range)
{
  range.min = Min(range.min, str.size);
  range.max = Min(range.max, str.size);
  str.str += range.min;
  str.size = dim_1u64(range);
  return str;
}

internal String8
str8_prefix(String8 str, u64 size)
{
  str.size = Min(size, str.size);
  return str;
}

internal String8
str8_skip(String8 str, u64 amt)
{
  amt = Min(amt, str.size);
  str.str  += amt;
  str.size -= amt;
  return str;
}

internal String8
str8_postfix(String8 str, u64 size)
{
  size = Min(size, str.size);
  str.str  = str.str + str.size - size;
  str.size = size;
  return str;
}

internal String8
str8_chop(String8 str, u64 amt)
{
  amt = Min(amt, str.size);
  str.size -= amt;
  return str;
}

internal String8
str8_skip_chop_whitespace(String8 string)
{
  u8 *first = string.str;
  u8 *opl   = first + string.size;
  for(; first < opl; first += 1)
  {
    if(!char_is_space(*first)) { break; }
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

////////////////////////////////
//~ String Formatting/Copying

internal String8
str8_cat(Arena *arena, String8 s1, String8 s2)
{
  String8 str;
  str.size = s1.size + s2.size;
  str.str  = push_array_no_zero(arena, u8, str.size + 1);
  MemoryCopy(str.str, s1.str, s1.size);
  MemoryCopy(str.str + s1.size, s2.str, s2.size);
  str.str[str.size] = 0;
  return str;
}

internal String8
str8_copy(Arena *arena, String8 s)
{
  String8 str;
  str.size = s.size;
  str.str  = push_array_no_zero(arena, u8, str.size + 1);
  MemoryCopy(str.str, s.str, s.size);
  str.str[str.size] = 0;
  return str;
}

internal String8
str8fv(Arena *arena, char *fmt, va_list args)
{
  va_list args2;
  va_copy(args2, args);
  u32 needed_bytes = base_vsnprintf(0, 0, fmt, args) + 1;
  String8 result = str8_zero();
  result.str  = push_array_no_zero(arena, u8, needed_bytes);
  result.size = base_vsnprintf((char *)result.str, needed_bytes, fmt, args2);
  result.str[result.size] = 0;
  va_end(args2);
  return result;
}

internal String8
str8f(Arena *arena, char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  String8 s = str8fv(arena, fmt, args);
  va_end(args);
  return s;
}

////////////////////////////////
//~ String <-> Integer Conversions

internal b32
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

internal u64
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

internal u32
u32_from_str8(String8 string, u32 radix)
{
  u64 x64 = u64_from_str8(string, radix);
  u32 x32 = (u32)x64;
  return x32;
}

internal b32
try_u64_from_str8(String8 string, u64 *x)
{
  // unpack radix / prefix size based on string prefix
  u64 radix = 0;
  u64 prefix_size = 0;
  {
    if(str8_match(str8_prefix(string, 2), str8_lit("0x"), StringMatchFlag_CaseInsensitive))                        { radix = 0x10, prefix_size = 2; }
    else if(str8_match(str8_prefix(string, 2), str8_lit("0b"), StringMatchFlag_CaseInsensitive))                   { radix = 2,    prefix_size = 2; }
    else if(str8_match(str8_prefix(string, 1), str8_lit("0"), StringMatchFlag_CaseInsensitive) && string.size > 1) { radix = 010,  prefix_size = 1; }
    else                                                                                                           { radix = 10,   prefix_size = 0; }
  }

  // convert if we can
  String8 integer = str8_skip(string, prefix_size);
  b32 is_integer = str8_is_integer(integer, radix);
  if(is_integer) { *x = u64_from_str8(integer, radix); }

  return is_integer;
}

internal String8
str8_from_u64(Arena *arena, u64 value, u32 radix, u8 min_digits, u8 digit_group_separator)
{
  String8 result = str8_zero();
  {
    String8 prefix = str8_zero();
    switch(radix)
    {
    case 16: { prefix = str8_lit("0x"); }break;
    case 8:  { prefix = str8_lit("0o"); }break;
    case 2:  { prefix = str8_lit("0b"); }break;
    }

    // determine # of chars between separators
    u8 digit_group_size = 3;
    switch(radix)
    {
    case 2:
    case 8:
    case 16: { digit_group_size = 4; }break;
    default: break;
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
          if(u64_reduce == 0) { break; }
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
        if(u64_reduce == 0) { break; }
      }
      for(u64 i = 0; i < needed_leading_0s; i += 1) { result.str[prefix.size + i] = '0'; }
    }

    // fill prefix
    if(prefix.size != 0) { MemoryCopy(result.str, prefix.str, prefix.size); }
  }
  return result;
}

////////////////////////////////
//~ String <-> Float Conversions

internal f64
f64_from_str8(String8 string)
{
  f64 result = 0.0;
  u64 i = 0;

  f64 sign = 1.0;
  if(i < string.size && (string.str[i] == '-' || string.str[i] == '+'))
  {
    if(string.str[i] == '-') { sign = -1.0; }
    i += 1;
  }

  f64 integer_part = 0.0;
  for(; i < string.size && char_is_digit(string.str[i], 10); i += 1)
  {
    integer_part = integer_part * 10.0 + (f64)(string.str[i] - '0');
  }

  f64 fractional_part = 0.0;
  u64 fractional_digits = 0;
  if(i < string.size && string.str[i] == '.')
  {
    i += 1;
    for(; i < string.size && char_is_digit(string.str[i], 10); i += 1)
    {
      fractional_part = fractional_part * 10.0 + (f64)(string.str[i] - '0');
      fractional_digits += 1;
    }
  }

  s64 exponent_value = 0;
  if(i < string.size && (string.str[i] == 'e' || string.str[i] == 'E'))
  {
    i += 1;
    s64 exponent_sign = 1;
    if(i < string.size && (string.str[i] == '-' || string.str[i] == '+'))
    {
      if(string.str[i] == '-') { exponent_sign = -1; }
      i += 1;
    }
    for(; i < string.size && char_is_digit(string.str[i], 10); i += 1)
    {
      exponent_value = exponent_value * 10 + (string.str[i] - '0');
    }
    exponent_value *= exponent_sign;
  }

  result = integer_part + fractional_part * pow_f64(10.0, -(f64)fractional_digits);
  result *= sign;
  if(exponent_value != 0) { result *= pow_f64(10.0, (f64)exponent_value); }

  return result;
}

////////////////////////////////
//~ String <-> DateTime Conversions

internal String8
str8_from_datetime(Arena *arena, DateTime dt)
{
  String8 result = str8f(arena, "%04u-%02u-%02u %02u:%02u:%02u", dt.year, dt.mon, dt.day, dt.hour, dt.min, dt.sec);
  return result;
}

////////////////////////////////
//~ String List Construction Functions

internal String8Node *
str8_list_push_node(String8List *list, String8Node *node, String8 string)
{
  SLLQueuePush(list->first, list->last, node);
  list->node_count += 1;
  list->total_size += string.size;
  node->string = string;
  return node;
}

internal String8Node *
str8_list_push(Arena *arena, String8List *list, String8 string)
{
  String8Node *node = push_array_no_zero(arena, String8Node, 1);
  str8_list_push_node(list, node, string);
  return node;
}

internal String8Node *
str8_list_pushf(Arena *arena, String8List *list, char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  String8 string = str8fv(arena, fmt, args);
  String8Node *result = str8_list_push(arena, list, string);
  va_end(args);
  return result;
}

////////////////////////////////
//~ String Splitting/Joining

internal String8List
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

internal String8
str8_list_join(Arena *arena, String8List list, StringJoin *optional_params)
{
  StringJoin join = {0};
  if(optional_params != 0) { MemoryCopyStruct(&join, optional_params); }
  u64 sep_count = 0;
  if(list.node_count > 0) { sep_count = list.node_count - 1; }
  String8 result;
  result.size = join.pre.size + join.post.size + sep_count * join.sep.size + list.total_size;
  u8 *ptr = result.str = push_array_no_zero(arena, u8, result.size + 1);
  MemoryCopy(ptr, join.pre.str, join.pre.size);
  ptr += join.pre.size;
  for(String8Node *node = list.first; node != 0; node = node->next)
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

////////////////////////////////
//~ String Arrays

internal String8Array
str8_array_from_list(Arena *arena, String8List list)
{
  String8Array array;
  array.count = list.node_count;
  array.v     = push_array_no_zero(arena, String8, array.count);
  u64 i = 0;
  for(String8Node *node = list.first; node != 0; node = node->next)
  {
    array.v[i] = node->string;
    i += 1;
  }
  return array;
}

internal String8Array
str8_array_reserve(Arena *arena, u64 count)
{
  String8Array array;
  array.count = 0;
  array.v     = push_array(arena, String8, count);
  return array;
}

////////////////////////////////
//~ String Path Helpers

internal String8
str8_chop_last_slash(String8 string)
{
  if(string.size > 0)
  {
    u8 *ptr = string.str + string.size - 1;
    for(; ptr >= string.str; ptr -= 1)
    {
      if(*ptr == '/') { break; }
    }
    if(ptr >= string.str) { string.size = ptr - string.str; }
    else                  { string.size = 0; }
  }
  return string;
}

internal String8
str8_skip_last_slash(String8 string)
{
  if(string.size > 0)
  {
    u8 *ptr = string.str + string.size - 1;
    for(; ptr >= string.str; ptr -= 1)
    {
      if(*ptr == '/') { break; }
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

internal String8
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

internal String8
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

////////////////////////////////
//~ Basic Text Indentation

internal String8
indented_from_string(Arena *arena, String8 string)
{
  Temp scratch = scratch_begin(&arena, 1);
  read_only local_persist u8 indentation_bytes[] =
      "                                                                                                                "
      "                ";
  String8List indented_strings = {0};
  s64 depth          = 0;
  s64 next_depth     = 0;
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
  String8 result = str8_list_join(arena, indented_strings, 0);
  scratch_end(scratch);
  return result;
}

////////////////////////////////
//~ Basic String Hashes

internal u64
u64_hash_from_str8(String8 string)
{
  u64 hash = 5381;
  for(u64 i = 0; i < string.size; i += 1) { hash = ((hash << 5) + hash) + string.str[i]; }
  return hash;
}

////////////////////////////////
//~ Base64 Encoding/Decoding

global read_only u8 base64_alphabet[64] =
{
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
  'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
  'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
  'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
  'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
  'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
  'w', 'x', 'y', 'z', '0', '1', '2', '3',
  '4', '5', '6', '7', '8', '9', '+', '/',
};

global read_only u8 base64_decode_table[256] =
{
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x3E, 0xFF, 0xFF, 0xFF, 0x3F,
  0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E,
  0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

internal String8
str8_base64_encode(Arena *arena, String8 data)
{
  u64 encoded_size = ((data.size + 2) / 3) * 4;
  u8 *out          = push_array(arena, u8, encoded_size);
  u64 j            = 0;

  u64 i = 0;
  for(; i + 2 < data.size; i += 3)
  {
    u8 b0 = data.str[i];
    u8 b1 = data.str[i + 1];
    u8 b2 = data.str[i + 2];
    out[j + 0] = base64_alphabet[b0 >> 2];
    out[j + 1] = base64_alphabet[((b0 << 4) | (b1 >> 4)) & 0x3F];
    out[j + 2] = base64_alphabet[((b1 << 2) | (b2 >> 6)) & 0x3F];
    out[j + 3] = base64_alphabet[b2 & 0x3F];
    j += 4;
  }

  u64 remaining = data.size - i;
  if(remaining == 2)
  {
    u8 b0 = data.str[i];
    u8 b1 = data.str[i + 1];
    out[j + 0] = base64_alphabet[b0 >> 2];
    out[j + 1] = base64_alphabet[((b0 << 4) | (b1 >> 4)) & 0x3F];
    out[j + 2] = base64_alphabet[(b1 << 2) & 0x3F];
    out[j + 3] = '=';
  }
  else if(remaining == 1)
  {
    u8 b0 = data.str[i];
    out[j + 0] = base64_alphabet[b0 >> 2];
    out[j + 1] = base64_alphabet[(b0 << 4) & 0x3F];
    out[j + 2] = '=';
    out[j + 3] = '=';
  }

  return str8(out, encoded_size);
}

internal String8
str8_base64_decode(Arena *arena, String8 base64)
{
  if(base64.size == 0) { return str8_zero(); }

  u64 padding = 0;
  if(base64.str[base64.size - 1] == '=')                     { padding += 1; }
  if(base64.size >= 2 && base64.str[base64.size - 2] == '=') { padding += 1; }

  u64 decoded_size = (base64.size / 4) * 3 - padding;
  u8 *out          = push_array(arena, u8, decoded_size);
  u64 j            = 0;

  for(u64 i = 0; i + 3 < base64.size; i += 4)
  {
    u8 v0 = base64_decode_table[base64.str[i]];
    u8 v1 = base64_decode_table[base64.str[i + 1]];
    u8 v2 = base64_decode_table[base64.str[i + 2]];
    u8 v3 = base64_decode_table[base64.str[i + 3]];

    if((v0 | v1) == 0xFF) { return str8_zero(); }

    out[j] = (v0 << 2) | (v1 >> 4);
    j += 1;

    if(base64.str[i + 2] != '=' && j < decoded_size)
    {
      if(v2 == 0xFF) { return str8_zero(); }
      out[j] = (v1 << 4) | (v2 >> 2);
      j += 1;
    }

    if(base64.str[i + 3] != '=' && j < decoded_size)
    {
      if(v3 == 0xFF) { return str8_zero(); }
      out[j] = (v2 << 6) | v3;
      j += 1;
    }
  }

  return str8(out, decoded_size);
}
