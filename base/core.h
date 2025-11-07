#ifndef CORE_H
#define CORE_H

// Foreign Includes
// clang-format off
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
// clang-format on

// Third Party Includes
#define STB_SPRINTF_DECORATE(name) base_##name
#define STB_SPRINTF_STATIC
#include "stb_sprintf.h"

// Codebase Keywords
#define read_only __attribute__((section(".rodata")))
#define thread_static __thread

// Units
#define KB(n) (((u64)(n)) << 10)
#define MB(n) (((u64)(n)) << 20)
#define GB(n) (((u64)(n)) << 30)
#define TB(n) (((u64)(n)) << 40)
#define Thousand(n) ((n) * 1000)
#define Million(n) ((n) * 1000000)
#define Billion(n) ((n) * 1000000000)

// Clamps, Mins, Maxes
#define Min(A, B) (((A) < (B)) ? (A) : (B))
#define Max(A, B) (((A) > (B)) ? (A) : (B))

// Type -> Alignment
#define AlignOf(T) __alignof(T)

// For-Loop Construct Macros
#define DeferLoop(begin, end) for (int _i_ = ((begin), 0); !_i_; _i_ += 1, (end))

// Memory Operation Macros
#define MemoryCopy(dst, src, size) memmove((dst), (src), (size))
#define MemoryCompare(a, b, size) memcmp((a), (b), (size))
#define MemoryCopyStruct(d, s) MemoryCopy((d), (s), sizeof(*(d)))
#define MemoryZero(s, z) memset((s), 0, (z))
#define MemoryMatch(a, b, z) (MemoryCompare((a), (b), (z)) == 0)

// Asserts
#define Trap() __builtin_trap()
#define AssertAlways(x) \
	do                    \
	{                     \
		if (!(x))           \
		{                   \
			Trap();           \
		}                   \
	} while (0)
#if BUILD_DEBUG
#define Assert(x) AssertAlways(x)
#else
#define Assert(x) (void)(x)
#endif
#define StaticAssert(C, ID) static u8 Glue(ID, __LINE__)[(C) ? 1 : -1]

// Linked List Building Macros

// linked list macro helpers
#define CheckNil(nil, p) ((p) == 0 || (p) == nil)
#define SetNil(nil, p) ((p) = nil)

// singly-linked, doubly-headed lists (queues)
#define SLLQueuePush_NZ(nil, f, l, n, next) \
	(CheckNil(nil, f) ? ((f) = (l) = (n), SetNil(nil, (n)->next)) : ((l)->next = (n), (l) = (n), SetNil(nil, (n)->next)))
#define SLLQueuePushFront_NZ(nil, f, l, n, next) \
	(CheckNil(nil, f) ? ((f) = (l) = (n), SetNil(nil, (n)->next)) : ((n)->next = (f), (f) = (n)))
#define SLLQueuePop_NZ(nil, f, l, next) ((f) == (l) ? (SetNil(nil, f), SetNil(nil, l)) : ((f) = (f)->next))

// singly-linked, singly-headed lists (stacks)
#define SLLStackPush_N(f, n, next) ((n)->next = (f), (f) = (n))
#define SLLStackPop_N(f, next) ((f) = (f)->next)

// singly-linked, doubly-headed list helpers
#define SLLQueuePush_N(f, l, n, next) SLLQueuePush_NZ(0, f, l, n, next)
#define SLLQueuePushFront_N(f, l, n, next) SLLQueuePushFront_NZ(0, f, l, n, next)
#define SLLQueuePop_N(f, l, next) SLLQueuePop_NZ(0, f, l, next)
#define SLLQueuePush(f, l, n) SLLQueuePush_NZ(0, f, l, n, next)
#define SLLQueuePushFront(f, l, n) SLLQueuePushFront_NZ(0, f, l, n, next)
#define SLLQueuePop(f, l) SLLQueuePop_NZ(0, f, l, next)

// singly-linked, singly-headed list helpers
#define SLLStackPush(f, n) SLLStackPush_N(f, n, next)
#define SLLStackPop(f) SLLStackPop_N(f, next)

// Address Sanitizer Markup
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#define ASAN_ENABLED 1
#endif
#endif

#if ASAN_ENABLED
void __asan_poison_memory_region(void const volatile *addr, size_t size);
void __asan_unpoison_memory_region(void const volatile *addr, size_t size);
#define AsanPoisonMemoryRegion(addr, size) __asan_poison_memory_region((addr), (size))
#define AsanUnpoisonMemoryRegion(addr, size) __asan_unpoison_memory_region((addr), (size))
#else
#define AsanPoisonMemoryRegion(addr, size) ((void)(addr), (void)(size))
#define AsanUnpoisonMemoryRegion(addr, size) ((void)(addr), (void)(size))
#endif

// Misc. Helper Macros
#define ArrayCount(a) (sizeof(a) / sizeof((a)[0]))
#define AlignPow2(x, b) (((x) + (b) - 1) & (~((b) - 1)))
#define Glue_(A, B) A##B
#define Glue(A, B) Glue_(A, B)
#define Swap(T, a, b) \
	do                  \
	{                   \
		T t__ = a;        \
		a     = b;        \
		b     = t__;      \
	} while (0)

// Base Types
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

// Endianness
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

// Array Types
typedef struct U64Array U64Array;
struct U64Array
{
	u64 count;
	u64 *v;
};

// Basic Constants
static u64 max_u64 = 0xffffffffffffffffull;
static u32 max_u32 = 0xffffffffu;
static u16 max_u16 = 0xffff;
static u8 max_u8   = 0xff;

static s64 max_s64 = 0x7fffffffffffffffll;
static s32 max_s32 = 0x7fffffff;
static s16 max_s16 = 0x7fff;
static s8 max_s8   = 0x7f;

static s64 min_s64 = (s64)0x8000000000000000ull;
static s32 min_s32 = (s32)0x80000000u;
static s16 min_s16 = (s16)0x8000;
static s8 min_s8   = (s8)0x80;

// Time Types
typedef struct DateTime DateTime;
struct DateTime
{
	u16 msec;  // [0,999]
	u16 sec;   // [0,60]
	u16 min;   // [0,59]
	u16 hour;  // [0,24]
	u16 day;   // [0,30]
	u32 mon;
	u32 year;  // 1 = 1 CE, 0 = 1 BC
};

typedef u64 DenseTime;

enum
{
	ISDIR = 1 << 0,
};

// Bit Functions
static u16 bswapu16(u16 x);
static u32 bswapu32(u32 x);
static u64 bswapu64(u64 x);

// Time Functions
static DenseTime dense_time_from_date_time(DateTime dt);
static DateTime date_time_from_dense_time(DenseTime t);

#endif  // CORE_H
