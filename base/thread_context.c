// Globals
thread_static TCTX *tctx_thread_local = 0;

// Thread Context Functions
static TCTX *
tctx_alloc(void)
{
	Arena *arena = arena_alloc();
	TCTX *tctx = push_array(arena, TCTX, 1);
	tctx->arenas[0] = arena;
	tctx->arenas[1] = arena_alloc();
	return tctx;
}

static void
tctx_release(TCTX *tctx)
{
	arena_release(tctx->arenas[1]);
	arena_release(tctx->arenas[0]);
}

static void
tctx_select(TCTX *tctx)
{
	tctx_thread_local = tctx;
}

static TCTX *
tctx_selected(void)
{
	return tctx_thread_local;
}

static Arena *
tctx_get_scratch(Arena **conflicts, u64 count)
{
	TCTX *tctx = tctx_selected();
	Arena *result = 0;
	Arena **arena_ptr = tctx->arenas;
	for(u64 i = 0; i < ArrayCount(tctx->arenas); i += 1, arena_ptr += 1)
	{
		Arena **conflict_ptr = conflicts;
		b32 has_conflict = 0;
		for(u64 j = 0; j < count; j += 1, conflict_ptr += 1)
		{
			if(*arena_ptr == *conflict_ptr)
			{
				has_conflict = 1;
				break;
			}
		}
		if(!has_conflict)
		{
			result = *arena_ptr;
			break;
		}
	}
	return result;
}
