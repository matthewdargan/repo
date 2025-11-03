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
dense_time_from_date_time(DateTime dt)
{
	u64 t = dt.year;
	t *= 12;
	t += dt.mon;
	t *= 31;
	t += dt.day;
	t *= 24;
	t += dt.hour;
	t *= 60;
	t += dt.min;
	t *= 61;
	t += dt.sec;
	t *= 1000;
	t += dt.msec;
	return t;
}

static DateTime
date_time_from_dense_time(DenseTime t)
{
	DateTime dt = {0};
	dt.msec     = t % 1000;
	t /= 1000;
	dt.sec = t % 61;
	t /= 61;
	dt.min = t % 60;
	t /= 60;
	dt.hour = t % 24;
	t /= 24;
	dt.day = t % 31;
	t /= 31;
	dt.mon = t % 12;
	t /= 12;
	dt.year = t;
	return dt;
}
