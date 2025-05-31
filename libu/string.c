static b32
charislower(u8 c)
{
	return 'a' <= c && c <= 'z';
}

static b32
charisupper(u8 c)
{
	return 'A' <= c && c <= 'Z';
}

static u8
chartolower(u8 c)
{
	if (charisupper(c))
		c += ('a' - 'A');
	return c;
}

static u8
chartoupper(u8 c)
{
	if (charislower(c))
		c += ('A' - 'a');
	return c;
}

static u64
cstrlen(u8 *c)
{
	u8 *p;

	for (p = c; *p != 0; p++) {
	}
	return p - c;
}

static String8
str8(u8 *str, u64 len)
{
	String8 s;

	s.str = str;
	s.len = len;
	return s;
}

static String8
str8rng(u8 *start, u8 *end)
{
	String8 s;

	s.str = start;
	s.len = end - start;
	return s;
}

static String8
str8zero(void)
{
	String8 s;

	s.str = NULL;
	s.len = 0;
	return s;
}

static String8
str8cstr(char *c)
{
	String8 s;

	s.str = (u8 *)c;
	s.len = cstrlen((u8 *)c);
	return s;
}

static Rng1u64
rng1u64(u64 min, u64 max)
{
	Rng1u64 r;
	u64 t;

	r.min = min;
	r.max = max;
	if (r.min > r.max) {
		t = r.min;
		r.min = r.max;
		r.max = t;
	}
	return r;
}

static u64
dim1u64(Rng1u64 r)
{
	return r.max > r.min ? (r.max - r.min) : 0;
}

static b32
str8cmp(String8 a, String8 b, u32 flags)
{
	b32 ok;
	u64 len, i;
	u8 at, bt;

	ok = 0;
	if (a.len == b.len && flags == 0)
		ok = memcmp(a.str, b.str, b.len) == 0;
	else if (a.len == b.len || (flags & RSIDETOL)) {
		len = min(a.len, b.len);
		ok = 1;
		for (i = 0; i < len; i++) {
			at = a.str[i];
			bt = b.str[i];
			if (flags & CASEINSENSITIVE) {
				at = chartoupper(at);
				bt = chartoupper(bt);
			}
			if (at != bt) {
				ok = 0;
				break;
			}
		}
	}
	return ok;
}

static u64
str8index(String8 s, u64 pos, String8 needle, u32 flags)
{
	u8 *p, *end, n0adj, c;
	u64 stoplen, i;
	String8 tail, hay;
	u32 adjflags;

	if (needle.len == 0)
		return s.len;
	p = s.str + pos;
	stoplen = s.len - needle.len;
	end = s.str + s.len;
	tail = str8skip(needle, 1);
	adjflags = flags | RSIDETOL;
	n0adj = needle.str[0];
	if (adjflags & CASEINSENSITIVE)
		n0adj = chartoupper(n0adj);
	for (; (u64)(p - s.str) <= stoplen; p++) {
		c = *p;
		if (adjflags & CASEINSENSITIVE)
			c = chartoupper(c);
		if (c == n0adj) {
			hay = str8rng(p + 1, end);
			if (str8cmp(hay, tail, adjflags))
				break;
		}
	}
	i = s.len;
	if ((u64)(p - s.str) <= stoplen)
		i = p - s.str;
	return i;
}

static u64
str8rindex(String8 s, u64 pos, String8 needle, u32 flags)
{
	s64 i;
	String8 hay;

	for (i = s.len - pos - needle.len; i >= 0; i--) {
		hay = str8(s.str + i, needle.len);
		if (str8cmp(hay, needle, flags))
			return i;
	}
	return 0;
}

static String8
str8substr(String8 s, Rng1u64 r)
{
	r.min = min(r.min, s.len);
	r.max = min(r.max, s.len);
	s.str += r.min;
	s.len = dim1u64(r);
	return s;
}

static String8
str8prefix(String8 s, u64 len)
{
	s.len = min(len, s.len);
	return s;
}

static String8
str8suffix(String8 s, u64 len)
{
	len = min(len, s.len);
	s.str = s.str + s.len - len;
	s.len = len;
	return s;
}

static String8
str8skip(String8 s, u64 len)
{
	len = min(len, s.len);
	s.str += len;
	s.len -= len;
	return s;
}

