internal arena *arena_alloc(arena_params params) {
    u64 reserve_size = params.reserve_size;
    u64 commit_size = params.commit_size;
    if (params.flags & ARENA_FLAGS_LARGE_PAGES) {
        reserve_size = ALIGN_POW2(reserve_size, os_info.large_page_size);
        commit_size = ALIGN_POW2(commit_size, os_info.large_page_size);
    } else {
        reserve_size = ALIGN_POW2(reserve_size, os_info.page_size);
        commit_size = ALIGN_POW2(commit_size, os_info.page_size);
    }
    void *base = 0;
    if (params.flags & ARENA_FLAGS_LARGE_PAGES) {
        base = os_reserve_large(reserve_size);
    } else {
        base = os_reserve(reserve_size);
    }
    os_commit(base, commit_size);
    arena *a = (arena *)base;
    a->flags = params.flags;
    a->cmt_size = params.commit_size;
    a->res_size = params.reserve_size;
    a->base_pos = 0;
    a->pos = ARENA_HEADER_SIZE;
    a->cmt = commit_size;
    a->res = reserve_size;
    return a;
}

internal void arena_release(arena *a) {
    os_release(a, a->res);
}

internal void *arena_push(arena *a, u64 size, u64 align) {
    u64 pos_pre = ALIGN_POW2(a->pos, align);
    u64 pos_pst = pos_pre + size;
    if (a->res < pos_pst) {
        u64 res_size = a->res_size;
        u64 cmt_size = a->cmt_size;
        if (size + ARENA_HEADER_SIZE > res_size) {
            res_size = ALIGN_POW2(size + ARENA_HEADER_SIZE, align);
            cmt_size = ALIGN_POW2(size + ARENA_HEADER_SIZE, align);
        }
        arena_params params = {.flags = a->flags, .reserve_size = res_size, .commit_size = cmt_size};
        arena *new_block = arena_alloc(params);
        new_block->base_pos = a->base_pos + a->res;
        a = new_block;
        pos_pre = ALIGN_POW2(a->pos, align);
        pos_pst = pos_pre + size;
    }
    if (a->cmt < pos_pst) {
        u64 cmt_pst_aligned = pos_pst + a->cmt_size - 1;
        cmt_pst_aligned -= cmt_pst_aligned % a->cmt_size;
        u64 cmt_pst_clamped = MIN(cmt_pst_aligned, a->res);
        u64 cmt_size = cmt_pst_clamped - a->cmt;
        u8 *cmt_ptr = (u8 *)a + a->cmt;
        os_commit(cmt_ptr, cmt_size);
        a->cmt = cmt_pst_clamped;
    }
    void *result = 0;
    if (a->cmt >= pos_pst) {
        result = (u8 *)a + pos_pre;
        a->pos = pos_pst;
    }
    return result;
}

internal u64 arena_pos(arena *a) {
    return a->base_pos + a->pos;
}

internal void arena_pop_to(arena *a, u64 pos) {
    u64 big_pos = MAX(ARENA_HEADER_SIZE, pos);
    if (a->base_pos >= big_pos) {
        os_release(a, a->res);
    }
    u64 new_pos = big_pos - a->base_pos;
    ASSERT_ALWAYS(new_pos <= a->pos);
    a->pos = new_pos;
}

internal void arena_clear(arena *a) {
    arena_pop_to(a, 0);
}

internal void arena_pop(arena *a, u64 amt) {
    u64 pos_old = arena_pos(a);
    u64 pos_new = pos_old;
    if (amt < pos_old) {
        pos_new = pos_old - amt;
    }
    arena_pop_to(a, pos_new);
}

internal temp temp_begin(arena *a) {
    return (temp){.a = a, .pos = arena_pos(a)};
}

internal void temp_end(temp t) {
    arena_pop_to(t.a, t.pos);
}
