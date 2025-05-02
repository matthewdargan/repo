#ifndef CORE_H
#define CORE_H

#define read_only __attribute__((section(".rodata")))

#define KB(n) (((u64)(n)) << 10)
#define MB(n) (((u64)(n)) << 20)
#define GB(n) (((u64)(n)) << 30)
#define THOUSAND(n) ((n) * 1000)
#define MILLION(n) ((n) * 1000000)

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define ASSERT_ALWAYS(x)      \
	do {                      \
		if (!(x)) {           \
			__builtin_trap(); \
		}                     \
	} while (0)
#if BUILD_DEBUG
#define ASSERT(x) ASSERT_ALWAYS(x)
#else
#define ASSERT(x) (void)(x)
#endif
#define GLUE_(a, b) a##b
#define GLUE(a, b) GLUE_(a, b)
#define STATIC_ASSERT(c, id) static u8 GLUE(id, __LINE__)[(c) ? 1 : -1]

#define CHECK_NIL(nil, p) ((p) == 0 || (p) == nil)
#define SET_NIL(nil, p) ((p) = nil)

#define SLL_QUEUE_PUSH_NZ(nil, f, l, n, next)                       \
	(CHECK_NIL(nil, f) ? ((f) = (l) = (n), SET_NIL(nil, (n)->next)) \
	                   : ((l)->next = (n), (l) = (n), SET_NIL(nil, (n)->next)))

#define SLL_QUEUE_PUSH(f, l, n) SLL_QUEUE_PUSH_NZ(0, f, l, n, next)

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define SWAP(T, a, b) \
	do {              \
		T t__ = a;    \
		a = b;        \
		b = t__;      \
	} while (0)
#define ALIGN_POW2(x, b) (((x) + (b) - 1) & (~((b) - 1)))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef s8 b8;
typedef s16 b16;
typedef s32 b32;
typedef s64 b64;
typedef float f32;
typedef double f64;

static u64 max_u64 = (u64)0xffffffffffffffffull;
static u32 max_u32 = 0xffffffff;
static u16 max_u16 = 0xffff;
static u8 max_u8 = 0xff;
static s64 max_s64 = (s64)0x7fffffffffffffffull;
static s32 max_s32 = (s32)0x7fffffff;
static s16 max_s16 = (s16)0x7fff;
static s8 max_s8 = (s8)0x7f;
static s64 min_s64 = (s64)0xffffffffffffffffull;
static s32 min_s32 = (s32)0xffffffff;
static s16 min_s16 = (s16)0xffff;
static s8 min_s8 = (s8)0xff;

typedef struct DateTime DateTime;
struct DateTime {
	u16 micro_sec;  // [0,999]
	u16 msec;       // [0,999]
	u16 sec;        // [0,60]
	u16 min;        // [0,59]
	u16 hour;       // [0,24]
	u16 day;        // [0,30]
	u32 mon;
	u32 year;  // 1 = 1 CE, 0 = 1 BC
};

typedef u64 DenseTime;

typedef u32 FilePropertyFlags;
enum {
	FILE_PROPERTY_FLAG_IS_DIR = (1 << 0),
};

typedef struct FileProperties FileProperties;
struct FileProperties {
	u64 size;
	u64 modified;
	u64 created;
	FilePropertyFlags flags;
};

static DenseTime date_time_to_dense_time(DateTime dt);
static DateTime dense_time_to_date_time(DenseTime t);

#endif  // CORE_H
