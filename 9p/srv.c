static void
setfcallerror(Fcall *f, String8 err)
{
	f->ename = err;
	f->type  = Rerror;
}

static void
changemsize(Srv *srv, u32 msize)
{
	if (srv->rbuf != 0 && srv->wbuf != 0 && srv->msize == msize)
	{
		return;
	}
	srv->msize = msize;
	srv->rbuf  = push_array(srv->arena, u8, msize);
	srv->wbuf  = push_array(srv->arena, u8, msize);
}

static Req *
getreq(Srv *srv)
{
	String8 msg = read9pmsg(srv->arena, srv->infd);
	if (msg.size <= 0)
	{
		return 0;
	}
	Fcall f = fcalldecode(msg);
	if (f.type == 0)
	{
		return 0;
	}
	Req *r = allocreq(srv, f.tag);
	if (r == 0)
	{
		r            = push_array(srv->arena, Req, 1);
		r->tag       = f.tag;
		r->ifcall    = f;
		r->error     = Eduptag;
		r->buf       = msg.str;
		r->responded = 0;
		r->srv       = srv;
		// TODO: logging
		return r;
	}
	r->srv       = srv;
	r->responded = 0;
	r->buf       = msg.str;
	r->ifcall    = f;
	r->ofcall    = (Fcall){0};
	// TODO: logging
	return r;
}

static void
sversion(Srv *srv, Req *r)
{
	if (!str8_match(r->ifcall.version, version9p, 0))
	{
		r->ofcall.version = str8_lit("unknown");
		respond(r, str8_zero());
		return;
	}
	r->ofcall.version = version9p;
	r->ofcall.msize   = r->ifcall.msize;
	respond(r, str8_zero());
}

static void
rversion(Req *r, String8 err)
{
	if (err.size > 0)
	{
		return;
	}
	changemsize(r->srv, r->ofcall.msize);
}

static void
sauth(Srv *srv, Req *r)
{
	r->afid = allocfid(srv, r->ifcall.afid);
	if (r->afid == 0)
	{
		respond(r, Edupfid);
		return;
	}
	if (srv->auth != 0)
	{
		srv->auth(r);
	}
	else
	{
		String8 err = str8f(srv->arena, "authentication not required");
		respond(r, err);
	}
}

static void
rauth(Req *r, String8 err)
{
	if (err.size > 0 && r->afid != 0)
	{
		closefid(removefid(r->srv, r->afid->fid));
	}
}

static void
sattach(Srv *srv, Req *r)
{
	r->fid = allocfid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Edupfid);
		return;
	}
	r->afid = 0;
	if (r->ifcall.afid != NOFID)
	{
		r->afid = lookupfid(srv, r->ifcall.afid);
		if (r->afid == 0)
		{
			respond(r, Eunknownfid);
			return;
		}
	}
	r->fid->uid = str8_copy(srv->arena, r->ifcall.uname);
	if (srv->attach != 0)
	{
		srv->attach(r);
	}
	else
	{
		respond(r, str8_zero());
	}
}

static void
rattach(Req *r, String8 err)
{
	if (err.size > 0 && r->fid != 0)
	{
		closefid(removefid(r->srv, r->fid->fid));
	}
}

static void
sflush(Srv *srv, Req *r)
{
	r->oldreq = lookupreq(srv, r->ifcall.oldtag);
	if (r->oldreq == 0 || r->oldreq == r)
	{
		respond(r, str8_zero());
	}
	else if (srv->flush != 0)
	{
		srv->flush(r);
	}
	else
	{
		respond(r, str8_zero());
	}
}

static b32
rflush(Req *r, String8 err)
{
	if (err.size > 0)
	{
		return 0;
	}
	Req *or = r->oldreq;
	if (or != 0)
	{
		if (or->responded == 0)
		{
			or->flush               = push_array(r->srv->arena, Req *, or->nflush + 1);
			or->flush[or->nflush++] = r;
			return 1;
		}
		closereq(or);
	}
	r->oldreq = 0;
	return 0;
}

