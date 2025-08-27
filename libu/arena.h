#ifndef ARENA_H
#define ARENA_H

enum {
	ARENAHDRSZ = 128,
	LARGEPAGES = 1 << 0,
};

typedef struct Arenaparams Arenaparams;
struct Arenaparams {
	u32 flags;
	u64 ressz;
	u64 cmtsz;
};

typedef struct Arena Arena;
struct Arena {
	u32 flags;
	u64 ressz;
	u64 cmtsz;
	u64 basepos;
	u64 pos;
	u64 res;
	u64 cmt;
};

typedef struct Temp Temp;
struct Temp {
	Arena *a;
	u64 pos;
};

readonly static u32 arenaflags = 0;
readonly static u64 arenaressz = 0x4000000;
readonly static u64 arenacmtsz = 0x10000;

#define pusharrnozalign(a, T, c, align) (T *)arenapush((a), sizeof(T) * (c), (align))
#define pusharralign(a, T, c, align) (T *)memset(pusharrnozalign(a, T, c, align), 0, sizeof(T) * (c))
#define pusharrnoz(a, T, c) pusharrnozalign(a, T, c, max(8, __alignof(T)))
#define pusharr(a, T, c) pusharralign(a, T, c, max(8, __alignof(T)))

static Arena *arenaalloc(Arenaparams params);
static void arenarelease(Arena *a);
static void *arenapush(Arena *a, u64 size, u64 align);
static u64 arenapos(Arena *a);
static void arenapopto(Arena *a, u64 pos);
static void arenaclear(Arena *a);
static void arenapop(Arena *a, u64 size);
static Temp tempbegin(Arena *a);
static void tempend(Temp t);

#endif /* ARENA_H */
