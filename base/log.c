// Globals/Thread-Locals
thread_static Log *log_active = 0;

// Log Creation/Selection
static Log *
log_alloc(void)
{
	Arena *arena = arena_alloc();
	Log *log = push_array(arena, Log, 1);
	log->arena = arena;
	return log;
}

static void
log_release(Log *log)
{
	arena_release(log->arena);
}

static void
log_select(Log *log)
{
	log_active = log;
}

// Log Building
static void
log_msg(LogMsgKind kind, String8 string)
{
	if(log_active != 0 && log_active->top_scope != 0)
	{
		String8 string_copy = str8_copy(log_active->arena, string);
		str8_list_push(log_active->arena, &log_active->top_scope->strings[kind], string_copy);
	}
}

static void
log_msgf(LogMsgKind kind, char *fmt, ...)
{
	if(log_active != 0)
	{
		Temp scratch = scratch_begin(0, 0);
		va_list args;
		va_start(args, fmt);
		String8 string = str8fv(scratch.arena, fmt, args);
		log_msg(kind, string);
		va_end(args);
		scratch_end(scratch);
	}
}

// Log Scopes
static void
log_scope_begin(void)
{
	if(log_active != 0)
	{
		u64 pos = arena_pos(log_active->arena);
		LogScope *scope = push_array(log_active->arena, LogScope, 1);
		scope->pos = pos;
		SLLStackPush(log_active->top_scope, scope);
	}
}

static LogScopeResult
log_scope_end(Arena *arena)
{
	LogScopeResult result = {0};
	if(log_active != 0)
	{
		LogScope *scope = log_active->top_scope;
		if(scope != 0)
		{
			SLLStackPop(log_active->top_scope);
			if(arena != 0)
			{
				for(LogMsgKind kind = (LogMsgKind)0; kind < LogMsgKind_COUNT; kind = (LogMsgKind)(kind + 1))
				{
					Temp scratch = scratch_begin(&arena, 1);
					String8 result_unindented = str8_list_join(scratch.arena, &scope->strings[kind], 0);
					result.strings[kind] = indented_from_string(arena, result_unindented);
					scratch_end(scratch);
				}
			}
			arena_pop_to(log_active->arena, scope->pos);
		}
	}
	return result;
}