static void
swalk(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if (r->fid->omode != ~0U)
	{
		respond(r, str8_lit("cannot clone open fid"));
		return;
	}
	if (r->ifcall.nwname && !(r->fid->qid.type & QTDIR))
	{
		respond(r, Ewalknodir);
		return;
	}
	if (r->ifcall.fid != r->ifcall.newfid)
	{
		r->newfid = allocfid(srv, r->ifcall.newfid);
		if (r->newfid == 0)
		{
			respond(r, Edupfid);
			return;
		}
		r->newfid->uid = str8_copy(srv->arena, r->fid->uid);
	}
	else
	{
		r->newfid = r->fid;
	}
	if (srv->walk != 0)
	{
		srv->walk(r);
	}
	else
	{
		respond(r, str8_lit("no walk function"));
	}
}

static void
rwalk(Req *r, String8 err)
{
	if (err.size > 0 || r->ofcall.nwqid < r->ifcall.nwname)
	{
		if (r->ifcall.fid != r->ifcall.newfid && r->newfid != 0)
		{
			closefid(removefid(r->srv, r->newfid->fid));
		}
		if (r->ofcall.nwqid == 0)
		{
			if (err.size == 0 && r->ifcall.nwname != 0)
			{
				r->error = Enotfound;
			}
		}
		else
		{
			r->error = str8_zero();
		}
	}
	else
	{
		if (r->ofcall.nwqid == 0)
		{
			r->newfid->qid = r->fid->qid;
		}
		else
		{
			r->newfid->qid = r->ofcall.wqid[r->ofcall.nwqid - 1];
		}
	}
}

static void
sopen(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if (r->fid->omode != ~0U)
	{
		respond(r, Ebotch);
		return;
	}
	if ((r->fid->qid.type & QTDIR) && (r->ifcall.mode & ~0x10) != OREAD)
	{
		respond(r, Eisdir);
		return;
	}
	r->ofcall.qid = r->fid->qid;
	u32 p         = 0;
	switch (r->ifcall.mode & 3)
	{
		case OREAD:
		{
			p = AREAD;
		}
		break;
		case OWRITE:
		{
			p = AWRITE;
		}
		break;
		case ORDWR:
		{
			p = AREAD | AWRITE;
		}
		break;
		case OEXEC:
		{
			p = AEXEC;
		}
		break;
		default:
		{
			respond(r, Ebotch);
			return;
		}
	}
	if (r->ifcall.mode & OTRUNC)
	{
		p |= AWRITE;
	}
	if ((r->fid->qid.type & QTDIR) && p != AREAD)
	{
		respond(r, Eperm);
		return;
	}
	if (srv->open != 0)
	{
		srv->open(r);
	}
	else
	{
		respond(r, str8_zero());
	}
}

static void
ropen(Req *r, String8 err)
{
	if (err.size > 0)
	{
		return;
	}
	r->fid->omode = r->ifcall.mode;
	r->fid->qid   = r->ofcall.qid;
	if (r->ofcall.qid.type & QTDIR)
	{
		r->fid->offset = 0;
	}
}

static void
screate(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Eunknownfid);
	}
	else if (r->fid->omode != ~0U)
	{
		respond(r, Ebotch);
	}
	else if (!(r->fid->qid.type & QTDIR))
	{
		respond(r, Ecreatenondir);
	}
	else if (srv->create != 0)
	{
		srv->create(r);
	}
	else
	{
		respond(r, Enocreate);
	}
}

static void
rcreate(Req *r, String8 err)
{
	if (err.size > 0)
	{
		return;
	}
	r->fid->omode = r->ifcall.mode;
	r->fid->qid   = r->ofcall.qid;
}

static void
sread(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if ((s32)r->ifcall.count < 0)
	{
		respond(r, Ebotch);
		return;
	}
	if (r->ifcall.offset < 0 ||
	    ((r->fid->qid.type & QTDIR) && r->ifcall.offset != 0 && r->ifcall.offset != r->fid->offset))
	{
		respond(r, Ebadoffset);
		return;
	}
	if (r->ifcall.count > srv->msize - IOHDRSZ)
	{
		r->ifcall.count = srv->msize - IOHDRSZ;
	}
	r->rbuf        = push_array(srv->arena, u8, r->ifcall.count);
	r->ofcall.data = str8(r->rbuf, 0);
	u32 o          = r->fid->omode & 3;
	if (o != OREAD && o != ORDWR && o != OEXEC)
	{
		respond(r, Ebotch);
		return;
	}
	if (srv->read != 0)
	{
		srv->read(r);
	}
	else
	{
		respond(r, str8_lit("no srv->read"));
	}
}

