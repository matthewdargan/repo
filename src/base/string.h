#ifndef STRING_H
#define STRING_H

typedef struct string8 string8;
struct string8 {
    u8 *str;
    u64 size;
};

typedef struct string8node string8node;
struct string8node {
    string8node *next;
    string8 string;
};

typedef struct string8list string8list;
struct string8list {
    string8node *first;
    string8node *last;
    u64 node_count;
    u64 total_size;
};

typedef struct string8array string8array;
struct string8array {
    string8 *v;
    u64 count;
};

typedef struct rng1u64 rng1u64;
struct rng1u64 {
    u64 min;
    u64 max;
};

typedef u32 string_match_flags;
enum {
    STRING_MATCH_FLAGS_CASE_INSENSITIVE = 1 << 0,
    STRING_MATCH_FLAGS_RIGHT_SIDE_SLOPPY = 1 << 1,
};

typedef u32 string_split_flags;
enum {
    STRING_SPLIT_FLAGS_KEEP_EMPTY = 1 << 0,
};

typedef struct string_join string_join;
struct string_join {
    string8 pre;
    string8 sep;
    string8 post;
};

read_only global u8 integer_symbols[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
};

read_only global u8 integer_symbol_reverse[128] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

internal b32 char_is_upper(u8 c);
internal b32 char_is_lower(u8 c);
internal u8 char_to_lower(u8 c);
internal u8 char_to_upper(u8 c);
internal u64 cstring8_length(u8 *c);

#define str8_lit(s) str8((u8 *)(s), sizeof(s) - 1)
internal string8 str8(u8 *str, u64 size);
internal string8 str8_range(u8 *first, u8 *one_past_last);
internal string8 str8_zero(void);
internal string8 str8_cstring(char *c);
internal rng1u64 rng_1u64(u64 min, u64 max);
internal u64 dim_1u64(rng1u64 r);

#define str8_match_lit(a_lit, b, flags) str8_match(str8_lit(a_lit), (b), (flags))
#define str8_match_cstr(a_cstr, b, flags) str8_match(str8_cstring(a_cstr), (b), (flags))
internal b32 str8_match(string8 a, string8 b, string_match_flags flags);
internal u64 str8_find_needle(string8 string, u64 start_pos, string8 needle, string_match_flags flags);
internal u64 str8_find_needle_reverse(string8 string, u64 start_pos, string8 needle, string_match_flags flags);

internal string8 str8_substr(string8 str, rng1u64 range);
internal string8 str8_prefix(string8 str, u64 size);
internal string8 str8_skip(string8 str, u64 amt);
internal string8 str8_postfix(string8 str, u64 size);

internal string8 push_str8_cat(arena *a, string8 s1, string8 s2);
internal string8 push_str8_copy(arena *a, string8 s);
internal string8 push_str8fv(arena *a, char *fmt, va_list args);
internal string8 push_str8f(arena *a, char *fmt, ...);

internal b32 str8_is_integer(string8 string, u32 radix);
internal u64 u64_from_str8(string8 string, u32 radix);
internal b32 try_u64_from_str8_c_rules(string8 string, u64 *x);
internal string8 str8_from_u64(arena *a, u64 value, u32 radix, u8 min_digits, u8 digit_group_separator);

internal string8node *str8_list_push(arena *a, string8list *list, string8 string);

internal string8list str8_split(arena *a, string8 string, u8 *split_chars, u64 split_char_count,
                                string_split_flags flags);
internal string8 str8_list_join(arena *a, string8list *list, string_join *optional_params);

internal string8array str8_array_from_list(arena *a, string8list *list);
internal string8array str8_array_reserve(arena *a, u64 count);

#endif  // STRING_H
