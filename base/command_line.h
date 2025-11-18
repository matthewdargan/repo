#ifndef COMMAND_LINE_H
#define COMMAND_LINE_H

////////////////////////////////
//~ Parsed Command Line Types

typedef struct CmdLineOpt CmdLineOpt;
struct CmdLineOpt
{
	CmdLineOpt *next;
	CmdLineOpt *hash_next;
	u64 hash;
	String8 string;
	String8List value_strings;
	String8 value_string;
};

typedef struct CmdLineOptList CmdLineOptList;
struct CmdLineOptList
{
	u64 count;
	CmdLineOpt *first;
	CmdLineOpt *last;
};

typedef struct CmdLine CmdLine;
struct CmdLine
{
	String8 exe_name;
	CmdLineOptList options;
	String8List inputs;
	u64 option_table_size;
	CmdLineOpt **option_table;
	u64 argc;
	char **argv;
};

////////////////////////////////
//~ Command Line Parsing Functions

internal CmdLineOpt **cmd_line_slot_from_string(CmdLine *cmd_line, String8 string);
internal CmdLineOpt *cmd_line_opt_from_slot(CmdLineOpt **slot, String8 string);
internal void cmd_line_push_opt(CmdLineOptList *list, CmdLineOpt *opt);
internal CmdLineOpt *cmd_line_insert_opt(Arena *arena, CmdLine *cmd_line, String8 string, String8List values);
internal CmdLine cmd_line_from_string_list(Arena *arena, String8List arguments);
internal CmdLineOpt *cmd_line_opt_from_string(CmdLine *cmd_line, String8 name);
internal String8List cmd_line_strings(CmdLine *cmd_line, String8 name);
internal String8 cmd_line_string(CmdLine *cmd_line, String8 name);
internal b32 cmd_line_has_flag(CmdLine *cmd_line, String8 name);
internal b32 cmd_line_has_argument(CmdLine *cmd_line, String8 name);

#endif // COMMAND_LINE_H
