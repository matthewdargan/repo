static u64
cmdhash(String8 s)
{
	u64 h = 5381;
	for (u64 i = 0; i < s.len; i++)
	{
		h = ((h << 5) + h) + s.str[i];
	}
	return h;
}

static Cmdopt **
cmdslot(Cmd *c, String8 s)
{
	Cmdopt **slot = NULL;
	if (c->optabsz != 0)
	{
		u64 h = cmdhash(s);
		u64 bucket = h % c->optabsz;
		slot = &c->optab[bucket];
	}
	return slot;
}

static Cmdopt *
cmdslottoopt(Cmdopt **slot, String8 s)
{
	Cmdopt *opt = NULL;
	for (Cmdopt *v = *slot; v != NULL; v = v->hash_next)
	{
		if (str8cmp(s, v->str, 0))
		{
			opt = v;
			break;
		}
	}
	return opt;
}

static void
cmdpushopt(Cmdoptlist *list, Cmdopt *v)
{
	if (list->start == NULL)
	{
		list->start = v;
		list->end = v;
	}
	else
	{
		list->end->next = v;
		list->end = v;
	}
	v->next = NULL;
	list->cnt++;
}

static Cmdopt *
cmdinsertopt(Arena *a, Cmd *c, String8 s, String8list vals)
{
	Cmdopt **slot = cmdslot(c, s);
	Cmdopt *found = cmdslottoopt(slot, s);
	Cmdopt *v;
	if (found != NULL)
	{
		v = found;
	}
	else
	{
		v = push_array(a, Cmdopt, 1);
		v->hash_next = *slot;
		v->hash = cmdhash(s);
		v->str = pushstr8cpy(a, s);
		v->vals = vals;
		Stringjoin join = {
		    .pre = str8lit(""),
		    .sep = str8lit(","),
		    .post = str8lit(""),
		};
		v->val = str8listjoin(a, &v->vals, &join);
		*slot = v;
		cmdpushopt(&c->opts, v);
	}
	return v;
}

static Cmd
cmdparse(Arena *a, String8list args)
{
	Cmd parsed = {0};
	parsed.exe = args.start->str;
	parsed.optabsz = 4096;
	parsed.optab = push_array(a, Cmdopt *, parsed.optabsz);
	b32 afterdash = 0;
	b32 firstarg = 1;
	for (String8node *node = args.start->next, *next = NULL; node != NULL; node = next)
	{
		next = node->next;
		String8 optname = node->str;
		b32 isopt = 1;
		if (!afterdash)
		{
			if (str8cmp(node->str, str8lit("--"), 0))
			{
				afterdash = 1;
				isopt = 0;
			}
			else if (str8cmp(str8prefix(node->str, 2), str8lit("--"), 0))
			{
				optname = str8skip(optname, 2);
			}
			else if (str8cmp(str8prefix(node->str, 1), str8lit("-"), 0))
			{
				optname = str8skip(optname, 1);
			}
			else
			{
				isopt = 0;
			}
		}
		else
		{
			isopt = 0;
		}
		if (isopt)
		{
			String8list optvals = {0};
			u64 eqpos = str8index(optname, 0, str8lit("="), 0);
			if (eqpos < optname.len)
			{
				str8listpush(a, &optvals, str8skip(optname, eqpos + 1));
				optname = str8prefix(optname, eqpos);
			}
			else if (next != NULL && next->str.len > 0 && next->str.str[0] != '-')
			{
				str8listpush(a, &optvals, next->str);
				next = next->next;
			}
			cmdinsertopt(a, &parsed, optname, optvals);
		}
		else
		{
			if (!str8cmp(node->str, str8lit("--"), 0))
			{
				afterdash = 1;
			}
			if (afterdash || !firstarg)
			{
				str8listpush(a, &parsed.inputs, node->str);
			}
			firstarg = 0;
		}
	}
	parsed.argc = args.nnode;
	parsed.argv = push_array(a, char *, parsed.argc);
	u64 i = 0;
	for (String8node *node = args.start; node != NULL; node = node->next, i++)
	{
		parsed.argv[i] = (char *)pushstr8cpy(a, node->str).str;
	}
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
	String8 s = str8zero();
	Cmdopt *v = cmdopt(c, name);
	if (v != NULL)
	{
		s = v->val;
	}
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
	Cmdopt *v = cmdopt(c, name);
	return v != NULL && v->vals.nnode > 0;
}
