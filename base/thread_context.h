#ifndef THREAD_CONTEXT_H
#define THREAD_CONTEXT_H

// Base Per-Thread State Bundle
typedef struct TCTX TCTX;
struct TCTX
{
	Arena *arenas[2];
};

// Thread Context Functions
static TCTX *tctx_alloc(void);
static void tctx_release(TCTX *tctx);
static void tctx_select(TCTX *tctx);
static TCTX *tctx_selected(void);

static Arena *tctx_get_scratch(Arena **conflicts, u64 count);
#define scratch_begin(conflicts, count) temp_begin(tctx_get_scratch((conflicts), (count)))
#define scratch_end(scratch) temp_end(scratch)

#endif  // THREAD_CONTEXT_H
