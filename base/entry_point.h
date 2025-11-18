#ifndef ENTRY_POINT_H
#define ENTRY_POINT_H

////////////////////////////////
//~ Entry Point Functions

internal void main_thread_base_entry_point(int argc, char **argv);
internal void supplement_thread_base_entry_point(ThreadEntryPointFunctionType *entry_point, void *params);
internal void entry_point(CmdLine *cmd_line);

#endif // ENTRY_POINT_H
