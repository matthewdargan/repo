#ifndef _9P_CORE_H
#define _9P_CORE_H

// 9P Protocol Constants
read_only static String8 version_9p = str8_lit_comp("9P2000");

#define MAX_WALK_ELEM_COUNT 16
#define STAT_DATA_FIXED_SIZE (2 + 2 + 4 + 1 + 4 + 8 + 4 + 4 + 4 + 8)
#define TAG_NONE 0xffff
#define FID_NONE 0xffffffff
#define MESSAGE_HEADER_SIZE 24
#define DIR_ENTRY_MAX 8192
#define DIR_BUFFER_MAX (DIR_ENTRY_MAX * 16)

// 9P Unique Identifier
typedef struct Qid Qid;
struct Qid
{
	u32 type;
	u32 version;
	u64 path;
};

// 9P Protocol Message
typedef struct Message9P Message9P;
struct Message9P
{
	u32 type;
	u32 tag;
	u32 fid;
	u32 max_message_size;                    // Tversion, Rversion
	String8 protocol_version;                // Tversion, Rversion
	u32 cancel_tag;                          // Tflush
	String8 error_message;                   // Rerror
	Qid qid;                                 // Rattach, Ropen, Rcreate
	u32 io_unit_size;                        // Ropen, Rcreate
	Qid auth_qid;                            // Rauth
	u32 auth_fid;                            // Tauth, Tattach
	String8 user_name;                       // Tauth, Tattach
	String8 attach_path;                     // Tauth, Tattach
	u32 permissions;                         // Tcreate
	String8 name;                            // Tcreate
	u32 open_mode;                           // Topen, Tcreate
	u32 new_fid;                             // Twalk
	u32 walk_name_count;                     // Twalk
	String8 walk_names[MAX_WALK_ELEM_COUNT]; // Twalk
	u32 walk_qid_count;                      // Rwalk
	Qid walk_qids[MAX_WALK_ELEM_COUNT];      // Rwalk
	u64 file_offset;                         // Tread, Twrite
	u32 byte_count;                          // Tread, Rread, Twrite, Rwrite
	String8 payload_data;                    // Rread, Twrite
	String8 stat_data;                       // Rstat, Twstat
};

// 9P Directory Entry
typedef struct Dir Dir;
struct Dir
{
	u32 server_type;
	u32 server_dev;
	Qid qid;
	u32 mode;
	u32 access_time;
	u32 modify_time;
	u64 length;
	String8 name;
	String8 user_id;
	String8 group_id;
	String8 modify_user_id;
};

// Dir List Types
typedef struct DirNode DirNode;
struct DirNode
{
	DirNode *next;
	Dir dir;
};

typedef struct DirList DirList;
struct DirList
{
	u64 count;
	DirNode *first;
	DirNode *last;
};

// 9P Message Types
typedef u32 Message9PType;
enum
{
	Msg9P_Tversion = 100,
	Msg9P_Rversion = 101,
	Msg9P_Tauth = 102,
	Msg9P_Rauth = 103,
	Msg9P_Tattach = 104,
	Msg9P_Rattach = 105,
	Msg9P_Rerror = 107,
	Msg9P_Tflush = 108,
	Msg9P_Rflush = 109,
	Msg9P_Twalk = 110,
	Msg9P_Rwalk = 111,
	Msg9P_Topen = 112,
	Msg9P_Ropen = 113,
	Msg9P_Tcreate = 114,
	Msg9P_Rcreate = 115,
	Msg9P_Tread = 116,
	Msg9P_Rread = 117,
	Msg9P_Twrite = 118,
	Msg9P_Rwrite = 119,
	Msg9P_Tclunk = 120,
	Msg9P_Rclunk = 121,
	Msg9P_Tremove = 122,
	Msg9P_Rremove = 123,
	Msg9P_Tstat = 124,
	Msg9P_Rstat = 125,
	Msg9P_Twstat = 126,
	Msg9P_Rwstat = 127,
};

// 9P Open Flags
typedef u32 OpenFlags;
enum
{
	OpenFlag_Read = 0,
	OpenFlag_Write = 1,
	OpenFlag_ReadWrite = 2,
	OpenFlag_Execute = 3,
	OpenFlag_Truncate = 16,
};

// 9P Access Flags
typedef u32 AccessFlags;
enum
{
	AccessFlag_Exist = 0,
	AccessFlag_Execute = 1,
	AccessFlag_Write = 2,
	AccessFlag_Read = 4,
};

// 9P Mode Flags
typedef u32 ModeFlags;
enum
{
	ModeFlag_Directory = 0x80000000,
};

// 9P Seek Whence
typedef u32 SeekWhence;
enum
{
	SeekWhence_Set = 0,
	SeekWhence_Cur = 1,
	SeekWhence_End = 2,
};

// 9P Protocol Encoding/Decoding Helpers
static u8 *encode_str8(u8 *ptr, String8 s);
static u8 *encode_qid(u8 *ptr, Qid qid);
static u8 *decode_str8(u8 *ptr, u8 *end, String8 *s);
static u8 *decode_qid(u8 *ptr, u8 *end, Qid *qid);

// 9P Message Encoding/Decoding
static u32 msg9p_size(Message9P msg);
static String8 str8_from_msg9p(Arena *arena, Message9P msg);
static Message9P msg9p_from_str8(String8 data);

// 9P Directory Encoding/Decoding
static u32 dir_size(Dir dir);
static String8 str8_from_dir(Arena *arena, Dir dir);
static Dir dir_from_str8(String8 data);

// 9P Directory List Operations
static void dir_list_push(Arena *arena, DirList *list, Dir dir);

// 9P Message I/O
static String8 read_9p_msg(Arena *arena, u64 fd);

#endif // _9P_CORE_H
