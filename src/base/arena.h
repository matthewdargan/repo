#ifndef ARENA_H
#define ARENA_H

#define ARENA_HEADER_SIZE 128

typedef u64 arena_flags;
enum {
	ARENA_FLAGS_LARGE_PAGES = (1 << 0),
};

typedef struct arena_params arena_params;
struct arena_params {
	arena_flags flags;
	u64 reserve_size;
	u64 commit_size;
};

typedef struct arena arena;
struct arena {
	arena_flags flags;
	u64 cmt_size;
	u64 res_size;
	u64 base_pos;
	u64 pos;
	u64 cmt;
	u64 res;
};
STATIC_ASSERT(sizeof(arena) <= ARENA_HEADER_SIZE, arena_header_size_check);

typedef struct temp temp;
struct temp {
	arena *a;
	u64 pos;
};

global u64 arena_default_reserve_size = MB(64);
global u64 arena_default_commit_size = KB(64);
global arena_flags arena_default_flags = 0;

internal arena *arena_alloc(arena_params params);
internal void arena_release(arena *a);
internal void *arena_push(arena *a, u64 size, u64 align);
internal u64 arena_pos(arena *a);
internal void arena_pop_to(arena *a, u64 pos);
internal void arena_clear(arena *a);
internal void arena_pop(arena *a, u64 amt);
internal temp temp_begin(arena *a);
internal void temp_end(temp t);

#define push_array_no_zero_aligned(a, T, c, align) (T *)arena_push((a), sizeof(T) * (c), (align))
#define push_array_aligned(a, T, c, align) (T *)MEMORY_ZERO(push_array_no_zero_aligned(a, T, c, align), sizeof(T) * (c))
#define push_array_no_zero(a, T, c) push_array_no_zero_aligned(a, T, c, MAX(8, __alignof(T)))
#define push_array(a, T, c) push_array_aligned(a, T, c, MAX(8, __alignof(T)))

#endif  // ARENA_H
