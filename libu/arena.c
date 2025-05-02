static Arena *
arena_alloc(ArenaParams params)
{
	u64 res_size = params.res_size;
	u64 cmt_size = params.cmt_size;
	if (params.flags & ARENA_FLAGS_LARGE_PAGES) {
		res_size = ALIGN_POW2(res_size, sys_info.large_page_size);
		cmt_size = ALIGN_POW2(cmt_size, sys_info.large_page_size);
	} else {
		res_size = ALIGN_POW2(res_size, sys_info.page_size);
		cmt_size = ALIGN_POW2(cmt_size, sys_info.page_size);
	}
	void *base = 0;
	if (params.flags & ARENA_FLAGS_LARGE_PAGES) {
		base = os_reserve_large(res_size);
	} else {
		base = os_reserve(res_size);
	}
	os_commit(base, cmt_size);
	Arena *a = (Arena *)base;
	a->flags = params.flags;
	a->cmt_size = params.cmt_size;
	a->res_size = params.res_size;
	a->base_pos = 0;
	a->pos = ARENA_HEADER_SIZE;
	a->cmt = cmt_size;
	a->res = res_size;
	return a;
}

static void
arena_release(Arena *a)
{
	os_release(a, a->res);
}

static void *
arena_push(Arena *a, u64 size, u64 align)
{
	u64 pre = ALIGN_POW2(a->pos, align);
	u64 post = pre + size;
	if (a->res < post) {
		u64 res_size = a->res_size;
		u64 cmt_size = a->cmt_size;
		if (size + ARENA_HEADER_SIZE > res_size) {
			res_size = ALIGN_POW2(size + ARENA_HEADER_SIZE, align);
			cmt_size = ALIGN_POW2(size + ARENA_HEADER_SIZE, align);
		}
		ArenaParams params = {.flags = a->flags, .res_size = res_size, .cmt_size = cmt_size};
		Arena *b = arena_alloc(params);
		b->base_pos = a->base_pos + a->res;
		a = b;
		pre = ALIGN_POW2(a->pos, align);
		post = pre + size;
	}
	if (a->cmt < post) {
		u64 aligned = post + a->cmt_size - 1;
		aligned -= aligned % a->cmt_size;
		u64 clamped = MIN(aligned, a->res);
		u64 cmt_size = clamped - a->cmt;
		u8 *cmt_p = (u8 *)a + a->cmt;
		os_commit(cmt_p, cmt_size);
		a->cmt = clamped;
	}
	void *p = 0;
	if (a->cmt >= post) {
		p = (u8 *)a + pre;
		a->pos = post;
	}
	return p;
}

static u64
arena_pos(Arena *a)
{
	return a->base_pos + a->pos;
}

static void
arena_pop_to(Arena *a, u64 pos)
{
	u64 safe = MAX(ARENA_HEADER_SIZE, pos);
	if (a->base_pos >= safe) {
		os_release(a, a->res);
	}
	u64 new_pos = safe - a->base_pos;
	ASSERT_ALWAYS(new_pos <= a->pos);
	a->pos = new_pos;
}

static void
arena_clear(Arena *a)
{
	arena_pop_to(a, 0);
}

static void
arena_pop(Arena *a, u64 size)
{
	u64 old = arena_pos(a);
	u64 dst = old;
	if (size < old) {
		dst = old - size;
	}
	arena_pop_to(a, dst);
}

static Temp
temp_begin(Arena *a)
{
	return (Temp){.a = a, .pos = arena_pos(a)};
}

static void
temp_end(Temp t)
{
	arena_pop_to(t.a, t.pos);
}
