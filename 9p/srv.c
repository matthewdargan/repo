static void
setfcallerror(Message9P *f, String8 err)
{
	f->error_message = err;
	f->type = Msg9P_Rerror;
}

static void
changemsize(Srv *srv, u32 msize)
{
	if(srv->rbuf != 0 && srv->wbuf != 0 && srv->msize == msize)
	{
		return;
	}
	srv->msize = msize;
	srv->rbuf = push_array(srv->arena, u8, msize);
	srv->wbuf = push_array(srv->arena, u8, msize);
}

static Req *
getreq(Srv *srv)
{
	String8 msg = read_9p_msg(srv->arena, srv->infd);
	if(msg.size <= 0)
	{
		return 0;
	}
	Message9P f = msg9p_from_str8(msg);
	if(f.type == 0)
	{
		return 0;
	}
	Req *r = allocreq(srv, f.tag);
	if(r == 0)
	{
		r = push_array(srv->arena, Req, 1);
		r->tag = f.tag;
		r->in_msg = f;
		r->error = Eduptag;
		r->buf = msg.str;
		r->responded = 0;
		r->srv = srv;
		// TODO: logging
		return r;
	}
	r->srv = srv;
	r->responded = 0;
	r->buf = msg.str;
	r->in_msg = f;
	r->out_msg = (Message9P){0};
	// TODO: logging
	return r;
}

static void
sversion(Srv *srv, Req *r)
{
	if(!str8_match(r->in_msg.protocol_version, version_9p, 0))
	{
		r->out_msg.protocol_version = str8_lit("unknown");
		respond(r, str8_zero());
		return;
	}
	r->out_msg.protocol_version = version_9p;
	r->out_msg.max_message_size = r->in_msg.max_message_size;
	respond(r, str8_zero());
}

static void
rversion(Req *r, String8 err)
{
	if(err.size > 0)
	{
		return;
	}
	changemsize(r->srv, r->out_msg.max_message_size);
}

static void
sauth(Srv *srv, Req *r)
{
	r->afid = allocfid(srv, r->in_msg.auth_fid);
	if(r->afid == 0)
	{
		respond(r, Edupfid);
		return;
	}
	if(srv->auth != 0)
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
	if(err.size > 0 && r->afid != 0)
	{
		closefid(removefid(r->srv, r->afid->fid));
	}
}

static void
sattach(Srv *srv, Req *r)
{
	r->fid = allocfid(srv, r->in_msg.fid);
	if(r->fid == 0)
	{
		respond(r, Edupfid);
		return;
	}
	r->afid = 0;
	if(r->in_msg.auth_fid != FID_NONE)
	{
		r->afid = lookupfid(srv, r->in_msg.auth_fid);
		if(r->afid == 0)
		{
			respond(r, Eunknownfid);
			return;
		}
	}
	r->fid->uid = str8_copy(srv->arena, r->in_msg.user_name);
	if(srv->attach != 0)
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
	if(err.size > 0 && r->fid != 0)
	{
		closefid(removefid(r->srv, r->fid->fid));
	}
}

