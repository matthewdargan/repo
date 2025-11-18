#ifndef MATH_H
#define MATH_H

////////////////////////////////
//~ Range Types

typedef struct Rng1U64 Rng1U64;
struct Rng1U64
{
	u64 min;
	u64 max;
};

////////////////////////////////
//~ Range Ops

internal Rng1U64 rng_1u64(u64 min, u64 max);
internal Rng1U64 shift_1u64(Rng1U64 range, u64 x);
internal Rng1U64 pad_1u64(Rng1U64 range, u64 x);
internal u64 center_1u64(Rng1U64 range);
internal b32 contains_1u64(Rng1U64 range, u64 x);
internal u64 dim_1u64(Rng1U64 range);
internal Rng1U64 union_1u64(Rng1U64 a, Rng1U64 b);
internal Rng1U64 intersect_1u64(Rng1U64 a, Rng1U64 b);
internal u64 clamp_1u64(Rng1U64 range, u64 v);

#endif // MATH_H
