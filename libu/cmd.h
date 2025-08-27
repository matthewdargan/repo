#ifndef CMD_H
#define CMD_H

typedef struct Cmdopt Cmdopt;
struct Cmdopt {
	Cmdopt *next;
	Cmdopt *hash_next;
	u64 hash;
	String8 str;
	String8list vals;
	String8 val;
};

typedef struct Cmdoptlist Cmdoptlist;
struct Cmdoptlist {
	u64 cnt;
	Cmdopt *start;
	Cmdopt *end;
};

typedef struct Cmd Cmd;
struct Cmd {
	String8 exe;
	Cmdoptlist opts;
	String8list inputs;
	u64 optabsz;
	Cmdopt **optab;
	u64 argc;
	char **argv;
};

static u64 cmdhash(String8 s);
static Cmdopt **cmdslot(Cmd *c, String8 s);
static Cmdopt *cmdslottoopt(Cmdopt **slot, String8 s);
static void cmdpushopt(Cmdoptlist *list, Cmdopt *v);
static Cmdopt *cmdinsertopt(Arena *a, Cmd *c, String8 s, String8list vals);
static Cmd cmdparse(Arena *a, String8list args);
static Cmdopt *cmdopt(Cmd *c, String8 name);
static String8 cmdstr(Cmd *c, String8 name);
static b32 cmdhasflag(Cmd *c, String8 name);
static b32 cmdhasarg(Cmd *c, String8 name);

#endif /* CMD_H */
