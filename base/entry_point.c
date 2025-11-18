////////////////////////////////
//~ Entry Point Functions

internal void
main_thread_base_entry_point(int arguments_count, char **arguments)
{
	Temp scratch = scratch_begin(0, 0);

	// parse command line
	String8List command_line_argument_strings = {0};
	{
		for(u64 i = 0; i < (u64)arguments_count; i += 1)
		{
			str8_list_push(scratch.arena, &command_line_argument_strings, str8_cstring(arguments[i]));
		}
	}
	CmdLine cmd_line = cmd_line_from_string_list(scratch.arena, command_line_argument_strings);

	// call into entry point
	entry_point(&cmd_line);

	scratch_end(scratch);
}

internal void
supplement_thread_base_entry_point(ThreadEntryPointFunctionType *entry_point, void *params)
{
	TCTX *tctx = tctx_alloc();
	tctx_select(tctx);
	entry_point(params);
	tctx_release(tctx);
}