static void
rread(Req *r, String8 err)
{
	if (err.size == 0 && (r->fid->qid.type & QTDIR))
	{
		r->fid->offset = r->ifcall.offset + r->ofcall.count;
	}
}

static void
swrite(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if ((s32)r->ifcall.count < 0)
	{
		respond(r, Ebotch);
		return;
	}
	if (r->ifcall.offset < 0)
	{
		respond(r, Ebotch);
		return;
	}
	if (r->ifcall.count > srv->msize - IOHDRSZ)
	{
		r->ifcall.count = srv->msize - IOHDRSZ;
	}
	u32 o = r->fid->omode & 3;
	if (o != OWRITE && o != ORDWR)
	{
		String8 err = str8f(srv->arena, "write on fid with open mode 0x%x", r->fid->omode);
		respond(r, err);
		return;
	}
	if (srv->write != 0)
	{
		srv->write(r);
	}
	else
	{
		respond(r, str8_lit("no srv->write"));
	}
}

static void
rwrite(Req *r, String8 err)
{
	if (err.size > 0)
	{
		return;
	}
}

static void
sclunk(Srv *srv, Req *r)
{
	r->fid = removefid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Eunknownfid);
	}
	else
	{
		respond(r, str8_zero());
	}
}

static void
rclunk(Req *r, String8 err)
{
	(void)r;
	(void)err;
}

static void
sremove(Srv *srv, Req *r)
{
	r->fid = removefid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if (srv->remove != 0)
	{
		srv->remove(r);
	}
	else
	{
		respond(r, Enoremove);
	}
}

static void
rremove(Req *r, String8 err)
{
	(void)r;
	(void)err;
}

static void
sstat(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if (srv->stat != 0)
	{
		srv->stat(r);
	}
	else
	{
		respond(r, Enostat);
	}
}

static void
rstat(Req *r, String8 err)
{
	(void)r;
	(void)err;
}

static void
swstat(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if (srv->wstat == 0)
	{
		respond(r, Enowstat);
		return;
	}
	srv->wstat(r);
}

static void
rwstat(Req *r, String8 err)
{
	(void)r;
	(void)err;
}

Srv *
srvalloc(Arena *a, u64 infd, u64 outfd)
{
	Srv *srv     = push_array(a, Srv, 1);
	srv->arena   = a;
	srv->infd    = infd;
	srv->outfd   = outfd;
	srv->msize   = 8192 + IOHDRSZ;
	srv->maxfid  = 256;
	srv->maxreq  = 256;
	srv->fidtab  = push_array(a, Fid *, srv->maxfid);
	srv->reqtab  = push_array(a, Req *, srv->maxreq);
	srv->nexttag = 1;
	changemsize(srv, srv->msize);
	return srv;
}

void
srvfree(Srv *srv)
{
	for (u32 i = 0; i < srv->maxfid; i++)
	{
		if (srv->fidtab[i] != 0)
		{
			closefid(srv->fidtab[i]);
		}
	}
	for (u32 i = 0; i < srv->maxreq; i++)
	{
		if (srv->reqtab[i] != 0)
		{
			closereq(srv->reqtab[i]);
		}
	}
}

void
srvrun(Srv *srv)
{
	if (srv->start != 0)
	{
		srv->start(srv);
	}
	for (;;)
	{
		Req *r = getreq(srv);
		if (r == 0)
		{
			break;
		}
		if (r->error.size > 0)
		{
			respond(r, r->error);
			continue;
		}
		switch (r->ifcall.type)
		{
			case Tversion:
			{
				sversion(srv, r);
			}
			break;
			case Tauth:
			{
				sauth(srv, r);
			}
			break;
			case Tattach:
			{
				sattach(srv, r);
			}
			break;
			case Tflush:
			{
				sflush(srv, r);
			}
			break;
			case Twalk:
			{
				swalk(srv, r);
			}
			break;
			case Topen:
			{
				sopen(srv, r);
			}
			break;
			case Tcreate:
			{
				screate(srv, r);
			}
			break;
			case Tread:
			{
				sread(srv, r);
			}
			break;
			case Twrite:
			{
				swrite(srv, r);
			}
			break;
			case Tclunk:
			{
				sclunk(srv, r);
			}
			break;
			case Tremove:
			{
				sremove(srv, r);
			}
			break;
			case Tstat:
			{
				sstat(srv, r);
			}
			break;
			case Twstat:
			{
				swstat(srv, r);
			}
			break;
			default:
			{
				respond(r, str8_lit("unknown message"));
			}
			break;
		}
	}
	if (srv->end != 0)
	{
		srv->end(srv);
	}
}

