internal u64
cmd_line_hash_from_string(string8 s)
{
	u64 result = 5381;
	for (u64 i = 0; i < s.size; ++i) {
		result = ((result << 5) + result) + s.str[i];
	}
	return result;
}

internal cmd_line_opt **
cmd_line_slot_from_string(cmd_line *cl, string8 s)
{
	cmd_line_opt **slot = 0;
	if (cl->option_table_size != 0) {
		u64 hash = cmd_line_hash_from_string(s);
		u64 bucket = hash % cl->option_table_size;
		slot = &cl->option_table[bucket];
	}
	return slot;
}

internal cmd_line_opt *
cmd_line_opt_from_slot(cmd_line_opt **slot, string8 s)
{
	cmd_line_opt *result = 0;
	for (cmd_line_opt *var = *slot; var; var = var->hash_next) {
		if (str8_match(s, var->string, 0)) {
			result = var;
			break;
		}
	}
	return result;
}

internal void
cmd_line_push_opt(cmd_line_opt_list *list, cmd_line_opt *var)
{
	SLL_QUEUE_PUSH(list->first, list->last, var);
	list->count += 1;
}

internal cmd_line_opt *
cmd_line_insert_opt(arena *a, cmd_line *cl, string8 s, string8list values)
{
	cmd_line_opt *var = 0;
	cmd_line_opt **slot = cmd_line_slot_from_string(cl, s);
	cmd_line_opt *existing_var = cmd_line_opt_from_slot(slot, s);
	if (existing_var != 0) {
		var = existing_var;
	} else {
		var = push_array(a, cmd_line_opt, 1);
		var->hash_next = *slot;
		var->hash = cmd_line_hash_from_string(s);
		var->string = push_str8_copy(a, s);
		var->value_strings = values;
		string_join join = {{0}};
		join.pre = str8_lit("");
		join.sep = str8_lit(",");
		join.post = str8_lit("");
		var->value_string = str8_list_join(a, &var->value_strings, &join);
		*slot = var;
		cmd_line_push_opt(&cl->options, var);
	}
	return var;
}

internal cmd_line
cmd_line_from_string_list(arena *a, string8list command_line)
{
	cmd_line parsed = {{0}};
	parsed.exe_name = command_line.first->string;
	{
		parsed.option_table_size = 4096;
		parsed.option_table = push_array(a, cmd_line_opt *, parsed.option_table_size);
	}
	b32 after_passthrough_option = 0;
	b32 first_passthrough = 1;
	for (string8node *node = command_line.first->next, *next = 0; node != 0; node = next) {
		next = node->next;
		string8 option_name = node->string;
		// Look at -- or - at the start of an argument to determine if it's a flag option. All arguments after a single
		// "--" (with no trailing string on the command line will be considered as input files.
		b32 is_option = 1;
		if (after_passthrough_option == 0) {
			if (str8_match(node->string, str8_lit("--"), 0)) {
				after_passthrough_option = 1;
				is_option = 0;
			} else if (str8_match(str8_prefix(node->string, 2), str8_lit("--"), 0)) {
				option_name = str8_skip(option_name, 2);
			} else if (str8_match(str8_prefix(node->string, 1), str8_lit("-"), 0)) {
				option_name = str8_skip(option_name, 1);
			} else {
				is_option = 0;
			}
		} else {
			is_option = 0;
		}
		if (is_option) {
			b32 has_args = 0;
			u64 arg_signifier_position1 = str8_find_needle(option_name, 0, str8_lit(":"), 0);
			u64 arg_signifier_position2 = str8_find_needle(option_name, 0, str8_lit("="), 0);
			u64 arg_signifier_position = MIN(arg_signifier_position1, arg_signifier_position2);
			string8 arg_portion_this_string = str8_skip(option_name, arg_signifier_position + 1);
			if (arg_signifier_position < option_name.size) {
				has_args = 1;
			}
			option_name = str8_prefix(option_name, arg_signifier_position);
			string8list args = {0};
			if (has_args) {
				for (string8node *n = node; n != 0; n = n->next) {
					next = n->next;
					string8 s = n->string;
					if (n == node) {
						s = arg_portion_this_string;
					}
					u8 splits[] = {','};
					string8list args_in_this_string = str8_split(a, s, splits, ARRAY_COUNT(splits), 0);
					for (string8node *sub_arg = args_in_this_string.first; sub_arg != 0; sub_arg = sub_arg->next) {
						str8_list_push(a, &args, sub_arg->string);
					}
					if (!str8_match(str8_postfix(n->string, 1), str8_lit(","), 0) &&
					    (n != node || arg_portion_this_string.size != 0)) {
						break;
					}
				}
			}
			cmd_line_insert_opt(a, &parsed, option_name, args);
		}
		// Default path, treat as a passthrough config option to be handled by tool-specific code.
		else if (!str8_match(node->string, str8_lit("--"), 0) || !first_passthrough) {
			str8_list_push(a, &parsed.inputs, node->string);
			after_passthrough_option = 1;
			first_passthrough = 0;
		}
	}
	parsed.argc = command_line.node_count;
	parsed.argv = push_array(a, char *, parsed.argc);
	{
		u64 i = 0;
		for (string8node *n = command_line.first; n != 0; n = n->next) {
			parsed.argv[i] = (char *)push_str8_copy(a, n->string).str;
			++i;
		}
	}
	return parsed;
}

internal cmd_line_opt *
cmd_line_opt_from_string(cmd_line *cl, string8 name)
{
	return cmd_line_opt_from_slot(cmd_line_slot_from_string(cl, name), name);
}

internal string8
cmd_line_string(cmd_line *cl, string8 name)
{
	string8 result = {0};
	cmd_line_opt *var = cmd_line_opt_from_string(cl, name);
	if (var != 0) {
		result = var->value_string;
	}
	return result;
}

internal b32
cmd_line_has_flag(cmd_line *cl, string8 name)
{
	return cmd_line_opt_from_string(cl, name) != 0;
}

internal b32
cmd_line_has_argument(cmd_line *cl, string8 name)
{
	cmd_line_opt *var = cmd_line_opt_from_string(cl, name);
	return var != 0 && var->value_strings.node_count > 0;
}
