#ifndef _9P_CORE_H
#define _9P_CORE_H

// Protocol Constants
read_only static String8 version_9p = str8_lit_comp("9P2000");

// Encoding Sizes
#define P9_MESSAGE_SIZE_FIELD_SIZE 4
#define P9_MESSAGE_TYPE_FIELD_SIZE 1
#define P9_MESSAGE_TAG_FIELD_SIZE 2
#define P9_MESSAGE_MINIMUM_SIZE (P9_MESSAGE_SIZE_FIELD_SIZE + P9_MESSAGE_TYPE_FIELD_SIZE + P9_MESSAGE_TAG_FIELD_SIZE)
#define P9_QID_ENCODED_SIZE 13
#define P9_STRING8_SIZE_FIELD_SIZE 2

// Protocol Limits
#define P9_MAX_WALK_ELEM_COUNT 16
#define P9_STAT_DATA_FIXED_SIZE (2 + 2 + 4 + 1 + 4 + 8 + 4 + 4 + 4 + 8)
#define P9_TAG_NONE max_u16
#define P9_FID_NONE max_u32
#define P9_OPEN_MODE_NONE max_u32
#define P9_MESSAGE_HEADER_SIZE 24
#define P9_IOUNIT_DEFAULT 8192
#define P9_DIR_ENTRY_MAX 8192
#define P9_DIR_BUFFER_MAX (P9_DIR_ENTRY_MAX * 16)

// Protocol Message Types
typedef struct Qid Qid;
struct Qid
{
	u32 type;
	u32 version;
	u64 path;
};

typedef struct Message9P Message9P;
struct Message9P
{
	u32 type;
	u32 tag;
	u32 fid;
	u32 max_message_size;                       // Tversion, Rversion
	String8 protocol_version;                   // Tversion, Rversion
	u32 cancel_tag;                             // Tflush
	String8 error_message;                      // Rerror
	Qid qid;                                    // Rattach, Ropen, Rcreate
	u32 io_unit_size;                           // Ropen, Rcreate
	Qid auth_qid;                               // Rauth
	u32 auth_fid;                               // Tauth, Tattach
	String8 user_name;                          // Tauth, Tattach
	String8 attach_path;                        // Tauth, Tattach
	u32 permissions;                            // Tcreate
	String8 name;                               // Tcreate
	u32 open_mode;                              // Topen, Tcreate
	u32 new_fid;                                // Twalk
	u32 walk_name_count;                        // Twalk
	String8 walk_names[P9_MAX_WALK_ELEM_COUNT]; // Twalk
	u32 walk_qid_count;                         // Rwalk
	Qid walk_qids[P9_MAX_WALK_ELEM_COUNT];      // Rwalk
	u64 file_offset;                            // Tread, Twrite
	u32 byte_count;                             // Tread, Rread, Twrite, Rwrite
	String8 payload_data;                       // Rread, Twrite
	String8 stat_data;                          // Rstat, Twstat
};

typedef struct Dir9P Dir9P;
struct Dir9P
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

typedef struct DirNode9P DirNode9P;
struct DirNode9P
{
	DirNode9P *next;
	Dir9P dir;
};

typedef struct DirList9P DirList9P;
struct DirList9P
{
	u64 count;
	DirNode9P *first;
	DirNode9P *last;
};

// Message Type Codes
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

// Open Flags
typedef u32 P9_OpenFlags;
enum
{
	P9_OpenFlag_Read = 0,
	P9_OpenFlag_Write = 1,
	P9_OpenFlag_ReadWrite = 2,
	P9_OpenFlag_Execute = 3,
	P9_OpenFlag_Truncate = 16,
};

// Access Flags
typedef u32 P9_AccessFlags;
enum
{
	P9_AccessFlag_Exist = 0,
	P9_AccessFlag_Execute = 1,
	P9_AccessFlag_Write = 2,
	P9_AccessFlag_Read = 4,
};

// Mode Flags
typedef u32 P9_ModeFlags;
enum
{
	P9_ModeFlag_Directory = 0x80000000,
};

// Seek Whence
typedef u32 P9_SeekWhence;
enum
{
	P9_SeekWhence_Set = 0,
	P9_SeekWhence_Cur = 1,
	P9_SeekWhence_End = 2,
};

// Type Constructors
static Message9P msg9p_zero(void);
static Dir9P dir9p_zero(void);

// Encoding/Decoding Helpers
static u8 *encode_str8(u8 *ptr, String8 string);
static u8 *encode_qid(u8 *ptr, Qid qid);
static u8 *decode_str8(u8 *ptr, u8 *end, String8 *out_string);
static u8 *decode_qid(u8 *ptr, u8 *end, Qid *out_qid);

// Message Encoding/Decoding
static u32 msg9p_size(Message9P msg);
static String8 str8_from_msg9p(Arena *arena, Message9P msg);
static Message9P msg9p_from_str8(String8 data);

// Message Formatting
static String8 str8_from_msg9p__fmt(Arena *arena, Message9P msg);

// Directory Encoding/Decoding
static u32 dir9p_size(Dir9P dir);
static String8 str8_from_dir9p(Arena *arena, Dir9P dir);
static Dir9P dir9p_from_str8(String8 data);

// Directory List Operations
static void dir9p_list_push(Arena *arena, DirList9P *list, Dir9P dir);

// Message I/O
static String8 read_9p_msg(Arena *arena, u64 fd);

#endif // _9P_CORE_H
