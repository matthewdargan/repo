#ifndef JSON_H
#define JSON_H

enum
{
	JSON_NULL,
	JSON_BOOL,
	JSON_NUMBER,
	JSON_STRING,
	JSON_ARRAY,
	JSON_OBJECT,
};

typedef struct Jsonvalue Jsonvalue;
struct Jsonvalue
{
	u32 type;
	b32 bool;
	f64 number;
	String8 string;
	Jsonvalue *arrvals;
	u64 arrcnt;
	String8 *objkeys;
	Jsonvalue *objvals;
	u64 objcnt;
};

typedef struct Jsonbuilder Jsonbuilder;
struct Jsonbuilder
{
	u8 *data;
	u64 pos;
	u64 cap;
};

static Jsonvalue jsonparse(Arena *a, String8 text);
static Jsonvalue jsonget(Jsonvalue obj, String8 key);
static Jsonvalue jsonindex(Jsonvalue arr, u64 idx);
static Jsonbuilder jsonbuilder(Arena *a, u64 estsize);
static void jsonbwrite(Jsonbuilder *b, String8 s);
static void jsonbwritec(Jsonbuilder *b, u8 c);
static void jsonbwritestr(Jsonbuilder *b, String8 s);
static void jsonbwritenum(Jsonbuilder *b, f64 n);
static void jsonbwritebool(Jsonbuilder *b, b32 val);
static void jsonbwritenull(Jsonbuilder *b);
static void jsonbobjstart(Jsonbuilder *b);
static void jsonbobjend(Jsonbuilder *b);
static void jsonbarrstart(Jsonbuilder *b);
static void jsonbarrend(Jsonbuilder *b);
static void jsonbobjkey(Jsonbuilder *b, String8 key);
static void jsonbarrcomma(Jsonbuilder *b);
static void jsonbobjcomma(Jsonbuilder *b);
static String8 jsonbfinish(Jsonbuilder *b);
static Jsonvalue jsonparsevalue(Arena *a, String8 text, u64 *pos);
static void jsonskipwhitespace(String8 text, u64 *pos);
static Jsonvalue jsonparsenull(String8 text, u64 *pos);
static Jsonvalue jsonparsebool(String8 text, u64 *pos);
static Jsonvalue jsonparsestring(Arena *a, String8 text, u64 *pos);
static Jsonvalue jsonparsenumber(Arena *a, String8 text, u64 *pos);
static Jsonvalue jsonparsearray(Arena *a, String8 text, u64 *pos);
static Jsonvalue jsonparseobject(Arena *a, String8 text, u64 *pos);

#endif  // JSON_H
