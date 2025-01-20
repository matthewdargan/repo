#ifndef COMMAND_LINE_H
#define COMMAND_LINE_H

typedef struct cmd_line_opt cmd_line_opt;
struct cmd_line_opt {
    cmd_line_opt *next;
    cmd_line_opt *hash_next;
    u64 hash;
    string8 string;
    string8list value_strings;
    string8 value_string;
};

typedef struct cmd_line_opt_list cmd_line_opt_list;
struct cmd_line_opt_list {
    u64 count;
    cmd_line_opt *first;
    cmd_line_opt *last;
};

typedef struct cmd_line cmd_line;
struct cmd_line {
    string8 exe_name;
    cmd_line_opt_list options;
    string8list inputs;
    u64 option_table_size;
    cmd_line_opt **option_table;
    u64 argc;
    char **argv;
};

internal u64 cmd_line_hash_from_string(string8 string);
internal cmd_line_opt **cmd_line_slot_from_string(cmd_line *cmd_line, string8 string);
internal cmd_line_opt *cmd_line_opt_from_slot(cmd_line_opt **slot, string8 string);
internal void cmd_line_push_opt(cmd_line_opt_list *list, cmd_line_opt *var);
internal cmd_line_opt *cmd_line_insert_opt(arena *a, cmd_line *cmd_line, string8 string, string8list values);
internal cmd_line cmd_line_from_string_list(arena *a, string8list arguments);
internal cmd_line_opt *cmd_line_opt_from_string(cmd_line *cmd_line, string8 name);
internal string8 cmd_line_string(cmd_line *cmd_line, string8 name);
internal b32 cmd_line_has_flag(cmd_line *cmd_line, string8 name);
internal b32 cmd_line_has_argument(cmd_line *cmd_line, string8 name);

#endif  // COMMAND_LINE_H