static String8
pushstr8cat(Arena *a, String8 s1, String8 s2)
{
	String8 str;

	str.len = s1.len + s2.len;
	str.str = pusharrnoz(a, u8, str.len + 1);
	memcpy(str.str, s1.str, s1.len);
	memcpy(str.str + s1.len, s2.str, s2.len);
	str.str[str.len] = 0;
	return str;
}

static String8
pushstr8cpy(Arena *a, String8 s)
{
	String8 str;

	str.len = s.len;
	str.str = pusharrnoz(a, u8, str.len + 1);
	memcpy(str.str, s.str, s.len);
	str.str[str.len] = 0;
	return str;
}

static String8
pushstr8fv(Arena *a, char *fmt, va_list args)
{
	va_list args2;
	u32 nbytes;
	String8 s;

	va_copy(args2, args);
	nbytes = vsnprintf(0, 0, fmt, args) + 1;
	s.str = pusharrnoz(a, u8, nbytes);
	s.len = vsnprintf((char *)s.str, nbytes, fmt, args2);
	s.str[s.len] = 0;
	va_end(args2);
	return s;
}

static String8
pushstr8f(Arena *a, char *fmt, ...)
{
	va_list args;
	String8 s;

	va_start(args, fmt);
	s = pushstr8fv(a, fmt, args);
	va_end(args);
	return s;
}

static b32
str8isint(String8 s, u32 radix)
{
	b32 ok;
	u64 i;
	u8 c;

	ok = 0;
	if (s.len > 0) {
		if (1 < radix && radix <= 16) {
			ok = 1;
			for (i = 0; i < s.len; i++) {
				c = s.str[i];
				if (!(c < 0x80) || hexdigitvals[c] >= radix) {
					ok = 0;
					break;
				}
			}
		}
	}
	return ok;
}

static u64
str8tou64(String8 s, u32 radix)
{
	u64 x, i;

	x = 0;
	if (1 < radix && radix <= 16) {
		for (i = 0; i < s.len; i++) {
			x *= radix;
			x += hexdigitvals[s.str[i] & 0x7f];
		}
	}
	return x;
}

static b32
str8tou64ok(String8 s, u64 *x)
{
	b32 ok;
	String8 hex, bin, oct;

	ok = 0;
	if (str8isint(s, 10)) {
		ok = 1;
		*x = str8tou64(s, 10);
	} else {
		if (s.len >= 2 && str8cmp(str8prefix(s, 2), str8lit("0x"), 0)) {
			hex = str8skip(s, 2);
			if (str8isint(hex, 16)) {
				ok = 1;
				*x = str8tou64(hex, 16);
			}
		} else if (s.len >= 2 && str8cmp(str8prefix(s, 2), str8lit("0b"), 0)) {
			bin = str8skip(s, 2);
			if (str8isint(bin, 2)) {
				ok = 1;
				*x = str8tou64(bin, 2);
			}
		} else if (s.len >= 1 && s.str[0] == '0') {
			oct = str8skip(s, 1);
			if (str8isint(oct, 8)) {
				ok = 1;
				*x = str8tou64(oct, 8);
			}
		}
	}
	return ok;
}

static String8
u64tostr8(Arena *a, u64 v, u32 radix, u8 mindig, u8 sep)
{
	String8 s, pre;
	u64 group, nleadz, ndigits, rem, nseps, digtosep, i, leadzidx;

	s = str8zero();
	pre = str8zero();
	switch (radix) {
		case 16:
			pre = str8lit("0x");
			break;
		case 8:
			pre = str8lit("0");
			break;
		case 2:
			pre = str8lit("0b");
			break;
		default:
			break;
	}
	group = 3;
	switch (radix) {
		default:
			break;
		case 2:
		case 8:
		case 16:
			group = 4;
			break;
	}
	nleadz = 0;
	ndigits = 1;
	rem = v;
	for (;;) {
		rem /= radix;
		if (rem == 0)
			break;
		ndigits++;
	}
	nleadz = (mindig > ndigits) ? mindig - ndigits : 0;
	nseps = 0;
	if (sep != 0) {
		nseps = (ndigits + nleadz) / group;
		if (nseps > 0 && (ndigits + nleadz) % group == 0)
			nseps--;
	}
	s.len = pre.len + nleadz + nseps + ndigits;
	s.str = pusharrnoz(a, u8, s.len + 1);
	s.str[s.len] = 0;
	rem = v;
	digtosep = group;
	for (i = 0; i < s.len; i++) {
		if (digtosep == 0 && sep != 0) {
			s.str[s.len - i - 1] = sep;
			digtosep = group + 1;
		} else {
			s.str[s.len - i - 1] = chartolower(hexdigits[rem % radix]);
			rem /= radix;
		}
		digtosep--;
		if (rem == 0)
			break;
	}
	for (leadzidx = 0; leadzidx < nleadz; leadzidx++)
		s.str[pre.len + leadzidx] = '0';
	if (pre.len != 0)
		memcpy(s.str, pre.str, pre.len);
	return s;
}

