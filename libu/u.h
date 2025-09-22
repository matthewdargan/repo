#ifndef U_H
#define U_H

#define readonly __attribute__((section(".rodata")))
#define nelem(a) (sizeof(a) / sizeof((a)[0]))
#define roundup(x, n) (((x) + (n) - 1) & ~((n) - 1))

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

#define U8MAX 0xff
#define U16MAX 0xffff
#define U32MAX 0xffffffffu
#define U64MAX 0xffffffffffffffffull
#define S8MAX 0x7f
#define S16MAX 0x7fff
#define S32MAX 0x7fffffff
#define S64MAX 0x7fffffffffffffffll
#define S8MIN (-S8MAX - 1)
#define S16MIN (-S16MAX - 1)
#define S32MIN (-S32MAX - 1)
#define S64MIN (-S64MAX - 1)

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define LITTLEENDIAN 1
#elif defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(_WIN32) || defined(__i386__) || \
    defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
#define LITTLEENDIAN 1
#else
#define LITTLEENDIAN 0
#endif

#if LITTLEENDIAN
#define fromleu16(x) (x)
#define fromleu32(x) (x)
#define fromleu64(x) (x)
#else
#define fromleu16(x) bswapu16((x))
#define fromleu32(x) bswapu32((x))
#define fromleu64(x) bswapu64((x))
#endif

typedef struct U64array U64array;
struct U64array {
	u64 *v;
	u64 cnt;
};

typedef struct Datetime Datetime;
struct Datetime {
	u16 msec; /* [0,999] */
	u16 sec;  /* [0,60] */
	u16 min;  /* [0,59] */
	u16 hour; /* [0,24] */
	u16 day;  /* [0,30] */
	u32 mon;
	u32 year; /* 1 = 1 CE, 0 = 1 BC */
};

enum {
	ISDIR = 1 << 0,
};

typedef struct Fprops Fprops;
struct Fprops {
	u64 size;
	u64 modified;
	u64 created;
	u32 flags;
};

static u16 bswapu16(u16 x);
static u32 bswapu32(u32 x);
static u64 bswapu64(u64 x);
static u64 max(u64 a, u64 b);
static u64 min(u64 a, u64 b);
static u64 datetimetodense(Datetime dt);
static Datetime densetodatetime(u64 t);

#endif /* U_H */
