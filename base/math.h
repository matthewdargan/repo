#ifndef MATH_H
#define MATH_H

////////////////////////////////
//~ Scalar Math Operations

#define abs_s64(v) (s64) llabs(v)

#define sqrt_f32(v) sqrtf(v)
#define cbrt_f32(v) cbrtf(v)
#define mod_f32(a, b) fmodf((a), (b))
#define pow_f32(b, e) powf((b), (e))
#define ceil_f32(v) ceilf(v)
#define floor_f32(v) floorf(v)
#define round_f32(v) roundf(v)
#define abs_f32(v) fabsf(v)

#define sqrt_f64(v) sqrt(v)
#define cbrt_f64(v) cbrt(v)
#define mod_f64(a, b) fmod((a), (b))
#define pow_f64(b, e) pow((b), (e))
#define ceil_f64(v) ceil(v)
#define floor_f64(v) floor(v)
#define round_f64(v) round(v)
#define abs_f64(v) fabs(v)

////////////////////////////////
//~ Range Types

typedef struct Rng1U64 Rng1U64;
struct Rng1U64
{
	u64 min;
	u64 max;
};

////////////////////////////////
//~ Range Operations

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
