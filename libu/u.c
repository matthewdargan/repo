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
