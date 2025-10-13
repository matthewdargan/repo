static void
setfcallerror(Fcall *f, String8 err)
{
	f->ename = err;
	f->type = Rerror;
}

static void
changemsize(Srv *srv, u32 msize)
{
	if (srv->rbuf && srv->wbuf && srv->msize == msize)
		return;
	srv->msize = msize;
	srv->rbuf = pusharr(srv->arena, u8, msize);
	srv->wbuf = pusharr(srv->arena, u8, msize);
}

static Req *
getreq(Srv *srv)
{
	String8 msg;
	Fcall f;
	Req *r;

	msg = read9pmsg(srv->arena, srv->infd);
	if (msg.len <= 0)
		return 0;
	f = fcalldecode(msg);
	if (f.type == 0)
		return 0;
	r = allocreq(srv, f.tag);
	if (r == 0) {
		r = pusharr(srv->arena, Req, 1);
		r->tag = f.tag;
		r->ifcall = f;
		r->error = Eduptag;
		r->buf = msg.str;
		r->responded = 0;
		r->srv = srv;
		if (debug9psrv)
			/* TODO: implement proper logging */;
		return r;
	}
	r->srv = srv;
	r->responded = 0;
	r->buf = msg.str;
	r->ifcall = f;
	r->ofcall = (Fcall){0};
	if (debug9psrv) {
		/* TODO: implement proper logging */
	}
	return r;
}

static void
sversion(Srv *srv, Req *r)
{
	if (!str8cmp(r->ifcall.version, version9p, 0)) {
		r->ofcall.version = str8lit("unknown");
		respond(r, str8zero());
		return;
	}
	r->ofcall.version = version9p;
	r->ofcall.msize = r->ifcall.msize;
	respond(r, str8zero());
}

static void
rversion(Req *r, String8 err)
{
	if (err.len > 0)
		return;
	changemsize(r->srv, r->ofcall.msize);
}

static void
sauth(Srv *srv, Req *r)
{
	r->afid = allocfid(srv, r->ifcall.afid);
	if (r->afid == 0) {
		respond(r, Edupfid);
		return;
	}
	if (srv->auth)
		srv->auth(r);
	else {
		String8 err = pushstr8f(srv->arena, "authentication not required");
		respond(r, err);
	}
}

static void
rauth(Req *r, String8 err)
{
	if (err.len > 0 && r->afid)
		closefid(removefid(r->srv, r->afid->fid));
}

static void
sattach(Srv *srv, Req *r)
{
	r->fid = allocfid(srv, r->ifcall.fid);
	if (r->fid == 0) {
		respond(r, Edupfid);
		return;
	}
	r->afid = 0;
	if (r->ifcall.afid != NOFID) {
		r->afid = lookupfid(srv, r->ifcall.afid);
		if (r->afid == 0) {
			respond(r, Eunknownfid);
			return;
		}
	}
	r->fid->uid = pushstr8cpy(srv->arena, r->ifcall.uname);
	if (srv->attach)
		srv->attach(r);
	else
		respond(r, str8zero());
}

static void
rattach(Req *r, String8 err)
{
	if (err.len > 0 && r->fid)
		closefid(removefid(r->srv, r->fid->fid));
}

static void
sflush(Srv *srv, Req *r)
{
	r->oldreq = lookupreq(srv, r->ifcall.oldtag);
	if (r->oldreq == 0 || r->oldreq == r)
		respond(r, str8zero());
	else if (srv->flush)
		srv->flush(r);
	else
		respond(r, str8zero());
}

