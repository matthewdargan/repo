#ifndef STRING_H
#define STRING_H

typedef struct String8 String8;
struct String8 {
	u8 *str;
	u64 len;
};

typedef struct String8Node String8Node;
struct String8Node {
	String8Node *next;
	String8 str;
};

typedef struct String8List String8List;
struct String8List {
	String8Node *start;
	String8Node *end;
	u64 node_cnt;
	u64 total_len;
};

typedef struct String8Array String8Array;
struct String8Array {
	String8 *v;
	u64 cnt;
};

typedef struct Rng1U64 Rng1U64;
struct Rng1U64 {
	u64 min;
	u64 max;
};

typedef u32 StringCmpFlags;
enum {
	STRING_CMP_FLAGS_CASE_INSENSITIVE = 1 << 0,
	STRING_CMP_FLAGS_RIGHT_SIDE_SLOPPY = 1 << 1,
};

typedef u32 StringSplitFlags;
enum {
	STRING_SPLIT_FLAGS_KEEP_EMPTY = 1 << 0,
};

typedef struct StringJoin StringJoin;
struct StringJoin {
	String8 pre;
	String8 sep;
	String8 post;
};

read_only static u8 int_symbols[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
};

read_only static u8 int_symbol_reverse[128] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

#define str8_lit(s) str8((u8 *)(s), sizeof(s) - 1)
#define str8_lit_comp(s) \
	{                    \
	    (u8 *)(s),       \
	    sizeof(s) - 1,   \
	}
#define str8_cmp_lit(a_lit, b, flags) str8_cmp(str8_lit(a_lit), (b), (flags))
#define str8_cmp_cstr(a_cstr, b, flags) str8_cmp(str8_cstr(a_cstr), (b), (flags))

static b32 char_is_upper(u8 c);
static b32 char_is_lower(u8 c);
static u8 char_to_lower(u8 c);
static u8 char_to_upper(u8 c);
static u64 cstr8_len(u8 *c);
static String8 str8(u8 *str, u64 len);
static String8 str8_rng(u8 *start, u8 *end);
static String8 str8_zero(void);
static String8 str8_cstr(char *c);
static Rng1U64 rng1u64(u64 min, u64 max);
static u64 dim1u64(Rng1U64 r);
static b32 str8_cmp(String8 a, String8 b, StringCmpFlags flags);
static u64 str8_index(String8 s, u64 start_pos, String8 needle, StringCmpFlags flags);
static u64 str8_rindex(String8 s, u64 start_pos, String8 needle, StringCmpFlags flags);
static String8 str8_substr(String8 s, Rng1U64 r);
static String8 str8_prefix(String8 s, u64 len);
static String8 str8_suffix(String8 s, u64 len);
static String8 str8_skip(String8 s, u64 len);
static String8 push_str8_cat(Arena *a, String8 s1, String8 s2);
static String8 push_str8_copy(Arena *a, String8 s);
static String8 push_str8fv(Arena *a, char *fmt, va_list args);
static String8 push_str8f(Arena *a, char *fmt, ...);
static b32 str8_is_int(String8 s, u32 radix);
static u64 str8_to_u64(String8 s, u32 radix);
static b32 str8_to_u64_ok(String8 s, u64 *x);
static String8 u64_to_str8(Arena *a, u64 v, u32 radix, u8 min_digits, u8 digit_sep);
static String8Node *str8_list_push(Arena *a, String8List *list, String8 s);
static String8List str8_split(Arena *a, String8 s, u8 *split, u64 split_len, StringSplitFlags flags);
static String8 str8_list_join(Arena *a, String8List *list, StringJoin *opts);
static String8Array str8_list_to_array(Arena *a, String8List *list);
static String8Array str8_array_reserve(Arena *a, u64 cnt);
static String8 str8_dirname(String8 s);
static String8 str8_basename(String8 s);
static String8 str8_prefix_ext(String8 s);
static String8 str8_ext(String8 s);

#endif  // STRING_H
