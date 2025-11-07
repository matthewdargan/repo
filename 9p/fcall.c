static u8 *
putstr(u8 *p, String8 s)
{
	write_u16((p), from_le_u16(s.size));
	p += 2;
	if (s.size > 0)
	{
		MemoryCopy(p, s.str, s.size);
		p += s.size;
	}
	return p;
}

static u8 *
putqid(u8 *p, Qid qid)
{
	(p)[0] = (u8)(qid.type);
	p++;
	write_u32((p), from_le_u32(qid.vers));
	p += 4;
	write_u64((p), from_le_u64(qid.path));
	p += 8;
	return p;
}

static u8 *
getstr(u8 *p, u8 *end, String8 *s)
{
	if (p + 2 > end)
	{
		return 0;
	}
	u32 len = ((u32)from_le_u16(read_u16(p)));
	p += 2;
	if (p + len > end)
	{
		return 0;
	}
	s->size = len;
	if (len > 0)
	{
		s->str = p;
		p += len;
	}
	else
	{
		s->str = 0;
	}
	return p;
}

static u8 *
getqid(u8 *p, u8 *end, Qid *qid)
{
	if (p + 13 > end)
	{
		return 0;
	}
	qid->type = ((u32)(p)[0]);
	p++;
	qid->vers = ((u32)from_le_u32(read_u32(p)));
	p += 4;
	qid->path = ((u64)from_le_u64(read_u64(p)));
	p += 8;
	return p;
}

static u32
fcallsize(Fcall fc)
{
	u32 size = 4 + 1 + 2;
	switch (fc.type)
	{
		case Tversion:
		case Rversion:
		{
			size += 4;
			size += 2 + fc.version.size;
		}
		break;
		case Tauth:
		{
			size += 4;
			size += 2 + fc.uname.size;
			size += 2 + fc.aname.size;
		}
		break;
		case Rauth:
		{
			size += 13;
		}
		break;
		case Rerror:
		{
			size += 2 + fc.ename.size;
		}
		break;
		case Tflush:
		{
			size += 2;
		}
		break;
		case Rflush:
			break;
		case Tattach:
		{
			size += 4;
			size += 4;
			size += 2 + fc.uname.size;
			size += 2 + fc.aname.size;
		}
		break;
		case Rattach:
		{
			size += 13;
		}
		break;
		case Twalk:
		{
			size += 4;
			size += 4;
			size += 2;
			for (u32 i = 0; i < fc.nwname; i++)
			{
				size += 2 + fc.wname[i].size;
			}
		}
		break;
		case Rwalk:
		{
			size += 2;
			size += fc.nwqid * 13;
		}
		break;
		case Topen:
		{
			size += 4;
			size++;
		}
		break;
		case Ropen:
		case Rcreate:
		{
			size += 13;
			size += 4;
		}
		break;
		case Tcreate:
		{
			size += 4;
			size += 2 + fc.name.size;
			size += 4;
			size++;
		}
		break;
		case Tread:
		{
			size += 4;
			size += 8;
			size += 4;
		}
		break;
		case Rread:
		{
			size += 4;
			size += fc.data.size;
		}
		break;
		case Twrite:
		{
			size += 4;
			size += 8;
			size += 4;
			size += fc.data.size;
		}
		break;
		case Rwrite:
		{
			size += 4;
		}
		break;
		case Tclunk:
		case Tremove:
		{
			size += 4;
		}
		break;
		case Rclunk:
		case Rremove:
			break;
		case Tstat:
		{
			size += 4;
		}
		break;
		case Rstat:
		{
			size += 2 + fc.stat.size;
		}
		break;
		case Twstat:
		{
			size += 4;
			size += 2 + fc.stat.size;
		}
		break;
		case Rwstat:
			break;
		default:
		{
			return 0;
		}
	}
	return size;
}