static String8node *
str8listpushnode(String8list *list, String8node *node, String8 s)
{
	if (list->start == NULL) {
		list->start = node;
		list->end = node;
	} else {
		list->end->next = node;
		list->end = node;
	}
	node->next = NULL;
	list->nnode++;
	list->tlen += s.len;
	node->str = s;
	return node;
}

static String8node *
str8listpush(Arena *a, String8list *list, String8 s)
{
	String8node *node;

	node = pusharrnoz(a, String8node, 1);
	str8listpushnode(list, node, s);
	return node;
}

static String8list
str8split(Arena *a, String8 s, u8 *split, u64 splen, u32 flags)
{
	String8list list;
	b32 issplit;
	u8 *p, *end, *start, c;
	u64 i;
	String8 ss;

	memset(&list, 0, sizeof list);
	end = s.str + s.len;
	for (p = s.str; p < end;) {
		start = p;
		for (; p < end; p++) {
			c = *p;
			issplit = 0;
			for (i = 0; i < splen; i++)
				if (split[i] == c) {
					issplit = 1;
					break;
				}
			if (issplit)
				break;
		}
		ss = str8rng(start, p);
		if ((flags & SPLITKEEPEMPTY) || ss.len > 0)
			str8listpush(a, &list, ss);
		p++;
	}
	return list;
}

static String8
str8listjoin(Arena *a, String8list *list, Stringjoin *opts)
{
	Stringjoin join;
	u64 nsep;
	String8 s;
	u8 *p;
	String8node *node;

	memset(&join, 0, sizeof join);
	if (opts != NULL)
		memcpy(&join, opts, sizeof join);
	nsep = 0;
	if (list->nnode > 0)
		nsep = list->nnode - 1;
	s = str8zero();
	s.len = join.pre.len + join.post.len + nsep * join.sep.len + list->tlen;
	p = s.str = pusharrnoz(a, u8, s.len + 1);
	memcpy(p, join.pre.str, join.pre.len);
	p += join.pre.len;
	for (node = list->start; node != NULL; node = node->next) {
		memcpy(p, node->str.str, node->str.len);
		p += node->str.len;
		if (node->next != NULL) {
			memcpy(p, join.sep.str, join.sep.len);
			p += join.sep.len;
		}
	}
	memcpy(p, join.post.str, join.post.len);
	p += join.post.len;
	*p = 0;
	return s;
}

static String8array
str8listtoarray(Arena *a, String8list *list)
{
	String8array array;
	u64 i;
	String8node *node;

	array.cnt = list->nnode;
	array.v = pusharrnoz(a, String8, array.cnt);
	i = 0;
	for (node = list->start; node != NULL; node = node->next, i++)
		array.v[i] = node->str;
	return array;
}

static String8array
str8arrayreserve(Arena *a, u64 cnt)
{
	String8array array;

	array.cnt = 0;
	array.v = pusharr(a, String8, cnt);
	return array;
}

static String8
str8dirname(String8 s)
{
	u64 p;

	p = s.len;
	while (p > 0) {
		p--;
		if (s.str[p] == '/')
			return str8prefix(s, p);
	}
	return str8zero();
}

static String8
str8basename(String8 s)
{
	u8 *p;

	if (s.len > 0) {
		for (p = s.str + s.len - 1; p >= s.str; p--)
			if (*p == '/')
				break;
		if (p >= s.str) {
			p++;
			s.len = s.str + s.len - p;
			s.str = p;
		}
	}
	return s;
}

static String8
str8prefixext(String8 s)
{
	u64 p;

	p = s.len;
	while (p > 0) {
		p--;
		if (s.str[p] == '.')
			return str8prefix(s, p);
	}
	return s;
}

static String8
str8ext(String8 s)
{
	u64 p;

	p = s.len;
	while (p > 0) {
		p--;
		if (s.str[p] == '.')
			return str8skip(s, p + 1);
	}
	return s;
}
