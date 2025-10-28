#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

/* clang-format off */
#include "libu/u.h"
#include "libu/arena.h"
#include "libu/string.h"
#include "libu/cmd.h"
#include "libu/os.h"
#include "libu/socket.h"
#include "lib9p/fcall.h"
#include "lib9p/srv.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/cmd.c"
#include "libu/os.c"
#include "libu/socket.c"
#include "lib9p/fcall.c"
#include "lib9p/srv.c"
/* clang-format on */

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

static Ramentry *filelist = NULL;
static u64 next_qid_path = 1;

static Ramentry *
findfile(String8 name)
{
	for (Ramentry *e = filelist; e != NULL; e = e->next)
	{
		if (str8cmp(e->name, name, 0))
		{
			return e;
		}
	}
	return NULL;
}

static Ramentry *
addfile(Arena *arena, String8 name)
{
	Ramentry *e = push_array(arena, Ramentry, 1);
	e->name = pushstr8cpy(arena, name);
	e->qid_path = next_qid_path++;
	e->file = NULL;
	e->next = filelist;
	filelist = e;
	return e;
}

static void
ramfs_read(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == NULL && (r->fid->qid.type & QTDIR))
	{
		String8 dirdata = str8zero();
		u64 pos = 0;
		time_t now = nowunix();
		for (Ramentry *file = filelist; file != NULL; file = file->next)
		{
			if (pos >= r->ifcall.offset)
			{
				Dir d = {0};
				d.qid = (Qid){.path = file->qid_path, .vers = 0, .type = QTFILE};
				d.mode = 0644;
				d.name = file->name;
				d.uid = str8lit("ramfs");
				d.gid = str8lit("ramfs");
				d.muid = str8lit("ramfs");
				d.atime = now;
				d.mtime = now;
				d.len = file->file ? file->file->data.len : 0;
				String8 entry = direncode(r->srv->arena, d);
				u64 entrylen = entry.len;
				if (dirdata.len + entrylen > r->ifcall.count)
				{
					break;
				}
				if (dirdata.len == 0)
				{
					dirdata = entry;
				}
				else
				{
					u8 *newdata = push_array(r->srv->arena, u8, dirdata.len + entrylen);
					memcpy(newdata, dirdata.str, dirdata.len);
					memcpy(newdata + dirdata.len, entry.str, entrylen);
					dirdata.str = newdata;
					dirdata.len = dirdata.len + entrylen;
				}
			}
			pos += 1;
		}
		r->ofcall.data = dirdata;
		respond(r, str8zero());
		return;
	}
	if (e == NULL || e->file == NULL)
	{
		r->ofcall.data = str8zero();
		respond(r, str8zero());
		return;
	}
	Ramfile *rf = e->file;
	u64 offset = r->ifcall.offset;
	u64 count = r->ifcall.count;
	if (offset >= rf->data.len)
	{
		r->ofcall.data = str8zero();
		respond(r, str8zero());
		return;
	}
	if (offset + count > rf->data.len)
	{
		count = rf->data.len - offset;
	}
	r->rbuf = push_array(r->srv->arena, u8, count);
	memcpy(r->rbuf, rf->data.str + offset, count);
	r->ofcall.data = str8(r->rbuf, count);
	respond(r, str8zero());
}

static void
ramfs_write(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == NULL || e->file == NULL)
	{
		respond(r, str8lit("no file data"));
		return;
	}
	Ramfile *rf = e->file;
	u64 offset = r->ifcall.offset;
	u64 count = r->ifcall.data.len;
	if (offset + count > rf->data.len)
	{
		u64 newsize = offset + count;
		u8 *newdata = push_array(rf->arena, u8, newsize);
		if (rf->data.len > 0)
		{
			memcpy(newdata, rf->data.str, rf->data.len);
		}
		rf->data.str = newdata;
		rf->data.len = newsize;
	}
	memcpy(rf->data.str + offset, r->ifcall.data.str, count);
	r->ofcall.count = count;
	respond(r, str8zero());
}

