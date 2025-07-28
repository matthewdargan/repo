static void
putb1(u8 *p, u8 v)
{
	p[0] = v;
}

static void
putb2(u8 *p, u16 v)
{
	p[0] = v;
	p[1] = v >> 8;
}

static void
putb4(u8 *p, u32 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static void
putb8(u8 *p, u64 v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
	p[4] = v >> 32;
	p[5] = v >> 40;
	p[6] = v >> 48;
	p[7] = v >> 56;
}

static u8 *
putstr(u8 *p, String8 s)
{
	putb2(p, s.len);
	p += 2;
	if (s.len > 0) {
		memcpy(p, s.str, s.len);
		p += s.len;
	}
	return p;
}

static u8 *
putqid(u8 *p, Qid qid)
{
	putb1(p, qid.type);
	p += 1;
	putb4(p, qid.vers);
	p += 4;
	putb8(p, qid.path);
	p += 8;
	return p;
}

static u32
getb1(u8 *p)
{
	return p[0];
}

static u32
getb2(u8 *p)
{
	return p[0] | (p[1] << 8);
}

static u32
getb4(u8 *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static u64
getb8(u8 *p)
{
	return (u64)p[0] | ((u64)p[1] << 8) | ((u64)p[2] << 16) | ((u64)p[3] << 24) | ((u64)p[4] << 32) |
	       ((u64)p[5] << 40) | ((u64)p[6] << 48) | ((u64)p[7] << 56);
}

static u8 *
getstr(u8 *p, u8 *end, String8 *s)
{
	u32 len;

	if (p + 2 > end)
		return NULL;
	len = getb2(p);
	p += 2;
	if (p + len > end)
		return NULL;
	s->len = len;
	if (len > 0) {
		s->str = p;
		p += len;
	} else
		s->str = NULL;
	return p;
}

static u8 *
getqid(u8 *p, u8 *end, Qid *qid)
{
	if (p + 13 > end)
		return NULL;
	qid->type = getb1(p);
	p += 1;
	qid->vers = getb4(p);
	p += 4;
	qid->path = getb8(p);
	p += 8;
	return p;
}

static u32
fcallsize(Fcall fc)
{
	u32 size, i;

	size = 4 + 1 + 2;
	switch (fc.type) {
		case Tversion:
		case Rversion:
			size += 4;
			size += 2 + fc.version.len;
			break;
		case Tauth:
			size += 4;
			size += 2 + fc.uname.len;
			size += 2 + fc.aname.len;
			break;
		case Rauth:
			size += 13;
			break;
		case Rerror:
			size += 2 + fc.ename.len;
			break;
		case Tflush:
			size += 2;
			break;
		case Rflush:
			break;
		case Tattach:
			size += 4;
			size += 4;
			size += 2 + fc.uname.len;
			size += 2 + fc.aname.len;
			break;
		case Rattach:
			size += 13;
			break;
		case Twalk:
			size += 4;
			size += 4;
			size += 2;
			for (i = 0; i < fc.nwname; i++)
				size += 2 + fc.wname[i].len;
			break;
		case Rwalk:
			size += 2;
			size += fc.nwqid * 13;
			break;
		case Topen:
			size += 4;
			size++;
			break;
		case Ropen:
		case Rcreate:
			size += 13;
			size += 4;
			break;
		case Tcreate:
			size += 4;
			size += 2 + fc.name.len;
			size += 4;
			size++;
			break;
		case Tread:
			size += 4;
			size += 8;
			size += 4;
			break;
		case Rread:
			size += 4;
			size += fc.data.len;
			break;
		case Twrite:
			size += 4;
			size += 8;
			size += 4;
			size += fc.data.len;
			break;
		case Rwrite:
			size += 4;
			break;
		case Tclunk:
		case Tremove:
			size += 4;
			break;
		case Rclunk:
		case Rremove:
			break;
		case Tstat:
			size += 4;
			break;
		case Rstat:
			size += 2 + fc.stat.len;
			break;
		case Twstat:
			size += 4;
			size += 2 + fc.stat.len;
			break;
		case Rwstat:
			break;
		default:
			return 0;
	}
	return size;
}

static String8
fcallencode(Arena *a, Fcall fc)
{
	String8 msg;
	u8 *p;
	u32 i;

	msg.len = fcallsize(fc);
	if (msg.len == 0)
		return str8zero();
	msg.str = pusharrnoz(a, u8, msg.len);
	p = msg.str;
	putb4(p, msg.len);
	p += 4;
	putb1(p, fc.type);
	p++;
	putb2(p, fc.tag);
	p += 2;
	switch (fc.type) {
		case Tversion:
		case Rversion:
			putb4(p, fc.msize);
			p += 4;
			p = putstr(p, fc.version);
			break;
		case Tauth:
			putb4(p, fc.afid);
			p += 4;
			p = putstr(p, fc.uname);
			p = putstr(p, fc.aname);
			break;
		case Rauth:
			p = putqid(p, fc.aqid);
			break;
		case Rerror:
			p = putstr(p, fc.ename);
			break;
		case Tflush:
			putb2(p, fc.oldtag);
			p += 2;
			break;
		case Rflush:
			break;
		case Tattach:
			putb4(p, fc.fid);
			p += 4;
			putb4(p, fc.afid);
			p += 4;
			p = putstr(p, fc.uname);
			p = putstr(p, fc.aname);
			break;
		case Rattach:
			p = putqid(p, fc.qid);
			break;
		case Twalk:
			putb4(p, fc.fid);
			p += 4;
			putb4(p, fc.newfid);
			p += 4;
			putb2(p, fc.nwname);
			p += 2;
			if (fc.nwname > MAXWELEM)
				return str8zero();
			for (i = 0; i < fc.nwname; i++)
				p = putstr(p, fc.wname[i]);
			break;
		case Rwalk:
			putb2(p, fc.nwqid);
			p += 2;
			if (fc.nwqid > MAXWELEM)
				return str8zero();
			for (i = 0; i < fc.nwqid; i++)
				p = putqid(p, fc.wqid[i]);
			break;
		case Topen:
			putb4(p, fc.fid);
			p += 4;
			putb1(p, fc.mode);
			p++;
			break;
		case Ropen:
		case Rcreate:
			p = putqid(p, fc.qid);
			putb4(p, fc.iounit);
			p += 4;
			break;
		case Tcreate:
			putb4(p, fc.fid);
			p += 4;
			p = putstr(p, fc.name);
			putb4(p, fc.perm);
			p += 4;
			putb1(p, fc.mode);
			p++;
			break;
		case Tread:
			putb4(p, fc.fid);
			p += 4;
			putb8(p, fc.offset);
			p += 8;
			putb4(p, fc.count);
			p += 4;
			break;
		case Rread:
			putb4(p, fc.data.len);
			p += 4;
			if (fc.data.len > 0) {
				memcpy(p, fc.data.str, fc.data.len);
				p += fc.data.len;
			}
			break;
		case Twrite:
			putb4(p, fc.fid);
			p += 4;
			putb8(p, fc.offset);
			p += 8;
			putb4(p, fc.data.len);
			p += 4;
			if (fc.data.len > 0) {
				memcpy(p, fc.data.str, fc.data.len);
				p += fc.data.len;
			}
			break;
		case Rwrite:
			putb4(p, fc.count);
			p += 4;
			break;
		case Tclunk:
		case Tremove:
			putb4(p, fc.fid);
			p += 4;
			break;
		case Rclunk:
		case Rremove:
			break;
		case Tstat:
			putb4(p, fc.fid);
			p += 4;
			break;
		case Rstat:
			putb2(p, fc.stat.len);
			p += 2;
			if (fc.stat.len > 0) {
				memcpy(p, fc.stat.str, fc.stat.len);
				p += fc.stat.len;
			}
			break;
		case Twstat:
			putb4(p, fc.fid);
			p += 4;
			putb2(p, fc.stat.len);
			p += 2;
			if (fc.stat.len > 0) {
				memcpy(p, fc.stat.str, fc.stat.len);
				p += fc.stat.len;
			}
			break;
		case Rwstat:
			break;
		default:
			return str8zero();
	}
	if (msg.len != p - msg.str)
		return str8zero();
	return msg;
}

static b32
fcalldecode(Arena *a, String8 msg, Fcall *fc)
{
	u8 *p, *end;
	u32 size, i;

	if (msg.len < 7)
		return 0;
	p = msg.str;
	end = msg.str + msg.len;
	size = getb4(p);
	p += 4;
	if (size < 7)
		return 0;
	memset(fc, 0, sizeof *fc);
	fc->type = getb1(p);
	p++;
	fc->tag = getb2(p);
	p += 2;
	switch (fc->type) {
		case Tversion:
		case Rversion:
			if (p + 4 > end)
				return 0;
			fc->msize = getb4(p);
			p += 4;
			p = getstr(p, end, &fc->version);
			if (p == NULL)
				return 0;
			break;
		case Tauth:
			if (p + 4 > end)
				return 0;
			fc->afid = getb4(p);
			p += 4;
			p = getstr(p, end, &fc->uname);
			if (p == NULL)
				return 0;
			p = getstr(p, end, &fc->aname);
			if (p == NULL)
				return 0;
			break;
		case Rauth:
			p = getqid(p, end, &fc->aqid);
			if (p == NULL)
				return 0;
			break;
		case Rerror:
			p = getstr(p, end, &fc->ename);
			if (p == NULL)
				return 0;
			break;
		case Tflush:
			if (p + 2 > end)
				return 0;
			fc->oldtag = getb2(p);
			p += 2;
			break;
		case Rflush:
			break;
		case Tattach:
			if (p + 8 > end)
				return 0;
			fc->fid = getb4(p);
			p += 4;
			fc->afid = getb4(p);
			p += 4;
			p = getstr(p, end, &fc->uname);
			if (p == NULL)
				return 0;
			p = getstr(p, end, &fc->aname);
			if (p == NULL)
				return 0;
			break;
		case Rattach:
			p = getqid(p, end, &fc->qid);
			if (p == NULL)
				return 0;
			break;
		case Twalk:
			if (p + 10 > end)
				return 0;
			fc->fid = getb4(p);
			p += 4;
			fc->newfid = getb4(p);
			p += 4;
			fc->nwname = getb2(p);
			p += 2;
			if (fc->nwname > MAXWELEM)
				return 0;
			for (i = 0; i < fc->nwname; i++) {
				p = getstr(p, end, &fc->wname[i]);
				if (p == NULL)
					return 0;
			}
			break;
		case Rwalk:
			if (p + 2 > end)
				return 0;
			fc->nwqid = getb2(p);
			p += 2;
			if (fc->nwqid > MAXWELEM)
				return 0;
			for (i = 0; i < fc->nwqid; i++) {
				p = getqid(p, end, &fc->wqid[i]);
				if (p == NULL)
					return 0;
			}
			break;
		case Topen:
			if (p + 5 > end)
				return 0;
			fc->fid = getb4(p);
			p += 4;
			fc->mode = getb1(p);
			p++;
			break;
		case Ropen:
		case Rcreate:
			p = getqid(p, end, &fc->qid);
			if (p == NULL)
				return 0;
			if (p + 4 > end)
				return 0;
			fc->iounit = getb4(p);
			p += 4;
			break;
		case Tcreate:
			if (p + 4 > end)
				return 0;
			fc->fid = getb4(p);
			p += 4;
			p = getstr(p, end, &fc->name);
			if (p == NULL)
				return 0;
			if (p + 5 > end)
				return 0;
			fc->perm = getb4(p);
			p += 4;
			fc->mode = getb1(p);
			p++;
			break;
		case Tread:
			if (p + 16 > end)
				return 0;
			fc->fid = getb4(p);
			p += 4;
			fc->offset = getb8(p);
			p += 8;
			fc->count = getb4(p);
			p += 4;
			break;
		case Rread:
			if (p + 4 > end)
				return 0;
			fc->data.len = getb4(p);
			p += 4;
			if (p + fc->data.len > end)
				return 0;
			if (fc->data.len > 0) {
				fc->data.str = p;
				p += fc->data.len;
			} else
				fc->data.str = NULL;
			break;
		case Twrite:
			if (p + 16 > end)
				return 0;
			fc->fid = getb4(p);
			p += 4;
			fc->offset = getb8(p);
			p += 8;
			fc->data.len = getb4(p);
			p += 4;
			if (p + fc->data.len > end)
				return 0;
			if (fc->data.len > 0) {
				fc->data.str = p;
				p += fc->data.len;
			} else
				fc->data.str = NULL;
			break;
		case Rwrite:
			if (p + 4 > end)
				return 0;
			fc->count = getb4(p);
			p += 4;
			break;
		case Tclunk:
		case Tremove:
			if (p + 4 > end)
				return 0;
			fc->fid = getb4(p);
			p += 4;
			break;
		case Rclunk:
		case Rremove:
			break;
		case Tstat:
			if (p + 4 > end)
				return 0;
			fc->fid = getb4(p);
			p += 4;
			break;
		case Rstat:
			if (p + 2 > end)
				return 0;
			fc->stat.len = getb2(p);
			p += 2;
			if (p + fc->stat.len > end)
				return 0;
			if (fc->stat.len > 0) {
				fc->stat.str = p;
				p += fc->stat.len;
			} else
				fc->stat.str = NULL;
			break;
		case Twstat:
			if (p + 6 > end)
				return 0;
			fc->fid = getb4(p);
			p += 4;
			fc->stat.len = getb2(p);
			p += 2;
			if (p + fc->stat.len > end)
				return 0;
			if (fc->stat.len > 0) {
				fc->stat.str = p;
				p += fc->stat.len;
			} else
				fc->stat.str = NULL;
			break;
		case Rwstat:
			break;
		default:
			return 0;
	}
	if (p != end)
		return 0;
	return 1;
}