static b32
rflush(Req *r, String8 err)
{
	Req * or ;

	if (err.len > 0)
		return 0;
	or = r->oldreq;
	if (or) {
		if (or->responded == 0) {
			or->flush = pusharr(r->srv->arena, Req *, or->nflush + 1);
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
	if (r->fid == 0) {
		respond(r, Eunknownfid);
		return;
	}
	if (r->fid->omode != ~0) {
		respond(r, str8lit("cannot clone open fid"));
		return;
	}
	if (r->ifcall.nwname && !(r->fid->qid.type & QTDIR)) {
		respond(r, Ewalknodir);
		return;
	}
	if (r->ifcall.fid != r->ifcall.newfid) {
		r->newfid = allocfid(srv, r->ifcall.newfid);
		if (r->newfid == 0) {
			respond(r, Edupfid);
			return;
		}
		r->newfid->uid = pushstr8cpy(srv->arena, r->fid->uid);
	} else {
		r->newfid = r->fid;
	}
	if (srv->walk)
		srv->walk(r);
	else
		respond(r, str8lit("no walk function"));
}

static void
rwalk(Req *r, String8 err)
{
	if (err.len > 0 || r->ofcall.nwqid < r->ifcall.nwname) {
		if (r->ifcall.fid != r->ifcall.newfid && r->newfid)
			closefid(removefid(r->srv, r->newfid->fid));
		if (r->ofcall.nwqid == 0) {
			if (err.len == 0 && r->ifcall.nwname != 0)
				r->error = Enotfound;
		} else
			r->error = str8zero();
	} else {
		if (r->ofcall.nwqid == 0) {
			r->newfid->qid = r->fid->qid;
		} else {
			r->newfid->qid = r->ofcall.wqid[r->ofcall.nwqid - 1];
		}
	}
}

static void
sopen(Srv *srv, Req *r)
{
	u32 p;

	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0) {
		respond(r, Eunknownfid);
		return;
	}
	if (r->fid->omode != ~0) {
		respond(r, Ebotch);
		return;
	}
	if ((r->fid->qid.type & QTDIR) && (r->ifcall.mode & ~0x10) != OREAD) {
		respond(r, Eisdir);
		return;
	}
	r->ofcall.qid = r->fid->qid;
	switch (r->ifcall.mode & 3) {
		default:
			respond(r, Ebotch);
			return;
		case OREAD:
			p = AREAD;
			break;
		case OWRITE:
			p = AWRITE;
			break;
		case ORDWR:
			p = AREAD | AWRITE;
			break;
		case OEXEC:
			p = AEXEC;
			break;
	}
	if (r->ifcall.mode & OTRUNC)
		p |= AWRITE;
	if ((r->fid->qid.type & QTDIR) && p != AREAD) {
		respond(r, Eperm);
		return;
	}
	if (srv->open)
		srv->open(r);
	else
		respond(r, str8zero());
}

static void
ropen(Req *r, String8 err)
{
	if (err.len > 0)
		return;
	r->fid->omode = r->ifcall.mode;
	r->fid->qid = r->ofcall.qid;
	if (r->ofcall.qid.type & QTDIR)
		r->fid->offset = 0;
}

static void
screate(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0)
		respond(r, Eunknownfid);
	else if (r->fid->omode != ~0)
		respond(r, Ebotch);
	else if (!(r->fid->qid.type & QTDIR))
		respond(r, Ecreatenondir);
	else if (srv->create)
		srv->create(r);
	else
		respond(r, Enocreate);
}

static void
rcreate(Req *r, String8 err)
{
	if (err.len > 0)
		return;
	r->fid->omode = r->ifcall.mode;
	r->fid->qid = r->ofcall.qid;
}

static void
sread(Srv *srv, Req *r)
{
	u32 o;

	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0) {
		respond(r, Eunknownfid);
		return;
	}
	if ((s32)r->ifcall.count < 0) {
		respond(r, Ebotch);
		return;
	}
	if (r->ifcall.offset < 0 ||
	    ((r->fid->qid.type & QTDIR) && r->ifcall.offset != 0 && r->ifcall.offset != r->fid->offset)) {
		respond(r, Ebadoffset);
		return;
	}
	if (r->ifcall.count > srv->msize - IOHDRSZ)
		r->ifcall.count = srv->msize - IOHDRSZ;
	r->rbuf = pusharr(srv->arena, u8, r->ifcall.count);
	r->ofcall.data = str8(r->rbuf, 0);
	o = r->fid->omode & 3;
	if (o != OREAD && o != ORDWR && o != OEXEC) {
		respond(r, Ebotch);
		return;
	}
	if (srv->read)
		srv->read(r);
	else
		respond(r, str8lit("no srv->read"));
}