static void
ramfs_create(Req *r)
{
	Ramentry *e = findfile(r->ifcall.name);
	if (e != NULL)
	{
		respond(r, str8lit("file exists"));
		return;
	}
	Ramfile *rf = push_array(r->srv->arena, Ramfile, 1);
	rf->arena = r->srv->arena;
	rf->data = str8zero();
	e = addfile(r->srv->arena, r->ifcall.name);
	e->file = rf;
	r->fid->aux = e;
	r->fid->qid.path = e->qid_path;
	r->fid->qid.vers = 0;
	r->fid->qid.type = QTFILE;
	r->ofcall.qid = r->fid->qid;
	respond(r, str8zero());
}

static void
ramfs_attach(Req *r)
{
	r->fid->qid.path = 0;
	r->fid->qid.vers = 0;
	r->fid->qid.type = QTDIR;
	r->ofcall.qid = r->fid->qid;
	respond(r, str8zero());
}

static void
ramfs_walk(Req *r)
{
	if (r->ifcall.nwname == 0)
	{
		r->newfid->qid = r->fid->qid;
		r->ofcall.nwqid = 0;
		respond(r, str8zero());
		return;
	}
	for (u64 i = 0; i < r->ifcall.nwname; i++)
	{
		if (!(r->fid->qid.type & QTDIR))
		{
			respond(r, str8lit("not a directory"));
			return;
		}
		String8 name = r->ifcall.wname[i];
		Ramentry *e = findfile(name);
		if (e == NULL)
		{
			respond(r, str8lit("file not found"));
			return;
		}
		r->newfid->qid.path = e->qid_path;
		r->newfid->qid.vers = 0;
		r->newfid->qid.type = QTFILE;
		r->newfid->aux = e;
		r->ofcall.wqid[i] = r->newfid->qid;
	}
	r->ofcall.nwqid = r->ifcall.nwname;
	respond(r, str8zero());
}

static void
ramfs_stat(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == NULL)
	{
		time_t now = nowunix();
		Dir d = {0};
		d.qid = r->fid->qid;
		d.mode = DMDIR | 0755;
		d.name = pushstr8cpy(r->srv->arena, str8lit("."));
		d.uid = pushstr8cpy(r->srv->arena, str8lit("ramfs"));
		d.gid = pushstr8cpy(r->srv->arena, str8lit("ramfs"));
		d.muid = pushstr8cpy(r->srv->arena, str8lit("ramfs"));
		d.atime = now;
		d.mtime = now;
		r->ofcall.stat = direncode(r->srv->arena, d);
	}
	else
	{
		time_t now = nowunix();
		Dir d = {0};
		d.qid = r->fid->qid;
		d.mode = 0644;
		d.name = pushstr8cpy(r->srv->arena, e->name);
		d.uid = pushstr8cpy(r->srv->arena, str8lit("ramfs"));
		d.gid = pushstr8cpy(r->srv->arena, str8lit("ramfs"));
		d.muid = pushstr8cpy(r->srv->arena, str8lit("ramfs"));
		d.atime = now;
		d.mtime = now;
		d.len = e->file ? e->file->data.len : 0;
		r->ofcall.stat = direncode(r->srv->arena, d);
	}
	respond(r, str8zero());
}

static void
ramfs_remove(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == NULL)
	{
		respond(r, str8lit("cannot remove root directory"));
		return;
	}
	if (filelist == e)
	{
		filelist = e->next;
	}
	else
	{
		for (Ramentry *prev = filelist; prev != NULL; prev = prev->next)
		{
			if (prev->next == e)
			{
				prev->next = e->next;
				break;
			}
		}
	}
	respond(r, str8zero());
}