static String8
fcallencode(Arena *a, Fcall fc)
{
	u32 msglen = fcallsize(fc);
	if (msglen == 0)
	{
		return str8_zero();
	}
	String8 msg = {
	    .str  = push_array_no_zero(a, u8, msglen),
	    .size = msglen,
	};
	u8 *p = msg.str;
	write_u32((p), from_le_u32(msg.size));
	p += 4;
	(p)[0] = (u8)(fc.type);
	p++;
	write_u16((p), from_le_u16(fc.tag));
	p += 2;
	switch (fc.type)
	{
		case Tversion:
		case Rversion:
		{
			write_u32((p), from_le_u32(fc.msize));
			p += 4;
			p = putstr(p, fc.version);
		}
		break;
		case Tauth:
		{
			write_u32((p), from_le_u32(fc.afid));
			p += 4;
			p = putstr(p, fc.uname);
			p = putstr(p, fc.aname);
		}
		break;
		case Rauth:
		{
			p = putqid(p, fc.aqid);
		}
		break;
		case Rerror:
		{
			p = putstr(p, fc.ename);
		}
		break;
		case Tflush:
		{
			write_u16((p), from_le_u16(fc.oldtag));
			p += 2;
		}
		break;
		case Rflush:
			break;
		case Tattach:
		{
			write_u32((p), from_le_u32(fc.fid));
			p += 4;
			write_u32((p), from_le_u32(fc.afid));
			p += 4;
			p = putstr(p, fc.uname);
			p = putstr(p, fc.aname);
		}
		break;
		case Rattach:
		{
			p = putqid(p, fc.qid);
		}
		break;
		case Twalk:
		{
			write_u32((p), from_le_u32(fc.fid));
			p += 4;
			write_u32((p), from_le_u32(fc.newfid));
			p += 4;
			write_u16((p), from_le_u16(fc.nwname));
			p += 2;
			if (fc.nwname > MAXWELEM)
			{
				return str8_zero();
			}
			for (u32 i = 0; i < fc.nwname; i++)
			{
				p = putstr(p, fc.wname[i]);
			}
		}
		break;
		case Rwalk:
		{
			write_u16((p), from_le_u16(fc.nwqid));
			p += 2;
			if (fc.nwqid > MAXWELEM)
			{
				return str8_zero();
			}
			for (u32 i = 0; i < fc.nwqid; i++)
			{
				p = putqid(p, fc.wqid[i]);
			}
		}
		break;
		case Topen:
		{
			write_u32((p), from_le_u32(fc.fid));
			p += 4;
			(p)[0] = (u8)(fc.mode);
			p++;
		}
		break;
		case Ropen:
		case Rcreate:
		{
			p = putqid(p, fc.qid);
			write_u32((p), from_le_u32(fc.iounit));
			p += 4;
		}
		break;
		case Tcreate:
		{
			write_u32((p), from_le_u32(fc.fid));
			p += 4;
			p = putstr(p, fc.name);
			write_u32((p), from_le_u32(fc.perm));
			p += 4;
			(p)[0] = (u8)(fc.mode);
			p++;
		}
		break;
		case Tread:
		{
			write_u32((p), from_le_u32(fc.fid));
			p += 4;
			write_u64((p), from_le_u64(fc.offset));
			p += 8;
			write_u32((p), from_le_u32(fc.count));
			p += 4;
		}
		break;
		case Rread:
		{
			write_u32((p), from_le_u32(fc.data.size));
			p += 4;
			if (fc.data.size > 0)
			{
				MemoryCopy(p, fc.data.str, fc.data.size);
				p += fc.data.size;
			}
		}
		break;
		case Twrite:
		{
			write_u32((p), from_le_u32(fc.fid));
			p += 4;
			write_u64((p), from_le_u64(fc.offset));
			p += 8;
			write_u32((p), from_le_u32(fc.data.size));
			p += 4;
			if (fc.data.size > 0)
			{
				MemoryCopy(p, fc.data.str, fc.data.size);
				p += fc.data.size;
			}
		}
		break;
		case Rwrite:
		{
			write_u32((p), from_le_u32(fc.count));
			p += 4;
		}
		break;
		case Tclunk:
		case Tremove:
		{
			write_u32((p), from_le_u32(fc.fid));
			p += 4;
		}
		break;
		case Rclunk:
		case Rremove:
			break;
		case Tstat:
		{
			write_u32((p), from_le_u32(fc.fid));
			p += 4;
		}
		break;
		case Rstat:
		{
			write_u16((p), from_le_u16(fc.stat.size));
			p += 2;
			if (fc.stat.size > 0)
			{
				MemoryCopy(p, fc.stat.str, fc.stat.size);
				p += fc.stat.size;
			}
		}
		break;
		case Twstat:
		{
			write_u32((p), from_le_u32(fc.fid));
			p += 4;
			write_u16((p), from_le_u16(fc.stat.size));
			p += 2;
			if (fc.stat.size > 0)
			{
				MemoryCopy(p, fc.stat.str, fc.stat.size);
				p += fc.stat.size;
			}
		}
		break;
		case Rwstat:
			break;
		default:
		{
			return str8_zero();
		}
	}
	if (msg.size != (u64)(p - msg.str))
	{
		return str8_zero();
	}
	return msg;
}

