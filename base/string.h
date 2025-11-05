#ifndef STRING_H
#define STRING_H

// String Types
typedef struct String8 String8;
struct String8
{
	u8 *str;
	u64 size;
};

// String List/Array Types
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

// String Matching/Splitting/Joining Types
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

// String <-> Integer Tables
read_only static u8 integer_symbols[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
};
read_only static u8 integer_symbol_reverse[128] = {
    [0 ... 47] = 0xff, ['0'] = 0x00, ['1'] = 0x01, ['2'] = 0x02, ['3'] = 0x03,         ['4'] = 0x04, ['5'] = 0x05,
    ['6'] = 0x06,      ['7'] = 0x07, ['8'] = 0x08, ['9'] = 0x09, [58 ... 64] = 0xff,   ['A'] = 0x0a, ['B'] = 0x0b,
    ['C'] = 0x0c,      ['D'] = 0x0d, ['E'] = 0x0e, ['F'] = 0x0f, [71 ... 96] = 0xff,   ['a'] = 0x0a, ['b'] = 0x0b,
    ['c'] = 0x0c,      ['d'] = 0x0d, ['e'] = 0x0e, ['f'] = 0x0f, [103 ... 127] = 0xff,
};

// Character Classification/Conversion Functions
static b32 char_is_space(u8 c);
static b32 char_is_upper(u8 c);
static b32 char_is_lower(u8 c);
static b32 char_is_alpha(u8 c);
static b32 char_is_digit(u8 c, u32 base);
static u8 lower_from_char(u8 c);
static u8 upper_from_char(u8 c);
static u64 cstring8_length(u8 *c);

// String Constructors
#define str8_lit(S) str8((u8 *)(S), sizeof(S) - 1)
#define str8_lit_comp(S) \
	{                      \
	    (u8 *)(S),         \
	    sizeof(S) - 1,     \
	}
#define str8_varg(S) (int)((S).size), ((S).str)

static String8 str8(u8 *str, u64 size);
static String8 str8_range(u8 *first, u8 *one_past_last);
static String8 str8_zero(void);
static String8 str8_cstring(char *c);

// String Stylization
static String8 upper_from_str8(Arena *arena, String8 string);
static String8 lower_from_str8(Arena *arena, String8 string);

// String Matching
static b32 str8_match(String8 a, String8 b, StringMatchFlags flags);
static u64 str8_find_needle(String8 string, u64 start_pos, String8 needle, StringMatchFlags flags);
static u64 str8_find_needle_reverse(String8 string, u64 start_pos, String8 needle, StringMatchFlags flags);

// String Slicing
static String8 str8_substr(String8 str, Rng1U64 range);
static String8 str8_prefix(String8 str, u64 size);
static String8 str8_skip(String8 str, u64 amt);
static String8 str8_postfix(String8 str, u64 size);
static String8 str8_chop(String8 str, u64 amt);

// String Formatting/Copying
static String8 str8_cat(Arena *arena, String8 s1, String8 s2);
static String8 str8_copy(Arena *arena, String8 s);
static String8 str8fv(Arena *arena, char *fmt, va_list args);
static String8 str8f(Arena *arena, char *fmt, ...);

// String <-> Integer Conversions
static b32 str8_is_integer(String8 string, u32 radix);
static u64 u64_from_str8(String8 string, u32 radix);
static u32 u32_from_str8(String8 string, u32 radix);
static b32 try_u64_from_str8(String8 string, u64 *x);
static String8 str8_from_u64(Arena *arena, u64 value, u32 radix, u8 min_digits, u8 digit_group_separator);

// String List Construction Functions
static String8Node *str8_list_push(Arena *arena, String8List *list, String8 string);

// String Splitting/Joining
static String8List str8_split(Arena *arena, String8 string, u8 *split_chars, u64 split_char_count,
                              StringSplitFlags flags);
static String8 str8_list_join(Arena *arena, String8List *list, StringJoin *optional_params);

// String Arrays
static String8Array str8_array_from_list(Arena *arena, String8List *list);
static String8Array str8_array_reserve(Arena *arena, u64 count);

// String Path Helpers
static String8 str8_chop_last_slash(String8 string);
static String8 str8_skip_last_slash(String8 string);
static String8 str8_chop_last_dot(String8 string);
static String8 str8_skip_last_dot(String8 string);

// Basic String Hashes
static u64 u64_hash_from_str8(String8 string);

#endif  // STRING_H