static void
ramfs_open(Req *r)
{
	Ramentry *e = r->fid->aux;
	if (e == NULL)
	{
		if ((r->ifcall.mode & 3) == OREAD)
		{
			r->ofcall.qid = r->fid->qid;
			respond(r, str8zero());
			return;
		}
		else
		{
			respond(r, str8lit("cannot write to root directory"));
			return;
		}
	}
	Ramfile *rf = e->file;
	if (rf == NULL)
	{
		rf = push_array(r->srv->arena, Ramfile, 1);
		rf->arena = r->srv->arena;
		rf->data = str8zero();
		e->file = rf;
	}
	if ((r->ifcall.mode & OTRUNC) && rf->data.len > 0)
	{
		rf->data = str8zero();
	}
	r->ofcall.qid = r->fid->qid;
	respond(r, str8zero());
}

int
main(int argc, char *argv[])
{
	sysinfo = (Sysinfo){.nprocs = sysconf(_SC_NPROCESSORS_ONLN), .pagesz = sysconf(_SC_PAGESIZE), .lpagesz = 0x200000};
	Arena *arena = arena_alloc();
	String8list args = os_args(arena, argc, argv);
	Cmd parsed = cmdparse(arena, args);
	String8 srvname = str8zero();
	String8 address = str8zero();
	if (cmdhasarg(&parsed, str8lit("s")))
	{
		srvname = cmdstr(&parsed, str8lit("s"));
	}
	if (cmdhasarg(&parsed, str8lit("a")))
	{
		address = cmdstr(&parsed, str8lit("a"));
	}
	if (srvname.len == 0 && address.len == 0)
	{
		fprintf(stderr, "usage: ramfs [-s srvname] [-a address]\n");
		return 1;
	}
	if (address.len > 0)
	{
		Netaddr na = netaddr(arena, address, str8lit("tcp"), str8lit("9pfs"));
		if (na.net.len == 0)
		{
			fprintf(stderr, "ramfs: failed to parse address '%.*s'\n", str8varg(address));
			arena_release(arena);
			return 1;
		}
		String8 portstr = pushstr8f(arena, "%llu", na.port);
		u64 listenfd = socketlisten(portstr, NULL);
		if (listenfd == 0)
		{
			fprintf(stderr, "ramfs: failed to listen on port %.*s\n", str8varg(portstr));
			arena_release(arena);
			return 1;
		}
		printf("ramfs: listening on %.*s (port %.*s)\n", str8varg(address), str8varg(portstr));
		for (;;)
		{
			u64 connfd = socketaccept(listenfd);
			if (connfd == 0)
			{
				fprintf(stderr, "ramfs: failed to accept connection\n");
				continue;
			}
			printf("ramfs: accepted connection\n");
			u64 infd = connfd;
			u64 outfd = connfd;
			Srv *srv = srvalloc(arena, infd, outfd);
			if (srv == NULL)
			{
				fprintf(stderr, "ramfs: failed to allocate server\n");
				close(connfd);
				continue;
			}
			srv->attach = ramfs_attach;
			srv->read = ramfs_read;
			srv->write = ramfs_write;
			srv->create = ramfs_create;
			srv->open = ramfs_open;
			srv->walk = ramfs_walk;
			srv->stat = ramfs_stat;
			srv->remove = ramfs_remove;
			srvrun(srv);
			srvfree(srv);
			close(connfd);
			printf("ramfs: connection closed\n");
		}
	}
	else
	{
		u64 infd = STDIN_FILENO;
		u64 outfd = STDOUT_FILENO;
		Srv *srv = srvalloc(arena, infd, outfd);
		if (srv == NULL)
		{
			fprintf(stderr, "ramfs: failed to allocate server\n");
			arena_release(arena);
			return 1;
		}
		srv->attach = ramfs_attach;
		srv->read = ramfs_read;
		srv->write = ramfs_write;
		srv->create = ramfs_create;
		srv->open = ramfs_open;
		srv->walk = ramfs_walk;
		srv->stat = ramfs_stat;
		srv->remove = ramfs_remove;
		srvrun(srv);
		srvfree(srv);
	}
	arena_release(arena);
	return 0;
}
