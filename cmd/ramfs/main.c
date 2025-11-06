// clang-format off
#include "base/inc.h"
#include "9p/fcall.h"
#include "9p/srv.h"
#include "base/inc.c"
#include "9p/fcall.c"
#include "9p/srv.c"
// clang-format on

typedef struct Ramfile Ramfile;
struct Ramfile
{
	String8 data;
	Arena *arena;
};

typedef struct Ramentry Ramentry;
struct Ramentry
{
	String8 name;
	u64 qid_path;
	Ramfile *file;
	Ramentry *next;
};

static Ramentry *filelist = 0;
static u64 next_qid_path  = 1;

static Ramentry *
findfile(String8 name)
{
	for (Ramentry *e = filelist; e != 0; e = e->next)
	{
		if (str8_match(e->name, name, 0))
		{
			return e;
		}
	}
	return 0;
}

static Ramentry *
addfile(Arena *arena, String8 name)
{
	Ramentry *e = push_array(arena, Ramentry, 1);
	e->name     = str8_copy(arena, name);
	e->qid_path = next_qid_path++;
	e->file     = 0;
	e->next     = filelist;
	filelist    = e;
	return e;
}

static void
ramfs_read(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == 0 && (r->fid->qid.type & QTDIR))
	{
		String8 dirdata = str8_zero();
		u64 pos         = 0;
		time_t now      = os_now_unix();
		for (Ramentry *file = filelist; file != 0; file = file->next)
		{
			if (pos >= r->ifcall.offset)
			{
				Dir d         = {0};
				d.qid         = (Qid){.path = file->qid_path, .vers = 0, .type = QTFILE};
				d.mode        = 0644;
				d.name        = file->name;
				d.uid         = str8_lit("ramfs");
				d.gid         = str8_lit("ramfs");
				d.muid        = str8_lit("ramfs");
				d.atime       = now;
				d.mtime       = now;
				d.len         = file->file ? file->file->data.size : 0;
				String8 entry = direncode(r->srv->arena, d);
				u64 entrylen  = entry.size;
				if (dirdata.size + entrylen > r->ifcall.count)
				{
					break;
				}
				if (dirdata.size == 0)
				{
					dirdata = entry;
				}
				else
				{
					u8 *newdata = push_array(r->srv->arena, u8, dirdata.size + entrylen);
					MemoryCopy(newdata, dirdata.str, dirdata.size);
					MemoryCopy(newdata + dirdata.size, entry.str, entrylen);
					dirdata.str  = newdata;
					dirdata.size = dirdata.size + entrylen;
				}
			}
			pos += 1;
		}
		r->ofcall.data = dirdata;
		respond(r, str8_zero());
		return;
	}
	if (e == 0 || e->file == 0)
	{
		r->ofcall.data = str8_zero();
		respond(r, str8_zero());
		return;
	}
	Ramfile *rf = e->file;
	u64 offset  = r->ifcall.offset;
	u64 count   = r->ifcall.count;
	if (offset >= rf->data.size)
	{
		r->ofcall.data = str8_zero();
		respond(r, str8_zero());
		return;
	}
	if (offset + count > rf->data.size)
	{
		count = rf->data.size - offset;
	}
	r->rbuf = push_array(r->srv->arena, u8, count);
	MemoryCopy(r->rbuf, rf->data.str + offset, count);
	r->ofcall.data = str8(r->rbuf, count);
	respond(r, str8_zero());
}

static void
ramfs_write(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == 0 || e->file == 0)
	{
		respond(r, str8_lit("no file data"));
		return;
	}
	Ramfile *rf = e->file;
	u64 offset  = r->ifcall.offset;
	u64 count   = r->ifcall.data.size;
	if (offset + count > rf->data.size)
	{
		u64 newsize = offset + count;
		u8 *newdata = push_array(rf->arena, u8, newsize);
		if (rf->data.size > 0)
		{
			MemoryCopy(newdata, rf->data.str, rf->data.size);
		}
		rf->data.str  = newdata;
		rf->data.size = newsize;
	}
	MemoryCopy(rf->data.str + offset, r->ifcall.data.str, count);
	r->ofcall.count = count;
	respond(r, str8_zero());
}