static Fcall
fcalldecode(String8 msg)
{
	Fcall fc    = {0};
	Fcall errfc = {0};
	if (msg.size < 7)
	{
		return errfc;
	}
	u8 *p    = msg.str;
	u8 *end  = msg.str + msg.size;
	u32 size = ((u32)from_le_u32(read_u32(p)));
	p += 4;
	if (size != msg.size)
	{
		return errfc;
	}
	fc.type = ((u32)(p)[0]);
	p++;
	fc.tag = ((u32)from_le_u16(read_u16(p)));
	p += 2;
	switch (fc.type)
	{
		case Tversion:
		case Rversion:
		{
			if (p + 4 > end)
			{
				return errfc;
			}
			fc.msize = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			p = getstr(p, end, &fc.version);
			if (p == 0)
			{
				return errfc;
			}
		}
		break;
		case Tauth:
		{
			if (p + 4 > end)
			{
				return errfc;
			}
			fc.afid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			p = getstr(p, end, &fc.uname);
			if (p == 0)
			{
				return errfc;
			}
			p = getstr(p, end, &fc.aname);
			if (p == 0)
			{
				return errfc;
			}
		}
		break;
		case Rauth:
		{
			p = getqid(p, end, &fc.aqid);
			if (p == 0)
			{
				return errfc;
			}
		}
		break;
		case Rerror:
		{
			p = getstr(p, end, &fc.ename);
			if (p == 0)
			{
				return errfc;
			}
		}
		break;
		case Tflush:
		{
			if (p + 2 > end)
			{
				return errfc;
			}
			fc.oldtag = ((u32)from_le_u16(read_u16(p)));
			p += 2;
		}
		break;
		case Rflush:
			break;
		case Tattach:
		{
			if (p + 8 > end)
			{
				return errfc;
			}
			fc.fid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			fc.afid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			p = getstr(p, end, &fc.uname);
			if (p == 0)
			{
				return errfc;
			}
			p = getstr(p, end, &fc.aname);
			if (p == 0)
			{
				return errfc;
			}
		}
		break;
		case Rattach:
		{
			p = getqid(p, end, &fc.qid);
			if (p == 0)
			{
				return errfc;
			}
		}
		break;
		case Twalk:
		{
			if (p + 10 > end)
			{
				return errfc;
			}
			fc.fid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			fc.newfid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			fc.nwname = ((u32)from_le_u16(read_u16(p)));
			p += 2;
			if (fc.nwname > MAXWELEM)
			{
				return errfc;
			}
			for (u32 i = 0; i < fc.nwname; i++)
			{
				p = getstr(p, end, &fc.wname[i]);
				if (p == 0)
				{
					return errfc;
				}
			}
		}
		break;
		case Rwalk:
		{
			if (p + 2 > end)
			{
				return errfc;
			}
			fc.nwqid = ((u32)from_le_u16(read_u16(p)));
			p += 2;
			if (fc.nwqid > MAXWELEM)
			{
				return errfc;
			}
			for (u32 i = 0; i < fc.nwqid; i++)
			{
				p = getqid(p, end, &fc.wqid[i]);
				if (p == 0)
				{
					return errfc;
				}
			}
		}
		break;
		case Topen:
		{
			if (p + 5 > end)
			{
				return errfc;
			}
			fc.fid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			fc.mode = ((u32)(p)[0]);
			p++;
		}
		break;
		case Ropen:
		case Rcreate:
		{
			p = getqid(p, end, &fc.qid);
			if (p == 0)
			{
				return errfc;
			}
			if (p + 4 > end)
			{
				return errfc;
			}
			fc.iounit = ((u32)from_le_u32(read_u32(p)));
			p += 4;
		}
		break;
		case Tcreate:
		{
			if (p + 4 > end)
			{
				return errfc;
			}
			fc.fid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			p = getstr(p, end, &fc.name);
			if (p == 0)
			{
				return errfc;
			}
			if (p + 5 > end)
			{
				return errfc;
			}
			fc.perm = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			fc.mode = ((u32)(p)[0]);
			p++;
		}
		break;
		case Tread:
		{
			if (p + 16 > end)
			{
				return errfc;
			}
			fc.fid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			fc.offset = ((u64)from_le_u64(read_u64(p)));
			p += 8;
			fc.count = ((u32)from_le_u32(read_u32(p)));
			p += 4;
		}
		break;
		case Rread:
		{
			if (p + 4 > end)
			{
				return errfc;
			}
			fc.data.size = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			if (p + fc.data.size > end)
			{
				return errfc;
			}
			if (fc.data.size > 0)
			{
				fc.data.str = p;
				p += fc.data.size;
			}
			else
			{
				fc.data.str = 0;
			}
		}
		break;
		case Twrite:
		{
			if (p + 16 > end)
			{
				return errfc;
			}
			fc.fid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			fc.offset = ((u64)from_le_u64(read_u64(p)));
			p += 8;
			fc.data.size = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			if (p + fc.data.size > end)
			{
				return errfc;
			}
			if (fc.data.size > 0)
			{
				fc.data.str = p;
				p += fc.data.size;
			}
			else
			{
				fc.data.str = 0;
			}
		}
		break;
		case Rwrite:
		{
			if (p + 4 > end)
			{
				return errfc;
			}
			fc.count = ((u32)from_le_u32(read_u32(p)));
			p += 4;
		}
		break;
		case Tclunk:
		case Tremove:
		{
			if (p + 4 > end)
			{
				return errfc;
			}
			fc.fid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
		}
		break;
		case Rclunk:
		case Rremove:
			break;
		case Tstat:
		{
			if (p + 4 > end)
			{
				return errfc;
			}
			fc.fid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
		}
		break;
		case Rstat:
		{
			if (p + 2 > end)
			{
				return errfc;
			}
			fc.stat.size = ((u32)from_le_u16(read_u16(p)));
			p += 2;
			if (p + fc.stat.size > end)
			{
				return errfc;
			}
			if (fc.stat.size > 0)
			{
				fc.stat.str = p;
				p += fc.stat.size;
			}
			else
			{
				fc.stat.str = 0;
			}
		}
		break;
		case Twstat:
		{
			if (p + 6 > end)
			{
				return errfc;
			}
			fc.fid = ((u32)from_le_u32(read_u32(p)));
			p += 4;
			fc.stat.size = ((u32)from_le_u16(read_u16(p)));
			p += 2;
			if (p + fc.stat.size > end)
			{
				return errfc;
			}
			if (fc.stat.size > 0)
			{
				fc.stat.str = p;
				p += fc.stat.size;
			}
			else
			{
				fc.stat.str = 0;
			}
		}
		break;
		case Rwstat:
			break;
		default:
		{
			return errfc;
		}
	}
	if (p != end)
	{
		return errfc;
	}
	return fc;
}

