#ifndef LOG_H
#define LOG_H

// Log Types
typedef enum LogMsgKind
{
	LogMsgKind_Info,
	LogMsgKind_Error,
	LogMsgKind_COUNT
} LogMsgKind;

typedef struct LogScope LogScope;
struct LogScope
{
	LogScope *next;
	u64 pos;
	String8List strings[LogMsgKind_COUNT];
};

typedef struct LogScopeResult LogScopeResult;
struct LogScopeResult
{
	String8 strings[LogMsgKind_COUNT];
};

typedef struct Log Log;
struct Log
{
	Arena *arena;
	LogScope *top_scope;
};

// Log Creation/Selection
static Log *log_alloc(void);
static void log_release(Log *log);
static void log_select(Log *log);

// Log Building
static void log_msg(LogMsgKind kind, String8 string);
static void log_msgf(LogMsgKind kind, char *fmt, ...);
#define log_info(s) log_msg(LogMsgKind_Info, (s))
#define log_infof(...) log_msgf(LogMsgKind_Info, __VA_ARGS__)
#define log_error(s) log_msg(LogMsgKind_Error, (s))
#define log_errorf(...) log_msgf(LogMsgKind_Error, __VA_ARGS__)

#define LogInfoNamedBlock(s) DeferLoop(log_infof("%S:\n{\n", (s)), log_infof("}\n"))
#define LogInfoNamedBlockF(...) DeferLoop((log_infof(__VA_ARGS__), log_infof(":\n{\n")), log_infof("}\n"))

// Log Scopes
static void log_scope_begin(void);
static LogScopeResult log_scope_end(Arena *arena);

#endif // LOG_H
