#ifndef CMD_H
#define CMD_H

typedef struct CmdOpt CmdOpt;
struct CmdOpt {
	CmdOpt *next;
	CmdOpt *hash_next;
	u64 hash;
	String8 str;
	String8List vals;
	String8 val;
};

typedef struct CmdOptList CmdOptList;
struct CmdOptList {
	u64 cnt;
	CmdOpt *start;
	CmdOpt *end;
};

typedef struct Cmd Cmd;
struct Cmd {
	String8 exe;
	CmdOptList opts;
	String8List inputs;
	u64 opt_table_size;
	CmdOpt **opt_table;
	u64 argc;
	char **argv;
};

static u64 cmd_hash(String8 s);
static CmdOpt **cmd_slot(Cmd *c, String8 s);
static CmdOpt *cmd_slot_to_opt(CmdOpt **slot, String8 s);
static void cmd_push_opt(CmdOptList *list, CmdOpt *v);
static CmdOpt *cmd_insert_opt(Arena *a, Cmd *c, String8 s, String8List vals);
static Cmd cmd_parse(Arena *a, String8List args);
static CmdOpt *cmd_opt(Cmd *c, String8 name);
static String8 cmd_str(Cmd *c, String8 name);
static b32 cmd_has_flag(Cmd *c, String8 name);
static b32 cmd_has_arg(Cmd *c, String8 name);

#endif  // CMD_H