static void
ramfs_create(Req *r)
{
	Ramentry *e = findfile(r->ifcall.name);
	if (e != 0)
	{
		respond(r, str8_lit("file exists"));
		return;
	}
	Ramfile *rf      = push_array(r->srv->arena, Ramfile, 1);
	rf->arena        = r->srv->arena;
	rf->data         = str8_zero();
	e                = addfile(r->srv->arena, r->ifcall.name);
	e->file          = rf;
	r->fid->aux      = e;
	r->fid->qid.path = e->qid_path;
	r->fid->qid.vers = 0;
	r->fid->qid.type = QTFILE;
	r->ofcall.qid    = r->fid->qid;
	respond(r, str8_zero());
}

static void
ramfs_attach(Req *r)
{
	r->fid->qid.path = 0;
	r->fid->qid.vers = 0;
	r->fid->qid.type = QTDIR;
	r->ofcall.qid    = r->fid->qid;
	respond(r, str8_zero());
}

static void
ramfs_walk(Req *r)
{
	if (r->ifcall.nwname == 0)
	{
		r->newfid->qid  = r->fid->qid;
		r->ofcall.nwqid = 0;
		respond(r, str8_zero());
		return;
	}
	for (u64 i = 0; i < r->ifcall.nwname; i++)
	{
		if (!(r->fid->qid.type & QTDIR))
		{
			respond(r, str8_lit("not a directory"));
			return;
		}
		String8 name = r->ifcall.wname[i];
		Ramentry *e  = findfile(name);
		if (e == 0)
		{
			respond(r, str8_lit("file not found"));
			return;
		}
		r->newfid->qid.path = e->qid_path;
		r->newfid->qid.vers = 0;
		r->newfid->qid.type = QTFILE;
		r->newfid->aux      = e;
		r->ofcall.wqid[i]   = r->newfid->qid;
	}
	r->ofcall.nwqid = r->ifcall.nwname;
	respond(r, str8_zero());
}

static void
ramfs_stat(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == 0)
	{
		time_t now     = os_now_unix();
		Dir d          = {0};
		d.qid          = r->fid->qid;
		d.mode         = DMDIR | 0755;
		d.name         = str8_copy(r->srv->arena, str8_lit("."));
		d.uid          = str8_copy(r->srv->arena, str8_lit("ramfs"));
		d.gid          = str8_copy(r->srv->arena, str8_lit("ramfs"));
		d.muid         = str8_copy(r->srv->arena, str8_lit("ramfs"));
		d.atime        = now;
		d.mtime        = now;
		r->ofcall.stat = direncode(r->srv->arena, d);
	}
	else
	{
		time_t now     = os_now_unix();
		Dir d          = {0};
		d.qid          = r->fid->qid;
		d.mode         = 0644;
		d.name         = str8_copy(r->srv->arena, e->name);
		d.uid          = str8_copy(r->srv->arena, str8_lit("ramfs"));
		d.gid          = str8_copy(r->srv->arena, str8_lit("ramfs"));
		d.muid         = str8_copy(r->srv->arena, str8_lit("ramfs"));
		d.atime        = now;
		d.mtime        = now;
		d.len          = e->file ? e->file->data.size : 0;
		r->ofcall.stat = direncode(r->srv->arena, d);
	}
	respond(r, str8_zero());
}

static void
ramfs_remove(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == 0)
	{
		respond(r, str8_lit("cannot remove root directory"));
		return;
	}
	if (filelist == e)
	{
		filelist = e->next;
	}
	else
	{
		for (Ramentry *prev = filelist; prev != 0; prev = prev->next)
		{
			if (prev->next == e)
			{
				prev->next = e->next;
				break;
			}
		}
	}
	respond(r, str8_zero());
}

