#ifndef ARENA_H
#define ARENA_H

// Arena Types
#define ARENA_HEADER_SIZE 128
#define ARENA_FREE_LIST 1

typedef u64 ArenaFlags;
enum
{
	ArenaFlag_NoChain    = (1 << 0),
	ArenaFlag_LargePages = (1 << 1),
};

typedef struct ArenaParams ArenaParams;
struct ArenaParams
{
	ArenaFlags flags;
	u64 reserve_size;
	u64 commit_size;
	void *optional_backing_buffer;
};

typedef struct Arena Arena;
struct Arena
{
	Arena *prev;     // previous arena in chain
	Arena *current;  // current arena in chain
	ArenaFlags flags;
	u64 cmt_size;
	u64 res_size;
	u64 base_pos;
	u64 pos;
	u64 cmt;
	u64 res;
#if ARENA_FREE_LIST
	Arena *free_last;
#endif
};
StaticAssert(sizeof(Arena) <= ARENA_HEADER_SIZE, arena_header_size_check);

typedef struct Temp Temp;
struct Temp
{
	Arena *arena;
	u64 pos;
};

// Arena Functions
read_only static ArenaFlags arena_default_flags = 0;
read_only static u64 arena_default_reserve_size = MB(64);
read_only static u64 arena_default_commit_size  = KB(64);

static Arena *arena_alloc(void);
static Arena *arena_alloc_(ArenaParams params);
static void arena_release(Arena *arena);
static void *arena_push(Arena *arena, u64 size, u64 align, b32 zero);
static u64 arena_pos(Arena *arena);
static void arena_pop_to(Arena *arena, u64 pos);
static void arena_clear(Arena *arena);
static void arena_pop(Arena *arena, u64 amt);
static Temp temp_begin(Arena *arena);
static void temp_end(Temp temp);

#define push_array_no_zero_aligned(a, T, c, align) (T *)arena_push((a), sizeof(T) * (c), (align), (0))
#define push_array_aligned(a, T, c, align) (T *)arena_push((a), sizeof(T) * (c), (align), (1))
#define push_array_no_zero(a, T, c) push_array_no_zero_aligned((a), T, (c), Max(8, AlignOf(T)))
#define push_array(a, T, c) push_array_aligned(a, T, c, Max(8, AlignOf(T)))

#endif  // ARENA_H