static void
rread(Req *r, String8 err)
{
	if (err.len == 0 && (r->fid->qid.type & QTDIR))
		r->fid->offset = r->ifcall.offset + r->ofcall.count;
}

static void
swrite(Srv *srv, Req *r)
{
	u32 o;

	r->fid = lookupfid(srv, r->ifcall.fid);
	if (r->fid == 0) {
		respond(r, Eunknownfid);
		return;
	}
	if ((s32)r->ifcall.count < 0) {
		respond(r, Ebotch);
		return;
	}
	if (r->ifcall.offset < 0) {
		respond(r, Ebotch);
		return;
	}
	if (r->ifcall.count > srv->msize - IOHDRSZ)
		r->ifcall.count = srv->msize - IOHDRSZ;
	o = r->fid->omode & 3;
	if (o != OWRITE && o != ORDWR) {
		String8 err = pushstr8f(srv->arena, "write on fid with open mode 0x%x", r->fid->omode);
		respond(r, err);
		return;
	}
	if (srv->write) {
		srv->write(r);
	} else
		respond(r, str8lit("no srv->write"));
}

static void
rwrite(Req *r, String8 err)
{
	if (err.len > 0)
		return;
}

static void
sclunk(Srv *srv, Req *r)
{
	r->fid = removefid(srv, r->ifcall.fid);
	if (r->fid == 0)
		respond(r, Eunknownfid);
	else
		respond(r, str8zero());
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
	if (r->fid == 0) {
		respond(r, Eunknownfid);
		return;
	}
	if (srv->remove)
		srv->remove(r);
	else
		respond(r, Enoremove);
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
	if (r->fid == 0) {
		respond(r, Eunknownfid);
		return;
	}
	if (srv->stat)
		srv->stat(r);
	else
		respond(r, Enostat);
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
	if (r->fid == 0) {
		respond(r, Eunknownfid);
		return;
	}
	if (srv->wstat == 0) {
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
	Srv *srv;

	srv = pusharr(a, Srv, 1);
	srv->arena = a;
	srv->infd = infd;
	srv->outfd = outfd;
	srv->msize = 8192 + IOHDRSZ;
	srv->maxfid = 256;
	srv->maxreq = 256;
	srv->fidtab = pusharr(a, Fid *, srv->maxfid);
	srv->reqtab = pusharr(a, Req *, srv->maxreq);
	srv->nexttag = 1;
	changemsize(srv, srv->msize);
	return srv;
}

void
srvfree(Srv *srv)
{
	u32 i;

	for (i = 0; i < srv->maxfid; i++) {
		if (srv->fidtab[i])
			closefid(srv->fidtab[i]);
	}
	for (i = 0; i < srv->maxreq; i++) {
		if (srv->reqtab[i])
			closereq(srv->reqtab[i]);
	}
}

void
srvrun(Srv *srv)
{
	Req *r;

	if (srv->start)
		srv->start(srv);
	while ((r = getreq(srv)) != 0) {
		if (r->error.len > 0) {
			respond(r, r->error);
			continue;
		}
		switch (r->ifcall.type) {
			default:
				respond(r, str8lit("unknown message"));
				break;
			case Tversion:
				sversion(srv, r);
				break;
			case Tauth:
				sauth(srv, r);
				break;
			case Tattach:
				sattach(srv, r);
				break;
			case Tflush:
				sflush(srv, r);
				break;
			case Twalk:
				swalk(srv, r);
				break;
			case Topen:
				sopen(srv, r);
				break;
			case Tcreate:
				screate(srv, r);
				break;
			case Tread:
				sread(srv, r);
				break;
			case Twrite:
				swrite(srv, r);
				break;
			case Tclunk:
				sclunk(srv, r);
				break;
			case Tremove:
				sremove(srv, r);
				break;
			case Tstat:
				sstat(srv, r);
				break;
			case Twstat:
				swstat(srv, r);
				break;
		}
	}
	if (srv->end)
		srv->end(srv);
}

void
respond(Req *r, String8 err)
{
	u64 n;
	Srv *srv;
	String8 buf;
	u32 i;

	srv = r->srv;
	if (r->responded)
		goto free;
	r->responded = 1;
	r->error = err;
	switch (r->ifcall.type) {
		default:
			break;
		case Tflush:
			if (rflush(r, err))
				return;
			break;
		case Tversion:
			rversion(r, err);
			break;
		case Tauth:
			rauth(r, err);
			break;
		case Tattach:
			rattach(r, err);
			break;
		case Twalk:
			rwalk(r, err);
			break;
		case Topen:
			ropen(r, err);
			break;
		case Tcreate:
			rcreate(r, err);
			break;
		case Tread:
			rread(r, err);
			break;
		case Twrite:
			rwrite(r, err);
			break;
		case Tclunk:
			rclunk(r, err);
			break;
		case Tremove:
			rremove(r, err);
			break;
		case Tstat:
			rstat(r, err);
			break;
		case Twstat:
			rwstat(r, err);
			break;
	}
	r->ofcall.tag = r->ifcall.tag;
	r->ofcall.type = r->ifcall.type + 1;
	if (r->error.len > 0)
		setfcallerror(&r->ofcall, r->error);
	if (debug9psrv) {
		/* TODO: implement proper logging */
	}
	buf = fcallencode(srv->arena, r->ofcall);
	if (buf.len <= 0) {
		/* TODO: handle error */
		return;
	}
	removereq(srv, r->ifcall.tag);
	n = write(srv->outfd, buf.str, buf.len);
	if (n != buf.len) {
		/* TODO: handle write error */
		return;
	}

free:
	for (i = 0; i < r->nflush; i++) {
		r->flush[i]->oldreq = 0;
		respond(r->flush[i], str8zero());
	}
	closereq(r);
}

Fid *
allocfid(Srv *srv, u32 fid)
{
	u32 hash;
	Fid *f;

	hash = fid % srv->maxfid;
	if (srv->fidtab[hash])
		return 0;
	f = pusharr(srv->arena, Fid, 1);
	f->fid = fid;
	f->omode = ~0;
	f->srv = srv;
	srv->fidtab[hash] = f;
	srv->nfid++;
	return f;
}

Fid *
lookupfid(Srv *srv, u32 fid)
{
	u32 hash;
	Fid *f;

	hash = fid % srv->maxfid;
	f = srv->fidtab[hash];
	if (f && f->fid == fid)
		return f;
	return 0;
}

Fid *
removefid(Srv *srv, u32 fid)
{
	u32 hash;
	Fid *f;

	hash = fid % srv->maxfid;
	f = srv->fidtab[hash];
	if (f && f->fid == fid) {
		srv->fidtab[hash] = 0;
		srv->nfid--;
		return f;
	}
	return 0;
}

void
closefid(Fid *fid)
{
	if (fid->srv->destroyfid)
		fid->srv->destroyfid(fid);
}

Req *
allocreq(Srv *srv, u32 tag)
{
	u32 hash;
	Req *r;

	hash = tag % srv->maxreq;
	if (srv->reqtab[hash])
		return 0;
	r = pusharr(srv->arena, Req, 1);
	r->tag = tag;
	r->srv = srv;
	srv->reqtab[hash] = r;
	srv->nreq++;
	return r;
}

Req *
lookupreq(Srv *srv, u32 tag)
{
	u32 hash;
	Req *r;

	hash = tag % srv->maxreq;
	r = srv->reqtab[hash];
	if (r && r->tag == tag)
		return r;
	return 0;
}

Req *
removereq(Srv *srv, u32 tag)
{
	u32 hash;
	Req *r;

	hash = tag % srv->maxreq;
	r = srv->reqtab[hash];
	if (r && r->tag == tag) {
		srv->reqtab[hash] = 0;
		srv->nreq--;
		return r;
	}
	return 0;
}

void
closereq(Req *req)
{
	if (req->srv->destroyreq)
		req->srv->destroyreq(req);
}
