static u64
cmdhash(String8 s)
{
	u64 h = 5381;
	for (u64 i = 0; i < s.size; i++)
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
		if (str8_match(s, v->str, 0))
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
cmdinsertopt(Arena *a, Cmd *c, String8 s, String8List vals)
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
		v->str = str8_copy(a, s);
		v->vals = vals;
		StringJoin join = {
		    .pre = str8_lit(""),
		    .sep = str8_lit(","),
		    .post = str8_lit(""),
		};
		v->val = str8_list_join(a, &v->vals, &join);
		*slot = v;
		cmdpushopt(&c->opts, v);
	}
	return v;
}

static Cmd
cmdparse(Arena *a, String8List args)
{
	Cmd parsed = {0};
	parsed.exe = args.first->string;
	parsed.optabsz = 4096;
	parsed.optab = push_array(a, Cmdopt *, parsed.optabsz);
	b32 afterdash = 0;
	b32 firstarg = 1;
	for (String8Node *node = args.first->next, *next = NULL; node != NULL; node = next)
	{
		next = node->next;
		String8 optname = node->string;
		b32 isopt = 1;
		if (!afterdash)
		{
			if (str8_match(node->string, str8_lit("--"), 0))
			{
				afterdash = 1;
				isopt = 0;
			}
			else if (str8_match(str8_prefix(node->string, 2), str8_lit("--"), 0))
			{
				optname = str8_skip(optname, 2);
			}
			else if (str8_match(str8_prefix(node->string, 1), str8_lit("-"), 0))
			{
				optname = str8_skip(optname, 1);
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
			String8List optvals = {0};
			u64 eqpos = str8_find_needle(optname, 0, str8_lit("="), 0);
			if (eqpos < optname.size)
			{
				str8_list_push(a, &optvals, str8_skip(optname, eqpos + 1));
				optname = str8_prefix(optname, eqpos);
			}
			else if (next != NULL && next->string.size > 0 && next->string.str[0] != '-')
			{
				str8_list_push(a, &optvals, next->string);
				next = next->next;
			}
			cmdinsertopt(a, &parsed, optname, optvals);
		}
		else
		{
			if (!str8_match(node->string, str8_lit("--"), 0))
			{
				afterdash = 1;
			}
			if (afterdash || !firstarg)
			{
				str8_list_push(a, &parsed.inputs, node->string);
			}
			firstarg = 0;
		}
	}
	parsed.argc = args.node_count;
	parsed.argv = push_array(a, char *, parsed.argc);
	u64 i = 0;
	for (String8Node *node = args.first; node != NULL; node = node->next, i++)
	{
		parsed.argv[i] = (char *)str8_copy(a, node->string).str;
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
	String8 s = str8_zero();
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
	return v != NULL && v->vals.node_count > 0;
}
