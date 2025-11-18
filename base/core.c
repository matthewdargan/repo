////////////////////////////////
//~ Third Party Includes

#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_STATIC
#define STB_SPRINTF_DECORATE(name) base_##name
#include "stb_sprintf.h"

////////////////////////////////
//~ Bit Patterns

internal u16
bswap_u16(u16 x)
{
	u16 result = (((x & 0xff00) >> 8) | ((x & 0x00ff) << 8));
	return result;
}

internal u32
bswap_u32(u32 x)
{
	u32 result =
	    (((x & 0xff000000) >> 24) | ((x & 0x00ff0000) >> 8) | ((x & 0x0000ff00) << 8) | ((x & 0x000000ff) << 24));
	return result;
}

internal u64
bswap_u64(u64 x)
{
	u64 result =
	    (((x & 0xff00000000000000ull) >> 56) | ((x & 0x00ff000000000000ull) >> 40) | ((x & 0x0000ff0000000000ull) >> 24) |
	     ((x & 0x000000ff00000000ull) >> 8) | ((x & 0x00000000ff000000ull) << 8) | ((x & 0x0000000000ff0000ull) << 24) |
	     ((x & 0x000000000000ff00ull) << 40) | ((x & 0x00000000000000ffull) << 56));
	return result;
}

////////////////////////////////
//~ Raw Byte IO

internal u16
read_u16(void const *ptr)
{
	u16 x;
	MemoryCopy(&x, ptr, sizeof(x));
	return x;
}

internal u32
read_u32(void const *ptr)
{
	u32 x;
	MemoryCopy(&x, ptr, sizeof(x));
	return x;
}

internal u64
read_u64(void const *ptr)
{
	u64 x;
	MemoryCopy(&x, ptr, sizeof(x));
	return x;
}

internal void
write_u16(void *ptr, u16 x)
{
	MemoryCopy(ptr, &x, sizeof(x));
}

internal void
write_u32(void *ptr, u32 x)
{
	MemoryCopy(ptr, &x, sizeof(x));
}

internal void
write_u64(void *ptr, u64 x)
{
	MemoryCopy(ptr, &x, sizeof(x));
}

////////////////////////////////
//~ Memory Functions

internal b32
memory_is_zero(void *ptr, u64 size)
{
	b32 result = 1;

	// break down size
	u64 extra = (size & 0x7);
	u64 count8 = (size >> 3);

	// check with 8-byte stride
	u64 *p64 = (u64 *)ptr;
	if(result)
	{
		for(u64 i = 0; i < count8; i += 1, p64 += 1)
		{
			if(*p64 != 0)
			{
				result = 0;
				goto done;
			}
		}
	}

	// check extra
	if(result)
	{
		u8 *p8 = (u8 *)p64;
		for(u64 i = 0; i < extra; i += 1, p8 += 1)
		{
			if(*p8 != 0)
			{
				result = 0;
				goto done;
			}
		}
	}

done:
	return result;
}

////////////////////////////////
//~ Time Functions

internal DenseTime
dense_time_from_date_time(DateTime date_time)
{
	DenseTime result = 0;
	result += date_time.year;
	result *= 12;
	result += date_time.mon;
	result *= 31;
	result += date_time.day;
	result *= 24;
	result += date_time.hour;
	result *= 60;
	result += date_time.min;
	result *= 61;
	result += date_time.sec;
	result *= 1000;
	result += date_time.msec;
	return result;
}

internal DateTime
date_time_from_dense_time(DenseTime time)
{
	DateTime result = {0};
	result.msec = time % 1000;
	time /= 1000;
	result.sec = time % 61;
	time /= 61;
	result.min = time % 60;
	time /= 60;
	result.hour = time % 24;
	time /= 24;
	result.day = time % 31;
	time /= 31;
	result.mon = time % 12;
	time /= 12;
	Assert(time <= max_u32);
	result.year = (u32)time;
	return result;
}
