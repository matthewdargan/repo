#ifndef _9P_SERVER_H
#define _9P_SERVER_H

// Server Types
typedef struct ServerFid9P ServerFid9P;
struct ServerFid9P
{
	u32 fid;
	u32 open_mode;
	Qid qid;
	String8 user_id;
	u64 offset;
	void *auxiliary;
	struct Server9P *server;
};

typedef struct ServerRequest9P ServerRequest9P;
struct ServerRequest9P
{
	u32 tag;
	u32 responded;
	Message9P in_msg;
	Message9P out_msg;
	ServerFid9P *fid;
	ServerFid9P *new_fid;
	ServerFid9P *auth_fid;
	ServerRequest9P *old_request;
	ServerRequest9P **flush;
	u32 flush_count;
	String8 error;
	u8 *buffer;
	u8 *read_buffer;
	void *auxiliary;
	struct Server9P *server;
};

typedef struct Server9P Server9P;
struct Server9P
{
	Arena *arena;
	u64 input_fd;
	u64 output_fd;
	u32 max_message_size;
	u8 *read_buffer;
	u8 *write_buffer;
	ServerFid9P **fid_table;
	u32 fid_count;
	u32 max_fid_count;
	ServerRequest9P **request_table;
	u32 request_count;
	u32 max_request_count;
	u32 next_tag;
	void *auxiliary;
	void (*start)(Server9P *);
	void (*end)(Server9P *);
	void (*auth)(ServerRequest9P *);
	void (*attach)(ServerRequest9P *);
	void (*flush)(ServerRequest9P *);
	void (*walk)(ServerRequest9P *);
	void (*open)(ServerRequest9P *);
	void (*create)(ServerRequest9P *);
	void (*read)(ServerRequest9P *);
	void (*write)(ServerRequest9P *);
	void (*stat)(ServerRequest9P *);
	void (*wstat)(ServerRequest9P *);
	void (*remove)(ServerRequest9P *);
	void (*destroy_fid)(ServerFid9P *);
	void (*destroy_request)(ServerRequest9P *);
};

// Qid Type Flags
typedef u32 QidTypeFlags;
enum
{
	QidTypeFlag_Directory = 0x80,
	QidTypeFlag_Append = 0x40,
	QidTypeFlag_Exclusive = 0x20,
	QidTypeFlag_Mount = 0x10,
	QidTypeFlag_Auth = 0x08,
	QidTypeFlag_Temporary = 0x04,
	QidTypeFlag_Symlink = 0x02,
	QidTypeFlag_File = 0x00,
};

// Server Error Messages
read_only static String8 Ebadoffset = str8_lit_comp("bad offset");
read_only static String8 Ebotch = str8_lit_comp("9P protocol botch");
read_only static String8 Ecreatenondir = str8_lit_comp("create in non-directory");
read_only static String8 Edupfid = str8_lit_comp("duplicate fid");
read_only static String8 Eduptag = str8_lit_comp("duplicate tag");
read_only static String8 Eisdir = str8_lit_comp("is a directory");
read_only static String8 Enocreate = str8_lit_comp("create prohibited");
read_only static String8 Enoremove = str8_lit_comp("remove prohibited");
read_only static String8 Enostat = str8_lit_comp("stat prohibited");
read_only static String8 Enotfound = str8_lit_comp("file not found");
read_only static String8 Enowstat = str8_lit_comp("wstat prohibited");
read_only static String8 Eperm = str8_lit_comp("permission denied");
read_only static String8 Eunknownfid = str8_lit_comp("unknown fid");
read_only static String8 Ewalknodir = str8_lit_comp("walk in non-directory");

// Server Lifecycle
static Server9P *server9p_alloc(Arena *arena, u64 input_fd, u64 output_fd);
static void server9p_free(Server9P *server);
static void server9p_run(Server9P *server);

// Request Handling
static void server9p_respond(ServerRequest9P *request, String8 err);

// Fid Management
static ServerFid9P *server9p_fid_alloc(Server9P *server, u32 fid);
static ServerFid9P *server9p_fid_lookup(Server9P *server, u32 fid);
static ServerFid9P *server9p_fid_remove(Server9P *server, u32 fid);
static void server9p_fid_close(ServerFid9P *fid);

// Request Management
static ServerRequest9P *server9p_request_alloc(Server9P *server, u32 tag);
static ServerRequest9P *server9p_request_lookup(Server9P *server, u32 tag);
static ServerRequest9P *server9p_request_remove(Server9P *server, u32 tag);
static void server9p_request_close(ServerRequest9P *request);

#endif // _9P_SERVER_H
