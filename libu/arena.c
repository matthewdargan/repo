static Arena *
arenaalloc(Arenaparams params)
{
	u64 ressz, cmtsz;
	void *base;
	Arena *a;

	ressz = params.ressz;
	cmtsz = params.cmtsz;
	if (params.flags & LARGEPAGES) {
		ressz = roundup(ressz, sysinfo.lpagesz);
		cmtsz = roundup(cmtsz, sysinfo.lpagesz);
	} else {
		ressz = roundup(ressz, sysinfo.pagesz);
		cmtsz = roundup(cmtsz, sysinfo.pagesz);
	}
	if (params.flags & LARGEPAGES)
		base = osreservelarge(ressz);
	else
		base = osreserve(ressz);
	oscommit(base, cmtsz);
	a = (Arena *)base;
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
	u64 pre, post, ressz, cmtsz, alignpos, clamp;
	Arenaparams params;
	Arena *b;
	u8 *cmtp;
	void *p;

	pre = roundup(a->pos, align);
	post = pre + size;
	if (a->res < post) {
		ressz = a->ressz;
		cmtsz = a->cmtsz;
		if (size + ARENAHDRSZ > ressz) {
			ressz = roundup(size + ARENAHDRSZ, align);
			cmtsz = roundup(size + ARENAHDRSZ, align);
		}
		params.flags = a->flags;
		params.ressz = ressz;
		params.cmtsz = cmtsz;
		b = arenaalloc(params);
		b->basepos = a->basepos + a->res;
		a = b;
		pre = roundup(a->pos, align);
		post = pre + size;
	}
	if (a->cmt < post) {
		alignpos = post + a->cmtsz - 1;
		alignpos -= alignpos % a->cmtsz;
		clamp = min(alignpos, a->res);
		cmtsz = clamp - a->cmt;
		cmtp = (u8 *)a + a->cmt;
		oscommit(cmtp, cmtsz);
		a->cmt = clamp;
	}
	p = NULL;
	if (a->cmt >= post) {
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
	u64 safe, npos;

	safe = max(ARENAHDRSZ, pos);
	if (a->basepos >= safe)
		osrelease(a, a->res);
	npos = safe - a->basepos;
	a->pos = npos;
}

static void
arenaclear(Arena *a)
{
	arenapopto(a, 0);
}

static void
arenapop(Arena *a, u64 size)
{
	u64 old, dst;

	old = arenapos(a);
	dst = old;
	if (size < old)
		dst = old - size;
	arenapopto(a, dst);
}

static Temp
tempbegin(Arena *a)
{
	Temp t;

	t.a = a;
	t.pos = arenapos(a);
	return t;
}

static void
tempend(Temp t)
{
	arenapopto(t.a, t.pos);
}