static void
ramfs_open(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == 0)
	{
		if ((r->ifcall.mode & 3) == OREAD)
		{
			r->ofcall.qid = r->fid->qid;
			respond(r, str8_zero());
			return;
		}
		else
		{
			respond(r, str8_lit("cannot write to root directory"));
			return;
		}
	}
	Ramfile *rf = e->file;
	if (rf == 0)
	{
		rf        = push_array(r->srv->arena, Ramfile, 1);
		rf->arena = r->srv->arena;
		rf->data  = str8_zero();
		e->file   = rf;
	}
	if ((r->ifcall.mode & OTRUNC) && rf->data.size > 0)
	{
		rf->data = str8_zero();
	}
	r->ofcall.qid = r->fid->qid;
	respond(r, str8_zero());
}

static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log     = log_alloc();
	log_select(log);
	log_scope_begin();
	Arena *arena    = arena_alloc();
	String8 srvname = str8_zero();
	String8 address = str8_zero();

	if (cmd_line_has_argument(cmd_line, str8_lit("s")))
	{
		srvname = cmd_line_string(cmd_line, str8_lit("s"));
	}
	if (cmd_line_has_argument(cmd_line, str8_lit("a")))
	{
		address = cmd_line_string(cmd_line, str8_lit("a"));
	}
	if (srvname.size == 0 && address.size == 0)
	{
		log_error(str8_lit("usage: ramfs [-s srvname] [-a address]\n"));
	}
	else
	{
		if (address.size > 0)
		{
			Netaddr na = netaddr(arena, address, str8_lit("tcp"), str8_lit("9pfs"));
			if (na.net.size == 0)
			{
				log_errorf("ramfs: failed to parse address '%S'\n", address);
			}
			else
			{
				String8 portstr = str8f(arena, "%llu", na.port);
				u64 listenfd    = socketlisten(portstr, 0);
				if (listenfd == 0)
				{
					log_errorf("ramfs: failed to listen on port %S\n", portstr);
				}
				else
				{
					log_infof("ramfs: listening on %S (port %S)\n", address, portstr);
					for (;;)
					{
						u64 connfd = socketaccept(listenfd);
						if (connfd == 0)
						{
							log_error(str8_lit("ramfs: failed to accept connection\n"));
							continue;
						}
						log_info(str8_lit("ramfs: accepted connection\n"));
						u64 infd  = connfd;
						u64 outfd = connfd;
						Srv *srv  = srvalloc(arena, infd, outfd);
						if (srv == 0)
						{
							log_error(str8_lit("ramfs: failed to allocate server\n"));
							close(connfd);
							continue;
						}
						srv->attach = ramfs_attach;
						srv->read   = ramfs_read;
						srv->write  = ramfs_write;
						srv->create = ramfs_create;
						srv->open   = ramfs_open;
						srv->walk   = ramfs_walk;
						srv->stat   = ramfs_stat;
						srv->remove = ramfs_remove;
						srvrun(srv);
						srvfree(srv);
						close(connfd);
						log_info(str8_lit("ramfs: connection closed\n"));
					}
				}
			}
		}
		else
		{
			u64 infd  = STDIN_FILENO;
			u64 outfd = STDOUT_FILENO;
			Srv *srv  = srvalloc(arena, infd, outfd);
			if (srv == 0)
			{
				log_error(str8_lit("ramfs: failed to allocate server\n"));
			}
			else
			{
				srv->attach = ramfs_attach;
				srv->read   = ramfs_read;
				srv->write  = ramfs_write;
				srv->create = ramfs_create;
				srv->open   = ramfs_open;
				srv->walk   = ramfs_walk;
				srv->stat   = ramfs_stat;
				srv->remove = ramfs_remove;
				srvrun(srv);
				srvfree(srv);
			}
		}
	}
	arena_release(arena);

	LogScopeResult result = log_scope_end(scratch.arena);
	if (result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}
	if (result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}
	scratch_end(scratch);
}
