// Third Party Includes
#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_STATIC
#define STB_SPRINTF_DECORATE(name) base_##name
#include "stb_sprintf.h"

// Bit Functions
static u16
bswapu16(u16 x)
{
	return (((x & 0xff00) >> 8) | ((x & 0x00ff) << 8));
}

static u32
bswapu32(u32 x)
{
	return (((x & 0xff000000) >> 24) | ((x & 0x00ff0000) >> 8) | ((x & 0x0000ff00) << 8) | ((x & 0x000000ff) << 24));
}

static u64
bswapu64(u64 x)
{
	return (((x & (u64)0xff00000000000000) >> 56) | ((x & (u64)0x00ff000000000000) >> 40) |
	        ((x & (u64)0x0000ff0000000000) >> 24) | ((x & (u64)0x000000ff00000000) >> 8) |
	        ((x & (u64)0x00000000ff000000) << 8) | ((x & (u64)0x0000000000ff0000) << 24) |
	        ((x & (u64)0x000000000000ff00) << 40) | ((x & (u64)0x00000000000000ff) << 56));
}

// Time Functions
static DenseTime
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

static DateTime
date_time_from_dense_time(DenseTime time)
{
	DateTime result = {0};
	result.msec     = time % 1000;
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