static void
sflush(Srv *srv, Req *r)
{
	r->oldreq = lookupreq(srv, r->in_msg.cancel_tag);
	if(r->oldreq == 0 || r->oldreq == r)
	{
		respond(r, str8_zero());
	}
	else if(srv->flush != 0)
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
	if(err.size > 0)
	{
		return 0;
	}
	Req *or = r->oldreq;
	if(or != 0)
	{
		if(or->responded == 0)
		{
			or->flush = push_array(r->srv->arena, Req *, or->nflush + 1);
			or->flush[or->nflush] = r;
			or->nflush += 1;
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
	r->fid = lookupfid(srv, r->in_msg.fid);
	if(r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if(r->fid->omode != ~0U)
	{
		respond(r, str8_lit("cannot clone open fid"));
		return;
	}
	if(r->in_msg.walk_name_count && !(r->fid->qid.type & QTDIR))
	{
		respond(r, Ewalknodir);
		return;
	}
	if(r->in_msg.fid != r->in_msg.new_fid)
	{
		r->newfid = allocfid(srv, r->in_msg.new_fid);
		if(r->newfid == 0)
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
	if(srv->walk != 0)
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
	if(err.size > 0 || r->out_msg.walk_qid_count < r->in_msg.walk_name_count)
	{
		if(r->in_msg.fid != r->in_msg.new_fid && r->newfid != 0)
		{
			closefid(removefid(r->srv, r->newfid->fid));
		}
		if(r->out_msg.walk_qid_count == 0)
		{
			if(err.size == 0 && r->in_msg.walk_name_count != 0)
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
		if(r->out_msg.walk_qid_count == 0)
		{
			r->newfid->qid = r->fid->qid;
		}
		else
		{
			r->newfid->qid = r->out_msg.walk_qids[r->out_msg.walk_qid_count - 1];
		}
	}
}

static void
sopen(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->in_msg.fid);
	if(r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if(r->fid->omode != ~0U)
	{
		respond(r, Ebotch);
		return;
	}
	if((r->fid->qid.type & QTDIR) && (r->in_msg.open_mode & ~0x10) != OpenFlag_Read)
	{
		respond(r, Eisdir);
		return;
	}
	r->out_msg.qid = r->fid->qid;
	u32 p = 0;
	switch(r->in_msg.open_mode & 3)
	{
		case OpenFlag_Read:
		{
			p = AccessFlag_Read;
		}
		break;
		case OpenFlag_Write:
		{
			p = AccessFlag_Write;
		}
		break;
		case OpenFlag_ReadWrite:
		{
			p = AccessFlag_Read | AccessFlag_Write;
		}
		break;
		case OpenFlag_Execute:
		{
			p = AccessFlag_Execute;
		}
		break;
		default:
		{
			respond(r, Ebotch);
			return;
		}
	}
	if(r->in_msg.open_mode & OpenFlag_Truncate)
	{
		p |= AccessFlag_Write;
	}
	if((r->fid->qid.type & QTDIR) && p != AccessFlag_Read)
	{
		respond(r, Eperm);
		return;
	}
	if(srv->open != 0)
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
	if(err.size > 0)
	{
		return;
	}
	r->fid->omode = r->in_msg.open_mode;
	r->fid->qid = r->out_msg.qid;
	if(r->out_msg.qid.type & QTDIR)
	{
		r->fid->offset = 0;
	}
}

static void
screate(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->in_msg.fid);
	if(r->fid == 0)
	{
		respond(r, Eunknownfid);
	}
	else if(r->fid->omode != ~0U)
	{
		respond(r, Ebotch);
	}
	else if(!(r->fid->qid.type & QTDIR))
	{
		respond(r, Ecreatenondir);
	}
	else if(srv->create != 0)
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
	if(err.size > 0)
	{
		return;
	}
	r->fid->omode = r->in_msg.open_mode;
	r->fid->qid = r->out_msg.qid;
}

static void
sread(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->in_msg.fid);
	if(r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if((s32)r->in_msg.byte_count < 0)
	{
		respond(r, Ebotch);
		return;
	}
	if(r->in_msg.file_offset < 0 ||
	   ((r->fid->qid.type & QTDIR) && r->in_msg.file_offset != 0 && r->in_msg.file_offset != r->fid->offset))
	{
		respond(r, Ebadoffset);
		return;
	}
	if(r->in_msg.byte_count > srv->msize - MESSAGE_HEADER_SIZE)
	{
		r->in_msg.byte_count = srv->msize - MESSAGE_HEADER_SIZE;
	}
	r->rbuf = push_array(srv->arena, u8, r->in_msg.byte_count);
	r->out_msg.payload_data = str8(r->rbuf, 0);
	u32 o = r->fid->omode & 3;
	if(o != OpenFlag_Read && o != OpenFlag_ReadWrite && o != OpenFlag_Execute)
	{
		respond(r, Ebotch);
		return;
	}
	if(srv->read != 0)
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
	if(err.size == 0 && (r->fid->qid.type & QTDIR))
	{
		r->fid->offset = r->in_msg.file_offset + r->out_msg.byte_count;
	}
}

static void
swrite(Srv *srv, Req *r)
{
	r->fid = lookupfid(srv, r->in_msg.fid);
	if(r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if((s32)r->in_msg.byte_count < 0)
	{
		respond(r, Ebotch);
		return;
	}
	if(r->in_msg.file_offset < 0)
	{
		respond(r, Ebotch);
		return;
	}
	if(r->in_msg.byte_count > srv->msize - MESSAGE_HEADER_SIZE)
	{
		r->in_msg.byte_count = srv->msize - MESSAGE_HEADER_SIZE;
	}
	u32 o = r->fid->omode & 3;
	if(o != OpenFlag_Write && o != OpenFlag_ReadWrite)
	{
		String8 err = str8f(srv->arena, "write on fid with open mode 0x%x", r->fid->omode);
		respond(r, err);
		return;
	}
	if(srv->write != 0)
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
	if(err.size > 0)
	{
		return;
	}
}

static void
sclunk(Srv *srv, Req *r)
{
	r->fid = removefid(srv, r->in_msg.fid);
	if(r->fid == 0)
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
	r->fid = removefid(srv, r->in_msg.fid);
	if(r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if(srv->remove != 0)
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
	r->fid = lookupfid(srv, r->in_msg.fid);
	if(r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if(srv->stat != 0)
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
	r->fid = lookupfid(srv, r->in_msg.fid);
	if(r->fid == 0)
	{
		respond(r, Eunknownfid);
		return;
	}
	if(srv->wstat == 0)
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
	Srv *srv = push_array(a, Srv, 1);
	srv->arena = a;
	srv->infd = infd;
	srv->outfd = outfd;
	srv->msize = 8192 + MESSAGE_HEADER_SIZE;
	srv->maxfid = 256;
	srv->maxreq = 256;
	srv->fidtab = push_array(a, Fid *, srv->maxfid);
	srv->reqtab = push_array(a, Req *, srv->maxreq);
	srv->nexttag = 1;
	changemsize(srv, srv->msize);
	return srv;
}

void
srvfree(Srv *srv)
{
	for(u64 i = 0; i < srv->maxfid; i += 1)
	{
		if(srv->fidtab[i] != 0)
		{
			closefid(srv->fidtab[i]);
		}
	}
	for(u64 i = 0; i < srv->maxreq; i += 1)
	{
		if(srv->reqtab[i] != 0)
		{
			closereq(srv->reqtab[i]);
		}
	}
}

void
srvrun(Srv *srv)
{
	if(srv->start != 0)
	{
		srv->start(srv);
	}
	for(;;)
	{
		Req *r = getreq(srv);
		if(r == 0)
		{
			break;
		}
		if(r->error.size > 0)
		{
			respond(r, r->error);
			continue;
		}
		switch(r->in_msg.type)
		{
			case Msg9P_Tversion:
			{
				sversion(srv, r);
			}
			break;
			case Msg9P_Tauth:
			{
				sauth(srv, r);
			}
			break;
			case Msg9P_Tattach:
			{
				sattach(srv, r);
			}
			break;
			case Msg9P_Tflush:
			{
				sflush(srv, r);
			}
			break;
			case Msg9P_Twalk:
			{
				swalk(srv, r);
			}
			break;
			case Msg9P_Topen:
			{
				sopen(srv, r);
			}
			break;
			case Msg9P_Tcreate:
			{
				screate(srv, r);
			}
			break;
			case Msg9P_Tread:
			{
				sread(srv, r);
			}
			break;
			case Msg9P_Twrite:
			{
				swrite(srv, r);
			}
			break;
			case Msg9P_Tclunk:
			{
				sclunk(srv, r);
			}
			break;
			case Msg9P_Tremove:
			{
				sremove(srv, r);
			}
			break;
			case Msg9P_Tstat:
			{
				sstat(srv, r);
			}
			break;
			case Msg9P_Twstat:
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
	if(srv->end != 0)
	{
		srv->end(srv);
	}
}

void
respond(Req *r, String8 err)
{
	Srv *srv = r->srv;
	if(r->responded != 0)
	{
		goto free;
	}
	r->responded = 1;
	r->error = err;
	switch(r->in_msg.type)
	{
		case Msg9P_Tflush:
		{
			if(rflush(r, err))
			{
				return;
			}
		}
		break;
		case Msg9P_Tversion:
		{
			rversion(r, err);
		}
		break;
		case Msg9P_Tauth:
		{
			rauth(r, err);
		}
		break;
		case Msg9P_Tattach:
		{
			rattach(r, err);
		}
		break;
		case Msg9P_Twalk:
		{
			rwalk(r, err);
		}
		break;
		case Msg9P_Topen:
		{
			ropen(r, err);
		}
		break;
		case Msg9P_Tcreate:
		{
			rcreate(r, err);
		}
		break;
		case Msg9P_Tread:
		{
			rread(r, err);
		}
		break;
		case Msg9P_Twrite:
		{
			rwrite(r, err);
		}
		break;
		case Msg9P_Tclunk:
		{
			rclunk(r, err);
		}
		break;
		case Msg9P_Tremove:
		{
			rremove(r, err);
		}
		break;
		case Msg9P_Tstat:
		{
			rstat(r, err);
		}
		break;
		case Msg9P_Twstat:
		{
			rwstat(r, err);
		}
		break;
		default:
			break;
	}
	r->out_msg.tag = r->in_msg.tag;
	r->out_msg.type = r->in_msg.type + 1;
	if(r->error.size > 0)
	{
		setfcallerror(&r->out_msg, r->error);
	}
	// TODO: logging
	String8 buf = str8_from_msg9p(srv->arena, r->out_msg);
	if(buf.size <= 0)
	{
		// TODO: log error
		return;
	}
	removereq(srv, r->in_msg.tag);
	u64 n = write(srv->outfd, buf.str, buf.size);
	if(n != buf.size)
	{
		// TODO: log error
		return;
	}

free:
	for(u64 i = 0; i < r->nflush; i += 1)
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
	if(srv->fidtab[hash])
	{
		return 0;
	}
	Fid *f = push_array(srv->arena, Fid, 1);
	f->fid = fid;
	f->omode = ~0U;
	f->srv = srv;
	srv->fidtab[hash] = f;
	srv->nfid += 1;
	return f;
}

Fid *
lookupfid(Srv *srv, u32 fid)
{
	u32 hash = fid % srv->maxfid;
	Fid *f = srv->fidtab[hash];
	if(f && f->fid == fid)
	{
		return f;
	}
	return 0;
}

Fid *
removefid(Srv *srv, u32 fid)
{
	u32 hash = fid % srv->maxfid;
	Fid *f = srv->fidtab[hash];
	if(f && f->fid == fid)
	{
		srv->fidtab[hash] = 0;
		srv->nfid -= 1;
		return f;
	}
	return 0;
}

void
closefid(Fid *fid)
{
	if(fid->srv->destroyfid != 0)
	{
		fid->srv->destroyfid(fid);
	}
}

Req *
allocreq(Srv *srv, u32 tag)
{
	u32 hash = tag % srv->maxreq;
	if(srv->reqtab[hash])
	{
		return 0;
	}
	Req *r = push_array(srv->arena, Req, 1);
	r->tag = tag;
	r->srv = srv;
	srv->reqtab[hash] = r;
	srv->nreq += 1;
	return r;
}

Req *
lookupreq(Srv *srv, u32 tag)
{
	u32 hash = tag % srv->maxreq;
	Req *r = srv->reqtab[hash];
	if(r && r->tag == tag)
	{
		return r;
	}
	return 0;
}

Req *
removereq(Srv *srv, u32 tag)
{
	u32 hash = tag % srv->maxreq;
	Req *r = srv->reqtab[hash];
	if(r && r->tag == tag)
	{
		srv->reqtab[hash] = 0;
		srv->nreq -= 1;
		return r;
	}
	return 0;
}

void
closereq(Req *req)
{
	if(req->srv->destroyreq != 0)
	{
		req->srv->destroyreq(req);
	}
}
