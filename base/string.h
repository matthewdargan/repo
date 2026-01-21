#ifndef STRING_H
#define STRING_H

////////////////////////////////
//~ String Types

typedef struct String8 String8;
struct String8
{
  u8 *str;
  u64 size;
};

////////////////////////////////
//~ String List/Array Types

typedef struct String8Node String8Node;
struct String8Node
{
  String8Node *next;
  String8 string;
};

typedef struct String8List String8List;
struct String8List
{
  String8Node *first;
  String8Node *last;
  u64 node_count;
  u64 total_size;
};

typedef struct String8Array String8Array;
struct String8Array
{
  String8 *v;
  u64 count;
};

////////////////////////////////
//~ String Matching/Splitting/Joining Types

typedef u32 StringMatchFlags;
enum
{
  StringMatchFlag_CaseInsensitive = (1 << 0),
  StringMatchFlag_RightSideSloppy = (1 << 1),
};

typedef u32 StringSplitFlags;
enum
{
  StringSplitFlag_KeepEmpties = (1 << 0),
};

typedef struct StringJoin StringJoin;
struct StringJoin
{
  String8 pre;
  String8 sep;
  String8 post;
};

////////////////////////////////
//~ String <-> Integer Tables

read_only global u8 integer_symbols[16] = {
    '0',
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    'A',
    'B',
    'C',
    'D',
    'E',
    'F',
};

// NOTE: Includes reverses for uppercase and lowercase hex
read_only global u8 integer_symbol_reverse[128] = {
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0x00,
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0x0a,
    0x0b,
    0x0c,
    0x0d,
    0x0e,
    0x0f,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0x0a,
    0x0b,
    0x0c,
    0x0d,
    0x0e,
    0x0f,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
    0xff,
};

////////////////////////////////
//~ Character Classification/Conversion Functions

internal b32 char_is_space(u8 c);
internal b32 char_is_upper(u8 c);
internal b32 char_is_lower(u8 c);
internal b32 char_is_alpha(u8 c);
internal b32 char_is_digit(u8 c, u32 base);
internal u8 lower_from_char(u8 c);
internal u8 upper_from_char(u8 c);
internal u64 cstring8_length(u8 *c);

////////////////////////////////
//~ String Constructors

#define str8_lit(S) str8((u8 *)(S), sizeof(S) - 1)
#define str8_lit_comp(S) \
  {                      \
      (u8 *)(S),         \
      sizeof(S) - 1,     \
  }
#define str8_varg(S) (int)((S).size), ((S).str)

internal String8 str8(u8 *str, u64 size);
internal String8 str8_range(u8 *first, u8 *one_past_last);
internal String8 str8_zero(void);
internal String8 str8_cstring(char *c);

////////////////////////////////
//~ String Stylization

internal String8 upper_from_str8(Arena *arena, String8 string);
internal String8 lower_from_str8(Arena *arena, String8 string);

////////////////////////////////
//~ String Matching

internal b32 str8_match(String8 a, String8 b, StringMatchFlags flags);
internal u64 str8_find_needle(String8 string, u64 start_pos, String8 needle, StringMatchFlags flags);
internal u64 str8_find_needle_reverse(String8 string, u64 start_pos, String8 needle, StringMatchFlags flags);

////////////////////////////////
//~ String Slicing

internal String8 str8_substr(String8 str, Rng1U64 range);
internal String8 str8_prefix(String8 str, u64 size);
internal String8 str8_skip(String8 str, u64 amt);
internal String8 str8_postfix(String8 str, u64 size);
internal String8 str8_chop(String8 str, u64 amt);
internal String8 str8_skip_chop_whitespace(String8 string);

////////////////////////////////
//~ String Formatting/Copying

internal String8 str8_cat(Arena *arena, String8 s1, String8 s2);
internal String8 str8_copy(Arena *arena, String8 s);
internal String8 str8fv(Arena *arena, char *fmt, va_list args);
internal String8 str8f(Arena *arena, char *fmt, ...);

////////////////////////////////
//~ String <-> Integer Conversions

internal b32 str8_is_integer(String8 string, u32 radix);
internal u64 u64_from_str8(String8 string, u32 radix);
internal u32 u32_from_str8(String8 string, u32 radix);
internal b32 try_u64_from_str8(String8 string, u64 *x);
internal String8 str8_from_u64(Arena *arena, u64 value, u32 radix, u8 min_digits, u8 digit_group_separator);

////////////////////////////////
//~ String <-> Float Conversions

internal f64 f64_from_str8(String8 string);

////////////////////////////////
//~ String <-> DateTime Conversions

internal String8 str8_from_datetime(Arena *arena, DateTime dt);

////////////////////////////////
//~ String List Construction Functions

internal String8Node *str8_list_push_node(String8List *list, String8Node *node, String8 string);
internal String8Node *str8_list_push(Arena *arena, String8List *list, String8 string);
internal String8Node *str8_list_pushf(Arena *arena, String8List *list, char *fmt, ...);

////////////////////////////////
//~ String Splitting/Joining

internal String8List str8_split(Arena *arena, String8 string, u8 *split_chars, u64 split_char_count,
                                StringSplitFlags flags);
internal String8 str8_list_join(Arena *arena, String8List *list, StringJoin *optional_params);

////////////////////////////////
//~ String Arrays

internal String8Array str8_array_from_list(Arena *arena, String8List *list);
internal String8Array str8_array_reserve(Arena *arena, u64 count);

////////////////////////////////
//~ String Path Helpers

internal String8 str8_chop_last_slash(String8 string);
internal String8 str8_skip_last_slash(String8 string);
internal String8 str8_chop_last_dot(String8 string);
internal String8 str8_skip_last_dot(String8 string);

////////////////////////////////
//~ Basic Text Indentation

internal String8 indented_from_string(Arena *arena, String8 string);

////////////////////////////////
//~ Basic String Hashes

internal u64 u64_hash_from_str8(String8 string);

#endif // STRING_H
