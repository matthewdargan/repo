#ifndef _9P_SERVER_H
#define _9P_SERVER_H

////////////////////////////////
//~ Server Types

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
};

////////////////////////////////
//~ Qid Type Flags

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

////////////////////////////////
//~ Request Management

internal ServerRequest9P *server9p_request_alloc(Server9P *server, u32 tag);
internal ServerRequest9P *server9p_request_remove(Server9P *server, u32 tag);

////////////////////////////////
//~ Server Lifecycle

internal Server9P *server9p_alloc(Arena *arena, u64 input_fd, u64 output_fd);

////////////////////////////////
//~ Request Handling

internal ServerRequest9P *server9p_get_request(Server9P *server);
internal b32 server9p_respond(ServerRequest9P *request, String8 err);

////////////////////////////////
//~ Fid Management

internal ServerFid9P *server9p_fid_alloc(Server9P *server, u32 fid);
internal ServerFid9P *server9p_fid_lookup(Server9P *server, u32 fid);
internal ServerFid9P *server9p_fid_remove(Server9P *server, u32 fid);

#endif // _9P_SERVER_H
