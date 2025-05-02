#ifndef ARENA_H
#define ARENA_H

#define ARENA_HEADER_SIZE 128

typedef u32 ArenaFlags;
enum {
	ARENA_FLAGS_LARGE_PAGES = (1 << 0),
};

typedef struct ArenaParams ArenaParams;
struct ArenaParams {
	ArenaFlags flags;
	u64 res_size;
	u64 cmt_size;
};

typedef struct Arena Arena;
struct Arena {
	ArenaFlags flags;
	u64 res_size;
	u64 cmt_size;
	u64 base_pos;
	u64 pos;
	u64 res;
	u64 cmt;
};
STATIC_ASSERT(sizeof(Arena) <= ARENA_HEADER_SIZE, arena_header_size_check);

typedef struct Temp Temp;
struct Temp {
	Arena *a;
	u64 pos;
};

static ArenaFlags arena_default_flags = 0;
static u64 arena_default_res_size = MB(64);
static u64 arena_default_cmt_size = KB(64);

#define push_array_no_zero_aligned(a, T, c, align) (T *)arena_push((a), sizeof(T) * (c), (align))
#define push_array_aligned(a, T, c, align) (T *)memset(push_array_no_zero_aligned(a, T, c, align), 0, sizeof(T) * (c))
#define push_array_no_zero(a, T, c) push_array_no_zero_aligned(a, T, c, MAX(8, __alignof(T)))
#define push_array(a, T, c) push_array_aligned(a, T, c, MAX(8, __alignof(T)))

static Arena *arena_alloc(ArenaParams params);
static void arena_release(Arena *a);
static void *arena_push(Arena *a, u64 size, u64 align);
static u64 arena_pos(Arena *a);
static void arena_pop_to(Arena *a, u64 pos);
static void arena_clear(Arena *a);
static void arena_pop(Arena *a, u64 size);
static Temp temp_begin(Arena *a);
static void temp_end(Temp t);

#endif  // ARENA_H
