// Command Line Parsing Functions
static CmdLineOpt **
cmd_line_slot_from_string(CmdLine *cmd_line, String8 string)
{
	CmdLineOpt **slot = 0;
	if (cmd_line->option_table_size != 0)
	{
		u64 hash   = u64_hash_from_str8(string);
		u64 bucket = hash % cmd_line->option_table_size;
		slot       = &cmd_line->option_table[bucket];
	}
	return slot;
}

static CmdLineOpt *
cmd_line_opt_from_slot(CmdLineOpt **slot, String8 string)
{
	CmdLineOpt *result = 0;
	for (CmdLineOpt *opt = *slot; opt != 0; opt = opt->hash_next)
	{
		if (str8_match(string, opt->string, 0))
		{
			result = opt;
			break;
		}
	}
	return result;
}

static void
cmd_line_push_opt(CmdLineOptList *list, CmdLineOpt *opt)
{
	SLLQueuePush(list->first, list->last, opt);
	list->count++;
}

static CmdLineOpt *
cmd_line_insert_opt(Arena *arena, CmdLine *cmd_line, String8 string, String8List values)
{
	CmdLineOpt **slot        = cmd_line_slot_from_string(cmd_line, string);
	CmdLineOpt *existing_opt = cmd_line_opt_from_slot(slot, string);
	CmdLineOpt *opt;
	if (existing_opt != 0)
	{
		opt = existing_opt;
	}
	else
	{
		opt                = push_array(arena, CmdLineOpt, 1);
		opt->hash_next     = *slot;
		opt->hash          = u64_hash_from_str8(string);
		opt->string        = str8_copy(arena, string);
		opt->value_strings = values;
		StringJoin join    = {
		       .pre  = str8_lit(""),
		       .sep  = str8_lit(","),
		       .post = str8_lit(""),
    };
		opt->value_string = str8_list_join(arena, &opt->value_strings, &join);
		*slot             = opt;
		cmd_line_push_opt(&cmd_line->options, opt);
	}
	return opt;
}

static CmdLine
cmd_line_from_string_list(Arena *arena, String8List arguments)
{
	CmdLine parsed           = {0};
	parsed.exe_name          = arguments.first->string;
	parsed.option_table_size = 64;
	parsed.option_table      = push_array(arena, CmdLineOpt *, parsed.option_table_size);

	// parse options/inputs
	b32 after_passthrough_option = 0;
	b32 first_passthrough        = 1;
	for (String8Node *node = arguments.first->next, *next = 0; node != 0; node = next)
	{
		next = node->next;

		// look at --, - at the start of an argument to determine if it's a flag option.
		// all arguments after a single "--" (with no trailing string) on the command line
		// will be considered as passthrough input strings.
		b32 is_option       = 0;
		String8 option_name = node->string;
		if (!after_passthrough_option)
		{
			is_option = 1;
			if (str8_match(node->string, str8_lit("--"), 0))
			{
				after_passthrough_option = 1;
				is_option                = 0;
			}
			else if (str8_match(str8_prefix(node->string, 2), str8_lit("--"), 0))
			{
				option_name = str8_skip(option_name, 2);
			}
			else if (str8_match(str8_prefix(node->string, 1), str8_lit("-"), 0))
			{
				option_name = str8_skip(option_name, 1);
			}
			else
			{
				is_option = 0;
			}
		}

		// string is an option
		if (is_option)
		{
			// unpack option prefix
			b32 has_values                    = 0;
			u64 value_signifier_position      = str8_find_needle(option_name, 0, str8_lit("="), 0);
			String8 value_portion_this_string = str8_skip(option_name, value_signifier_position + 1);
			if (value_signifier_position < option_name.size)
			{
				has_values = 1;
			}
			option_name = str8_prefix(option_name, value_signifier_position);

			// parse option's values
			String8List values = {0};
			if (has_values)
			{
				for (String8Node *n = node; n; n = n->next)
				{
					next           = n->next;
					String8 string = n->string;
					if (n == node)
					{
						string = value_portion_this_string;
					}
					u8 splits[]                       = {','};
					String8List values_in_this_string = str8_split(arena, string, splits, ArrayCount(splits), 0);
					for (String8Node *sub_val = values_in_this_string.first; sub_val; sub_val = sub_val->next)
					{
						str8_list_push(arena, &values, sub_val->string);
					}
					if (!str8_match(str8_postfix(n->string, 1), str8_lit(","), 0) &&
					    (n != node || value_portion_this_string.size != 0))
					{
						break;
					}
				}
			}

			// store
			cmd_line_insert_opt(arena, &parsed, option_name, values);
		}

		// default path - treat as a passthrough input
		else if (!str8_match(node->string, str8_lit("--"), 0) || !first_passthrough)
		{
			str8_list_push(arena, &parsed.inputs, node->string);
			first_passthrough = 0;
		}
	}

	// fill argc/argv
	parsed.argc = arguments.node_count;
	parsed.argv = push_array(arena, char *, parsed.argc);
	u64 i       = 0;
	for (String8Node *node = arguments.first; node != 0; node = node->next, i++)
	{
		parsed.argv[i] = (char *)str8_copy(arena, node->string).str;
	}
	return parsed;
}

static CmdLineOpt *
cmd_line_opt_from_string(CmdLine *cmd_line, String8 name)
{
	return cmd_line_opt_from_slot(cmd_line_slot_from_string(cmd_line, name), name);
}

static String8List
cmd_line_strings(CmdLine *cmd_line, String8 name)
{
	String8List result = {0};
	CmdLineOpt *opt    = cmd_line_opt_from_string(cmd_line, name);
	if (opt != 0)
	{
		result = opt->value_strings;
	}
	return result;
}

static String8
cmd_line_string(CmdLine *cmd_line, String8 name)
{
	String8 result  = str8_zero();
	CmdLineOpt *opt = cmd_line_opt_from_string(cmd_line, name);
	if (opt != 0)
	{
		result = opt->value_string;
	}
	return result;
}

static b32
cmd_line_has_flag(CmdLine *cmd_line, String8 name)
{
	return cmd_line_opt_from_string(cmd_line, name) != 0;
}

static b32
cmd_line_has_argument(CmdLine *cmd_line, String8 name)
{
	CmdLineOpt *opt = cmd_line_opt_from_string(cmd_line, name);
	return opt != 0 && opt->value_strings.node_count > 0;
}