static u32
dirsize(Dir d)
{
	u32 size = DIRFIXLEN;
	size += 2 + d.name.size;
	size += 2 + d.uid.size;
	size += 2 + d.gid.size;
	size += 2 + d.muid.size;
	return size;
}

static String8
direncode(Arena *a, Dir d)
{
	u32 msglen = dirsize(d);
	if (msglen == 0)
	{
		return str8_zero();
	}
	String8 msg = {
	    .str  = push_array_no_zero(a, u8, msglen),
	    .size = msglen,
	};
	u8 *p = msg.str;
	write_u16((p), from_le_u16(msg.size - 2));
	p += 2;
	write_u16((p), from_le_u16(d.type));
	p += 2;
	write_u32((p), from_le_u32(d.dev));
	p += 4;
	(p)[0] = (u8)(d.qid.type);
	p++;
	write_u32((p), from_le_u32(d.qid.vers));
	p += 4;
	write_u64((p), from_le_u64(d.qid.path));
	p += 8;
	write_u32((p), from_le_u32(d.mode));
	p += 4;
	write_u32((p), from_le_u32(d.atime));
	p += 4;
	write_u32((p), from_le_u32(d.mtime));
	p += 4;
	write_u64((p), from_le_u64(d.len));
	p += 8;
	p = putstr(p, d.name);
	p = putstr(p, d.uid);
	p = putstr(p, d.gid);
	p = putstr(p, d.muid);
	if (msg.size != (u64)(p - msg.str))
	{
		return str8_zero();
	}
	return msg;
}