void
respond(Req *r, String8 err)
{
	Srv *srv = r->srv;
	if (r->responded != 0)
	{
		goto free;
	}
	r->responded = 1;
	r->error     = err;
	switch (r->ifcall.type)
	{
		case Tflush:
		{
			if (rflush(r, err))
			{
				return;
			}
		}
		break;
		case Tversion:
		{
			rversion(r, err);
		}
		break;
		case Tauth:
		{
			rauth(r, err);
		}
		break;
		case Tattach:
		{
			rattach(r, err);
		}
		break;
		case Twalk:
		{
			rwalk(r, err);
		}
		break;
		case Topen:
		{
			ropen(r, err);
		}
		break;
		case Tcreate:
		{
			rcreate(r, err);
		}
		break;
		case Tread:
		{
			rread(r, err);
		}
		break;
		case Twrite:
		{
			rwrite(r, err);
		}
		break;
		case Tclunk:
		{
			rclunk(r, err);
		}
		break;
		case Tremove:
		{
			rremove(r, err);
		}
		break;
		case Tstat:
		{
			rstat(r, err);
		}
		break;
		case Twstat:
		{
			rwstat(r, err);
		}
		break;
		default:
			break;
	}
	r->ofcall.tag  = r->ifcall.tag;
	r->ofcall.type = r->ifcall.type + 1;
	if (r->error.size > 0)
	{
		setfcallerror(&r->ofcall, r->error);
	}
	// TODO: logging
	String8 buf = fcallencode(srv->arena, r->ofcall);
	if (buf.size <= 0)
	{
		// TODO: log error
		return;
	}
	removereq(srv, r->ifcall.tag);
	u64 n = write(srv->outfd, buf.str, buf.size);
	if (n != buf.size)
	{
		// TODO: log error
		return;
	}

free:
	for (u32 i = 0; i < r->nflush; i++)
	{
		r->flush[i]->oldreq = 0;
		respond(r->flush[i], str8_zero());
	}
	closereq(r);
}

Fid *
allocfid(Srv *srv, u32 fid)
{
	u32 hash = fid % srv->maxfid;
	if (srv->fidtab[hash])
	{
		return 0;
	}
	Fid *f            = push_array(srv->arena, Fid, 1);
	f->fid            = fid;
	f->omode          = ~0U;
	f->srv            = srv;
	srv->fidtab[hash] = f;
	srv->nfid++;
	return f;
}

Fid *
lookupfid(Srv *srv, u32 fid)
{
	u32 hash = fid % srv->maxfid;
	Fid *f   = srv->fidtab[hash];
	if (f && f->fid == fid)
	{
		return f;
	}
	return 0;
}

Fid *
removefid(Srv *srv, u32 fid)
{
	u32 hash = fid % srv->maxfid;
	Fid *f   = srv->fidtab[hash];
	if (f && f->fid == fid)
	{
		srv->fidtab[hash] = 0;
		srv->nfid--;
		return f;
	}
	return 0;
}

void
closefid(Fid *fid)
{
	if (fid->srv->destroyfid != 0)
	{
		fid->srv->destroyfid(fid);
	}
}

Req *
allocreq(Srv *srv, u32 tag)
{
	u32 hash = tag % srv->maxreq;
	if (srv->reqtab[hash])
	{
		return 0;
	}
	Req *r            = push_array(srv->arena, Req, 1);
	r->tag            = tag;
	r->srv            = srv;
	srv->reqtab[hash] = r;
	srv->nreq++;
	return r;
}

Req *
lookupreq(Srv *srv, u32 tag)
{
	u32 hash = tag % srv->maxreq;
	Req *r   = srv->reqtab[hash];
	if (r && r->tag == tag)
	{
		return r;
	}
	return 0;
}

Req *
removereq(Srv *srv, u32 tag)
{
	u32 hash = tag % srv->maxreq;
	Req *r   = srv->reqtab[hash];
	if (r && r->tag == tag)
	{
		srv->reqtab[hash] = 0;
		srv->nreq--;
		return r;
	}
	return 0;
}

void
closereq(Req *req)
{
	if (req->srv->destroyreq != 0)
	{
		req->srv->destroyreq(req);
	}
}
