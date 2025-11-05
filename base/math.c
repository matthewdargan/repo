// Range Ops
static Rng1U64
rng_1u64(u64 min, u64 max)
{
	Rng1U64 r = {min, max};
	if (r.min > r.max)
	{
		Swap(u64, r.min, r.max);
	}
	return r;
}

static u64
dim_1u64(Rng1U64 r)
{
	u64 c = ((r.max > r.min) ? (r.max - r.min) : 0);
	return c;
}