static Dir
dirdecode(String8 msg)
{
	Dir d    = {0};
	Dir errd = {0};
	if (msg.size < DIRFIXLEN)
	{
		return errd;
	}
	u8 *p   = msg.str;
	u8 *end = msg.str + msg.size;
	p += 2;
	if (p + 39 > end)
	{
		return errd;
	}
	d.type = ((u32)from_le_u16(read_u16(p)));
	p += 2;
	d.dev = ((u32)from_le_u32(read_u32(p)));
	p += 4;
	d.qid.type = ((u32)(p)[0]);
	p++;
	d.qid.vers = ((u32)from_le_u32(read_u32(p)));
	p += 4;
	d.qid.path = ((u64)from_le_u64(read_u64(p)));
	p += 8;
	d.mode = ((u32)from_le_u32(read_u32(p)));
	p += 4;
	d.atime = ((u32)from_le_u32(read_u32(p)));
	p += 4;
	d.mtime = ((u32)from_le_u32(read_u32(p)));
	p += 4;
	d.len = ((u64)from_le_u64(read_u64(p)));
	p += 8;
	p = getstr(p, end, &d.name);
	if (p == 0)
	{
		return errd;
	}
	p = getstr(p, end, &d.uid);
	if (p == 0)
	{
		return errd;
	}
	p = getstr(p, end, &d.gid);
	if (p == 0)
	{
		return errd;
	}
	p = getstr(p, end, &d.muid);
	if (p == 0)
	{
		return errd;
	}
	if (p != end)
	{
		return errd;
	}
	return d;
}

static String8
read9pmsg(Arena *a, u64 fd)
{
	u8 lenbuf[4];
	u32 nread = 0;
	u32 nleft = 4;
	while (nleft > 0)
	{
		ssize_t n = read(fd, lenbuf + nread, nleft);
		if (n <= 0)
		{
			return str8_zero();
		}
		nread += n;
		nleft -= n;
	}
	u32 msglen  = ((u32)from_le_u32(read_u32(lenbuf)));
	String8 msg = {
	    .str  = push_array_no_zero(a, u8, msglen),
	    .size = msglen,
	};
	MemoryCopy(msg.str, lenbuf, sizeof lenbuf);
	nread = 0;
	nleft = msg.size - 4;
	while (nleft > 0)
	{
		ssize_t n = read(fd, msg.str + 4 + nread, nleft);
		if (n <= 0)
		{
			return str8_zero();
		}
		nread += n;
		nleft -= n;
	}
	return msg;
}

static void
dirlistpush(Arena *a, Dirlist *list, Dir d)
{
	Dirnode *node = push_array_no_zero(a, Dirnode, 1);
	node->dir     = d;
	if (list->start == 0)
	{
		list->start = node;
		list->end   = node;
	}
	else
	{
		list->end->next = node;
		list->end       = node;
	}
	list->cnt++;
}
