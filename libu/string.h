#ifndef STRING_H
#define STRING_H

typedef struct String8 String8;
struct String8 {
	u8 *str;
	u64 len;
};

typedef struct String8node String8node;
struct String8node {
	String8node *next;
	String8 str;
};

typedef struct String8list String8list;
struct String8list {
	String8node *start;
	String8node *end;
	u64 nnode;
	u64 tlen;
};

typedef struct String8array String8array;
struct String8array {
	String8 *v;
	u64 cnt;
};

typedef struct Rng1u64 Rng1u64;
struct Rng1u64 {
	u64 min;
	u64 max;
};

enum {
	CASEINSENSITIVE = 1 << 0,
	RSIDETOL = 1 << 1,
	SPLITKEEPEMPTY = 1 << 0,
};

typedef struct Stringjoin Stringjoin;
struct Stringjoin {
	String8 pre;
	String8 sep;
	String8 post;
};

readonly static u8 hexdigits[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
};
readonly static u8 hexdigitvals[128] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

#define str8lit(s) str8((u8 *)(s), sizeof(s) - 1)
#define str8litc(s)    \
	{                  \
	    (u8 *)(s),     \
	    sizeof(s) - 1, \
	}

static b32 charislower(u8 c);
static b32 charisupper(u8 c);
static u8 chartolower(u8 c);
static u8 chartoupper(u8 c);
static u64 cstrlen(u8 *c);
static String8 str8(u8 *str, u64 len);
static String8 str8rng(u8 *start, u8 *end);
static String8 str8zero(void);
static String8 str8cstr(char *c);
static Rng1u64 rng1u64(u64 min, u64 max);
static u64 dim1u64(Rng1u64 r);
static b32 str8cmp(String8 a, String8 b, u32 flags);
static u64 str8index(String8 s, u64 pos, String8 needle, u32 flags);
static u64 str8rindex(String8 s, u64 pos, String8 needle, u32 flags);
static String8 str8substr(String8 s, Rng1u64 r);
static String8 str8prefix(String8 s, u64 len);
static String8 str8suffix(String8 s, u64 len);
static String8 str8skip(String8 s, u64 len);
static String8 pushstr8cat(Arena *a, String8 s1, String8 s2);
static String8 pushstr8cpy(Arena *a, String8 s);
static String8 pushstr8fv(Arena *a, char *fmt, va_list args);
static String8 pushstr8f(Arena *a, char *fmt, ...);
static b32 str8isint(String8 s, u32 radix);
static u64 str8tou64(String8 s, u32 radix);
static b32 str8tou64ok(String8 s, u64 *x);
static String8 u64tostr8(Arena *a, u64 v, u32 radix, u8 mindig, u8 sep);
static String8node *str8listpush(Arena *a, String8list *list, String8 s);
static String8list str8split(Arena *a, String8 s, u8 *split, u64 splen, u32 flags);
static String8 str8listjoin(Arena *a, String8list *list, Stringjoin *opts);
static String8array str8listtoarray(Arena *a, String8list *list);
static String8array str8arrayreserve(Arena *a, u64 cnt);
static String8 str8dirname(String8 s);
static String8 str8basename(String8 s);
static String8 str8prefixext(String8 s);
static String8 str8ext(String8 s);

#endif /* STRING_H */
