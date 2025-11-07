#ifndef MATH_H
#define MATH_H

// Range Types
typedef struct Rng1U64 Rng1U64;
struct Rng1U64
{
	u64 min;
	u64 max;
};

// Range Ops
static Rng1U64 rng_1u64(u64 min, u64 max);
static u64 dim_1u64(Rng1U64 range);

#endif // MATH_H
