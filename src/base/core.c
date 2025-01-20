internal dense_time dense_time_from_date_time(date_time dt) {
    dense_time result = 0;
    result += dt.year;
    result *= 12;
    result += dt.mon;
    result *= 31;
    result += dt.day;
    result *= 24;
    result += dt.hour;
    result *= 60;
    result += dt.min;
    result *= 61;
    result += dt.sec;
    result *= 1000;
    result += dt.msec;
    return result;
}

internal date_time date_time_from_dense_time(dense_time time) {
    date_time result = {0};
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
    ASSERT(time <= max_u32);
    result.year = (u32)time;
    return result;
}
