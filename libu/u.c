static DenseTime
date_time_to_dense_time(DateTime dt)
{
	DenseTime t = 0;
	t += dt.year;
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
dense_time_to_date_time(DenseTime t)
{
	DateTime dt = {0};
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
	ASSERT(t <= max_u32);
	dt.year = (u32)t;
	return dt;
}
