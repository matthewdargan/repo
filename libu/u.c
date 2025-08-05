static u16
bswapu16(u16 x)
{
	return (((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8));
}

static u32
bswapu32(u32 x)
{
	return (((x & 0xFF000000) >> 24) | ((x & 0x00FF0000) >> 8) | ((x & 0x0000FF00) << 8) | ((x & 0x000000FF) << 24));
}

static u64
bswapu64(u64 x)
{
	return (((x & 0xFF00000000000000ULL) >> 56) | ((x & 0x00FF000000000000ULL) >> 40) |
	        ((x & 0x0000FF0000000000ULL) >> 24) | ((x & 0x000000FF00000000ULL) >> 8) |
	        ((x & 0x00000000FF000000ULL) << 8) | ((x & 0x0000000000FF0000ULL) << 24) |
	        ((x & 0x000000000000FF00ULL) << 40) | ((x & 0x00000000000000FFULL) << 56));
}

static u64
max(u64 a, u64 b)
{
	return a < b ? b : a;
}

static u64
min(u64 a, u64 b)
{
	return a > b ? b : a;
}

static u64
datetimetodense(Datetime dt)
{
	u64 t;

	t = dt.year;
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

static Datetime
densetodatetime(u64 t)
{
	Datetime dt;

	dt.msec = t % 1000;
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
