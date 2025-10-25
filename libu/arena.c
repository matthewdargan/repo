static Arena *
arenaalloc(Arenaparams params)
{
	u64 ressz = params.ressz;
	u64 cmtsz = params.cmtsz;
	if (params.flags & LARGEPAGES)
	{
		ressz = roundup(ressz, sysinfo.lpagesz);
		cmtsz = roundup(cmtsz, sysinfo.lpagesz);
	}
	else
	{
		ressz = roundup(ressz, sysinfo.pagesz);
		cmtsz = roundup(cmtsz, sysinfo.pagesz);
	}
	void *base = (params.flags & LARGEPAGES) ? osreservelarge(ressz) : osreserve(ressz);
	oscommit(base, cmtsz);
	Arena *a = (Arena *)base;
	a->flags = params.flags;
	a->cmtsz = params.cmtsz;
	a->ressz = params.ressz;
	a->basepos = 0;
	a->pos = ARENAHDRSZ;
	a->cmt = cmtsz;
	a->res = ressz;
	return a;
}

static void
arenarelease(Arena *a)
{
	osrelease(a, a->res);
}

static void *
arenapush(Arena *a, u64 size, u64 align)
{
	u64 pre = roundup(a->pos, align);
	u64 post = pre + size;
	if (a->res < post)
	{
		u64 ressz = a->ressz;
		u64 cmtsz = a->cmtsz;
		if (size + ARENAHDRSZ > ressz)
		{
			ressz = roundup(size + ARENAHDRSZ, align);
			cmtsz = roundup(size + ARENAHDRSZ, align);
		}
		Arenaparams params = {
		    .flags = a->flags,
		    .ressz = ressz,
		    .cmtsz = cmtsz,
		};
		Arena *b = arenaalloc(params);
		b->basepos = a->basepos + a->res;
		a = b;
		pre = roundup(a->pos, align);
		post = pre + size;
	}
	if (a->cmt < post)
	{
		u64 alignpos = post + a->cmtsz - 1;
		alignpos -= alignpos % a->cmtsz;
		u64 clamp = min(alignpos, a->res);
		u64 cmtsz = clamp - a->cmt;
		u8 *cmtp = (u8 *)a + a->cmt;
		oscommit(cmtp, cmtsz);
		a->cmt = clamp;
	}
	void *p = NULL;
	if (a->cmt >= post)
	{
		p = (u8 *)a + pre;
		a->pos = post;
	}
	return p;
}

static u64
arenapos(Arena *a)
{
	return a->basepos + a->pos;
}

static void
arenapopto(Arena *a, u64 pos)
{
	u64 safe = max(ARENAHDRSZ, pos);
	if (a->basepos >= safe)
	{
		osrelease(a, a->res);
	}
	a->pos = safe - a->basepos;
}

static void
arenaclear(Arena *a)
{
	arenapopto(a, 0);
}

static void
arenapop(Arena *a, u64 size)
{
	u64 old = arenapos(a);
	u64 dst = (old > size) ? old - size : old;
	arenapopto(a, dst);
}

static Temp
tempbegin(Arena *a)
{
	Temp t = {.a = a, .pos = arenapos(a)};
	return t;
}

static void
tempend(Temp t)
{
	arenapopto(t.a, t.pos);
}
