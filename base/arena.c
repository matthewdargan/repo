////////////////////////////////
//~ Arena Functions

internal Arena *
arena_alloc(void)
{
  ArenaParams params  = {0};
  params.flags        = arena_default_flags;
  params.reserve_size = arena_default_reserve_size;
  params.commit_size  = arena_default_commit_size;
  return arena_alloc_(params);
}

internal Arena *
arena_alloc_(ArenaParams params)
{
  u64 reserve_size = params.reserve_size;
  u64 commit_size  = params.commit_size;
  if(params.flags & ArenaFlag_LargePages)
  {
    reserve_size = AlignPow2(reserve_size, os_get_system_info()->large_page_size);
    commit_size  = AlignPow2(commit_size, os_get_system_info()->large_page_size);
  }
  else
  {
    reserve_size = AlignPow2(reserve_size, os_get_system_info()->page_size);
    commit_size  = AlignPow2(commit_size, os_get_system_info()->page_size);
  }

  void *base = params.optional_backing_buffer;
  if(base == 0)
  {
    if(params.flags & ArenaFlag_LargePages)
    {
      base = os_reserve_large(reserve_size);
      os_commit(base, commit_size);
    }
    else
    {
      base = os_reserve(reserve_size);
      os_commit(base, commit_size);
    }
  }

  Arena *arena     = (Arena *)base;
  arena->prev      = 0;
  arena->current   = arena;
  arena->flags     = params.flags;
  arena->cmt_size  = params.commit_size;
  arena->res_size  = params.reserve_size;
  arena->base_pos  = 0;
  arena->pos       = ARENA_HEADER_SIZE;
  arena->cmt       = commit_size;
  arena->res       = reserve_size;
#if ARENA_FREE_LIST
  arena->free_last = 0;
#endif
  AsanPoisonMemoryRegion(base, commit_size);
  AsanUnpoisonMemoryRegion(base, ARENA_HEADER_SIZE);
  return arena;
}

internal void
arena_release(Arena *arena)
{
  for(Arena *n = arena->current, *prev = 0; n != 0; n = prev)
  {
    prev = n->prev;
    os_release(n, n->res);
  }
}

internal void *
arena_push(Arena *arena, u64 size, u64 align, b32 zero)
{
  Arena *current = arena->current;
  u64 pos_pre    = AlignPow2(current->pos, align);
  u64 pos_pst    = pos_pre + size;

  if(current->res < pos_pst && !(arena->flags & ArenaFlag_NoChain))
  {
    Arena *new_block = 0;

#if ARENA_FREE_LIST
    {
      Arena *prev_block;
      for(new_block = arena->free_last, prev_block = 0; new_block != 0;
          prev_block = new_block, new_block = new_block->prev)
      {
        if(new_block->res >= AlignPow2(new_block->pos, align) + size)
        {
          if(prev_block) { prev_block->prev = new_block->prev; }
          else           { arena->free_last = new_block->prev; }
          break;
        }
      }
    }
#endif

    if(new_block == 0)
    {
      u64 res_size = current->res_size;
      u64 cmt_size = current->cmt_size;
      if(size + ARENA_HEADER_SIZE > res_size)
      {
        res_size = AlignPow2(size + ARENA_HEADER_SIZE, align);
        cmt_size = AlignPow2(size + ARENA_HEADER_SIZE, align);
      }
      ArenaParams params  = {0};
      params.flags        = current->flags;
      params.reserve_size = res_size;
      params.commit_size  = cmt_size;
      new_block           = arena_alloc_(params);
    }

    new_block->base_pos = current->base_pos + current->res;
    SLLStackPush_N(arena->current, new_block, prev);

    current = new_block;
    pos_pre = AlignPow2(current->pos, align);
    pos_pst = pos_pre + size;
  }

  u64 size_to_zero = 0;
  if(zero) { size_to_zero = Min(current->cmt, pos_pst) - pos_pre; }

  if(current->cmt < pos_pst)
  {
    u64 cmt_pst_aligned = pos_pst + current->cmt_size - 1;
    cmt_pst_aligned    -= cmt_pst_aligned % current->cmt_size;
    u64 cmt_pst_clamped = Min(cmt_pst_aligned, current->res);
    u64 cmt_size        = cmt_pst_clamped - current->cmt;
    u8 *cmt_ptr         = (u8 *)current + current->cmt;
    os_commit(cmt_ptr, cmt_size);
    current->cmt = cmt_pst_clamped;
  }

  void *result = 0;
  if(current->cmt >= pos_pst)
  {
    result       = (u8 *)current + pos_pre;
    current->pos = pos_pst;
    AsanUnpoisonMemoryRegion(result, size);
    if(size_to_zero != 0) { MemoryZero(result, size_to_zero); }
  }
  return result;
}

internal u64
arena_pos(Arena *arena)
{
  Arena *current = arena->current;
  u64 pos        = current->base_pos + current->pos;
  return pos;
}

internal void
arena_pop_to(Arena *arena, u64 pos)
{
  u64 big_pos    = Max(ARENA_HEADER_SIZE, pos);
  Arena *current = arena->current;

#if ARENA_FREE_LIST
  for(Arena *prev = 0; current->base_pos >= big_pos; current = prev)
  {
    prev         = current->prev;
    current->pos = ARENA_HEADER_SIZE;
    SLLStackPush_N(arena->free_last, current, prev);
    AsanPoisonMemoryRegion((u8 *)current + ARENA_HEADER_SIZE, current->res - ARENA_HEADER_SIZE);
  }
#else
  for(Arena *prev = 0; current->base_pos >= big_pos; current = prev)
  {
    prev = current->prev;
    os_release(current, current->res);
  }
#endif

  arena->current = current;
  u64 new_pos    = big_pos - current->base_pos;
  AssertAlways(new_pos <= current->pos);
  AsanPoisonMemoryRegion((u8 *)current + new_pos, (current->pos - new_pos));
  current->pos   = new_pos;
}

internal void arena_clear(Arena *arena) { arena_pop_to(arena, 0); }

internal void
arena_pop(Arena *arena, u64 amt)
{
  u64 pos_old                 = arena_pos(arena);
  u64 pos_new                 = pos_old;
  if(amt < pos_old) { pos_new = pos_old - amt; }
  arena_pop_to(arena, pos_new);
}

internal Temp
temp_begin(Arena *arena)
{
  u64 pos   = arena_pos(arena);
  Temp temp = {arena, pos};
  return temp;
}

internal void temp_end(Temp temp) { arena_pop_to(temp.arena, temp.pos); }
