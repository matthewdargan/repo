static u64
cmdhash(String8 s)
{
	u64 h, i;

	h = 5381;
	for (i = 0; i < s.len; i++)
		h = ((h << 5) + h) + s.str[i];
	return h;
}

static Cmdopt **
cmdslot(Cmd *c, String8 s)
{
	Cmdopt **slot;
	u64 h, bucket;

	slot = NULL;
	if (c->optabsz != 0) {
		h = cmdhash(s);
		bucket = h % c->optabsz;
		slot = &c->optab[bucket];
	}
	return slot;
}

static Cmdopt *
cmdslottoopt(Cmdopt **slot, String8 s)
{
	Cmdopt *opt, *v;

	opt = NULL;
	for (v = *slot; v != NULL; v = v->hash_next)
		if (str8cmp(s, v->str, 0)) {
			opt = v;
			break;
		}
	return opt;
}

static void
cmdpushopt(Cmdoptlist *list, Cmdopt *v)
{
	if (list->start == NULL) {
		list->start = v;
		list->end = v;
	} else {
		list->end->next = v;
		list->end = v;
	}
	v->next = NULL;
	list->cnt++;
}

static Cmdopt *
cmdinsertopt(Arena *a, Cmd *c, String8 s, String8list vals)
{
	Cmdopt *v, *found;
	Cmdopt **slot;
	Stringjoin join;

	slot = cmdslot(c, s);
	found = cmdslottoopt(slot, s);
	if (found != NULL)
		v = found;
	else {
		v = pusharr(a, Cmdopt, 1);
		v->hash_next = *slot;
		v->hash = cmdhash(s);
		v->str = pushstr8cpy(a, s);
		v->vals = vals;
		join.pre = str8lit("");
		join.sep = str8lit(",");
		join.post = str8lit("");
		v->val = str8listjoin(a, &v->vals, &join);
		*slot = v;
		cmdpushopt(&c->opts, v);
	}
	return v;
}

static Cmd
cmdparse(Arena *a, String8list args)
{
	Cmd parsed;
	b32 afterdash, firstarg, isopt;
	String8node *node, *next;
	String8 optname;
	String8list optvals;
	u64 eqpos, i;

	memset(&parsed, 0, sizeof(parsed));
	parsed.exe = args.start->str;
	parsed.optabsz = 4096;
	parsed.optab = pusharr(a, Cmdopt *, parsed.optabsz);
	afterdash = 0;
	firstarg = 1;
	for (node = args.start->next, next = NULL; node != NULL; node = next) {
		next = node->next;
		optname = node->str;
		isopt = 1;
		if (!afterdash) {
			if (str8cmp(node->str, str8lit("--"), 0)) {
				afterdash = 1;
				isopt = 0;
			} else if (str8cmp(str8prefix(node->str, 2), str8lit("--"), 0))
				optname = str8skip(optname, 2);
			else if (str8cmp(str8prefix(node->str, 1), str8lit("-"), 0))
				optname = str8skip(optname, 1);
			else
				isopt = 0;
		} else
			isopt = 0;
		if (isopt) {
			memset(&optvals, 0, sizeof(optvals));
			eqpos = str8index(optname, 0, str8lit("="), 0);
			if (eqpos < optname.len) {
				str8listpush(a, &optvals, str8skip(optname, eqpos + 1));
				optname = str8prefix(optname, eqpos);
			} else if (next != NULL && next->str.len > 0 && next->str.str[0] != '-') {
				str8listpush(a, &optvals, next->str);
				next = next->next;
			}
			cmdinsertopt(a, &parsed, optname, optvals);
		} else {
			if (!str8cmp(node->str, str8lit("--"), 0))
				afterdash = 1;
			if (afterdash || !firstarg)
				str8listpush(a, &parsed.inputs, node->str);
			firstarg = 0;
		}
	}
	parsed.argc = args.nnode;
	parsed.argv = pusharr(a, char *, parsed.argc);
	for (node = args.start, i = 0; node != NULL; node = node->next, i++)
		parsed.argv[i] = (char *)pushstr8cpy(a, node->str).str;
	return parsed;
}

static Cmdopt *
cmdopt(Cmd *c, String8 name)
{
	return cmdslottoopt(cmdslot(c, name), name);
}

static String8
cmdstr(Cmd *c, String8 name)
{
	String8 s;
	Cmdopt *v;

	s = str8zero();
	v = cmdopt(c, name);
	if (v != NULL)
		s = v->val;
	return s;
}

static b32
cmdhasflag(Cmd *c, String8 name)
{
	return cmdopt(c, name) != NULL;
}

static b32
cmdhasarg(Cmd *c, String8 name)
{
	Cmdopt *v;

	v = cmdopt(c, name);
	return v != NULL && v->vals.nnode > 0;
}
