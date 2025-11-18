////////////////////////////////
//~ Range Ops

internal Rng1U64
rng_1u64(u64 min, u64 max)
{
	Rng1U64 r = {min, max};
	if(r.min > r.max)
	{
		Swap(u64, r.min, r.max);
	}
	return r;
}

internal Rng1U64
shift_1u64(Rng1U64 r, u64 x)
{
	r.min += x;
	r.max += x;
	return r;
}

internal Rng1U64
pad_1u64(Rng1U64 r, u64 x)
{
	r.min -= x;
	r.max += x;
	return r;
}

internal u64
center_1u64(Rng1U64 r)
{
	u64 c = (r.min + r.max) / 2;
	return c;
}

internal b32
contains_1u64(Rng1U64 r, u64 x)
{
	b32 c = (r.min <= x && x < r.max);
	return c;
}

internal u64
dim_1u64(Rng1U64 r)
{
	u64 c = ((r.max > r.min) ? (r.max - r.min) : 0);
	return c;
}

internal Rng1U64
union_1u64(Rng1U64 a, Rng1U64 b)
{
	Rng1U64 c = {Min(a.min, b.min), Max(a.max, b.max)};
	return c;
}

internal Rng1U64
intersect_1u64(Rng1U64 a, Rng1U64 b)
{
	Rng1U64 c = {Max(a.min, b.min), Min(a.max, b.max)};
	return c;
}

internal u64
clamp_1u64(Rng1U64 r, u64 v)
{
	v = Clamp(r.min, v, r.max);
	return v;
}
