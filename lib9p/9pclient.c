static Cfsys *
fsinit(Arena *a, u64 fd)
{
	Cfsys *fs = pusharr(a, Cfsys, 1);
	fs->fd = fd;
	fs->nexttag = 1;
	fs->nextfid = 1;
	if (!fsversion(a, fs, 8192))
	{
		fs9unmount(a, fs);
		return NULL;
	}
	return fs;
}

static String8
getuser()
{
	uid_t uid = getuid();
	struct passwd *pw = getpwuid(uid);
	if (pw == NULL)
	{
		return str8lit("none");
	}
	return str8cstr(pw->pw_name);
}

static Cfsys *
fs9mount(Arena *a, u64 fd, String8 aname)
{
	Cfsys *fs = fsinit(a, fd);
	if (fs == NULL)
	{
		return NULL;
	}
	String8 user = getuser();
	Cfid *fid = fsattach(a, fs, NOFID, user, aname);
	if (fid == NULL)
	{
		fs9unmount(a, fs);
		return NULL;
	}
	fs->root = fid;
	return fs;
}

static void
fs9unmount(Arena *a, Cfsys *fs)
{
	fsclose(a, fs->root);
	fs->root = NULL;
	close(fs->fd);
	fs->fd = -1;
}

static void
debug9pprint(String8 dir, Fcall fc)
{
	if (!debug9pclient)
	{
		return;
	}
	fprintf(stderr, "%.*s ", str8varg(dir));
	switch (fc.type)
	{
		case Tversion:
		{
			fprintf(stderr, "Tversion tag=%u msize=%u version='%.*s'\n", fc.tag, fc.msize, str8varg(fc.version));
		}
		break;
		case Rversion:
		{
			fprintf(stderr, "Rversion tag=%u msize=%u version='%.*s'\n", fc.tag, fc.msize, str8varg(fc.version));
		}
		break;
		case Tauth:
		{
			fprintf(stderr, "Tauth tag=%u afid=%u uname='%.*s' aname='%.*s'\n", fc.tag, fc.afid, str8varg(fc.uname),
			        str8varg(fc.aname));
		}
		break;
		case Rauth:
		{
			fprintf(stderr, "Rauth tag=%u qid=(type=%u vers=%u path=%lu)\n", fc.tag, fc.aqid.type, fc.aqid.vers,
			        fc.aqid.path);
		}
		break;
		case Rerror:
		{
			fprintf(stderr, "Rerror tag=%u ename='%.*s'\n", fc.tag, str8varg(fc.ename));
		}
		break;
		case Tattach:
		{
			fprintf(stderr, "Tattach tag=%u fid=%u afid=%u uname='%.*s' aname='%.*s'\n", fc.tag, fc.fid, fc.afid,
			        str8varg(fc.uname), str8varg(fc.aname));
		}
		break;
		case Rattach:
		{
			fprintf(stderr, "Rattach tag=%u qid=(type=%u vers=%u path=%lu)\n", fc.tag, fc.qid.type, fc.qid.vers, fc.qid.path);
		}
		break;
		case Twalk:
		{
			fprintf(stderr, "Twalk tag=%u fid=%u newfid=%u nwname=%u", fc.tag, fc.fid, fc.newfid, fc.nwname);
			for (u32 i = 0; i < fc.nwname; i++)
			{
				fprintf(stderr, " '%.*s'", str8varg(fc.wname[i]));
			}
			fprintf(stderr, "\n");
		}
		break;
		case Rwalk:
		{
			fprintf(stderr, "Rwalk tag=%u nwqid=%u", fc.tag, fc.nwqid);
			for (u32 i = 0; i < fc.nwqid; i++)
			{
				fprintf(stderr, " qid%u=(type=%u vers=%u path=%lu)", i, fc.wqid[i].type, fc.wqid[i].vers, fc.wqid[i].path);
			}
			fprintf(stderr, "\n");
		}
		break;
		case Topen:
		{
			fprintf(stderr, "Topen tag=%u fid=%u mode=%u\n", fc.tag, fc.fid, fc.mode);
		}
		break;
		case Ropen:
		{
			fprintf(stderr, "Ropen tag=%u qid=(type=%u vers=%u path=%lu) iounit=%u\n", fc.tag, fc.qid.type, fc.qid.vers,
			        fc.qid.path, fc.iounit);
		}
		break;
		case Tcreate:
		{
			fprintf(stderr, "Tcreate tag=%u fid=%u name='%.*s' perm=%u mode=%u\n", fc.tag, fc.fid, str8varg(fc.name), fc.perm,
			        fc.mode);
		}
		break;
		case Rcreate:
		{
			fprintf(stderr, "Rcreate tag=%u qid=(type=%u vers=%u path=%lu) iounit=%u\n", fc.tag, fc.qid.type, fc.qid.vers,
			        fc.qid.path, fc.iounit);
		}
		break;
		case Tread:
		{
			fprintf(stderr, "Tread tag=%u fid=%u offset=%lu count=%u\n", fc.tag, fc.fid, fc.offset, fc.count);
		}
		break;
		case Rread:
		{
			fprintf(stderr, "Rread tag=%u count=%lu\n", fc.tag, fc.data.len);
		}
		break;
		case Twrite:
		{
			fprintf(stderr, "Twrite tag=%u fid=%u offset=%lu count=%lu\n", fc.tag, fc.fid, fc.offset, fc.data.len);
		}
		break;
		case Rwrite:
		{
			fprintf(stderr, "Rwrite tag=%u count=%u\n", fc.tag, fc.count);
		}
		break;
		case Tclunk:
		{
			fprintf(stderr, "Tclunk tag=%u fid=%u\n", fc.tag, fc.fid);
		}
		break;
		case Rclunk:
		{
			fprintf(stderr, "Rclunk tag=%u\n", fc.tag);
		}
		break;
		case Tremove:
		{
			fprintf(stderr, "Tremove tag=%u fid=%u\n", fc.tag, fc.fid);
		}
		break;
		case Rremove:
		{
			fprintf(stderr, "Rremove tag=%u\n", fc.tag);
		}
		break;
		case Tstat:
		{
			fprintf(stderr, "Tstat tag=%u fid=%u\n", fc.tag, fc.fid);
		}
		break;
		case Rstat:
		{
			fprintf(stderr, "Rstat tag=%u stat.len=%lu\n", fc.tag, fc.stat.len);
		}
		break;
		case Twstat:
		{
			fprintf(stderr, "Twstat tag=%u fid=%u stat.len=%lu\n", fc.tag, fc.fid, fc.stat.len);
		}
		break;
		case Rwstat:
		{
			fprintf(stderr, "Rwstat tag=%u\n", fc.tag);
		}
		break;
		default:
		{
			fprintf(stderr, "unknown type=%u tag=%u\n", fc.type, fc.tag);
		}
		break;
	}
}

static Fcall
fsrpc(Arena *a, Cfsys *fs, Fcall tx)
{
	Fcall errfc = {0};
	if (tx.type != Tversion)
	{
		tx.tag = fs->nexttag++;
		if (fs->nexttag == NOTAG)
		{
			fs->nexttag = 1;
		}
	}
	debug9pprint(str8lit("<-"), tx);
	String8 txmsg = fcallencode(a, tx);
	if (txmsg.len == 0)
	{
		return errfc;
	}
	ssize_t n = write(fs->fd, txmsg.str, txmsg.len);
	if (n < 0 || (u64)n != txmsg.len)
	{
		return errfc;
	}
	String8 rxmsg = read9pmsg(a, fs->fd);
	if (rxmsg.len == 0)
	{
		return errfc;
	}
	Fcall rx = fcalldecode(rxmsg);
	debug9pprint(str8lit("->"), rx);
	if (rx.type == 0 || rx.type == Rerror || rx.type != tx.type + 1)
	{
		return errfc;
	}
	if (rx.tag != tx.tag)
	{
		return errfc;
	}
	return rx;
}

static b32
fsversion(Arena *a, Cfsys *fs, u32 msize)
{
	Fcall tx = {0};
	tx.type = Tversion;
	tx.tag = NOTAG;
	tx.msize = msize;
	tx.version = version9p;
	Fcall rx = fsrpc(a, fs, tx);
	if (rx.type != Rversion)
	{
		return 0;
	}
	fs->msize = rx.msize;
	if (!str8cmp(rx.version, version9p, 0))
	{
		return 0;
	}
	return 1;
}

static Cfid *
fsauth(Arena *a, Cfsys *fs, String8 uname, String8 aname)
{
	Cfid *afid = pusharr(a, Cfid, 1);
	afid->fid = fs->nextfid++;
	afid->fs = fs;
	Fcall tx = {0};
	tx.type = Tauth;
	tx.afid = afid->fid;
	tx.uname = uname;
	tx.aname = aname;
	Fcall rx = fsrpc(a, fs, tx);
	if (rx.type != Rauth)
	{
		return NULL;
	}
	afid->qid = rx.aqid;
	return afid;
}

static Cfid *
fsattach(Arena *a, Cfsys *fs, u32 afid, String8 uname, String8 aname)
{
	Cfid *fid = pusharr(a, Cfid, 1);
	fid->fid = fs->nextfid++;
	fid->fs = fs;
	Fcall tx = {0};
	tx.type = Tattach;
	tx.fid = fid->fid;
	tx.afid = afid;
	tx.uname = uname;
	tx.aname = aname;
	Fcall rx = fsrpc(a, fs, tx);
	if (rx.type != Rattach)
	{
		return NULL;
	}
	fid->qid = rx.qid;
	return fid;
}

static void
fsclose(Arena *a, Cfid *fid)
{
	if (fid == NULL)
	{
		return;
	}
	Fcall tx = {0};
	tx.type = Tclunk;
	tx.fid = fid->fid;
	fsrpc(a, fid->fs, tx);
}

static Cfid *
fswalk(Arena *a, Cfid *fid, String8 path)
{
	if (fid == NULL)
	{
		return NULL;
	}
	Cfid *wfid = pusharr(a, Cfid, 1);
	Temp scratch = tempbegin(a);
	wfid->fid = fid->fs->nextfid++;
	wfid->qid = fid->qid;
	wfid->fs = fid->fs;
	b32 firstwalk = 1;
	String8list parts = str8split(scratch.a, path, (u8 *)"/", 1, 0);
	String8node *node = parts.start;
	Fcall tx = {0};
	tx.type = Twalk;
	tx.fid = fid->fid;
	tx.newfid = wfid->fid;
	if (node == NULL)
	{
		Fcall rx = fsrpc(a, fid->fs, tx);
		if (rx.type != Rwalk || rx.nwqid != tx.nwname)
		{
			return NULL;
		}
		return wfid;
	}
	while (node != NULL)
	{
		tx.fid = firstwalk ? fid->fid : wfid->fid;
		u64 i = 0;
		while (node != NULL && i < MAXWELEM)
		{
			String8 part = node->str;
			if (part.str[0] == '.' && part.len == 1)
			{
				node = node->next;
				continue;
			}
			tx.wname[i] = part;
			i++;
			node = node->next;
		}
		tx.nwname = i;
		Fcall rx = fsrpc(a, fid->fs, tx);
		if (rx.type != Rwalk || rx.nwqid != tx.nwname)
		{
			if (!firstwalk)
			{
				fsclose(a, wfid);
			}
			return NULL;
		}
		if (rx.nwqid > 0)
		{
			wfid->qid = rx.wqid[rx.nwqid - 1];
		}
		firstwalk = 0;
	}
	return wfid;
}

static b32
fsfcreate(Arena *a, Cfid *fid, String8 name, u32 mode, u32 perm)
{
	if (fid == NULL)
	{
		return 0;
	}
	Fcall tx = {0};
	tx.type = Tcreate;
	tx.fid = fid->fid;
	tx.name = name;
	tx.perm = perm;
	tx.mode = mode;
	Fcall rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rcreate)
	{
		return 0;
	}
	fid->mode = mode;
	return 1;
}

static Cfid *
fscreate(Arena *a, Cfsys *fs, String8 name, u32 mode, u32 perm)
{
	if (fs == NULL || fs->root == NULL)
	{
		return NULL;
	}
	String8 dir = str8dirname(name);
	String8 elem = str8basename(name);
	Cfid *fid = fswalk(a, fs->root, dir);
	if (fid == NULL)
	{
		return NULL;
	}
	if (!fsfcreate(a, fid, elem, mode, perm))
	{
		fsclose(a, fid);
		return NULL;
	}
	return fid;
}

static b32
fsfremove(Arena *a, Cfid *fid)
{
	if (fid == NULL)
	{
		return 0;
	}
	Fcall tx = {0};
	tx.type = Tremove;
	tx.fid = fid->fid;
	Fcall rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rremove)
	{
		return 0;
	}
	return 1;
}

static b32
fsremove(Arena *a, Cfsys *fs, String8 name)
{
	if (fs == NULL || fs->root == NULL)
	{
		return 0;
	}
	Cfid *fid = fswalk(a, fs->root, name);
	if (fid == NULL)
	{
		return 0;
	}
	if (!fsfremove(a, fid))
	{
		return 0;
	}
	return 1;
}

static b32
fsfopen(Arena *a, Cfid *fid, u32 mode)
{
	if (fid == NULL)
	{
		return 0;
	}
	Fcall tx = {0};
	tx.type = Topen;
	tx.fid = fid->fid;
	tx.mode = mode;
	Fcall rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Ropen)
	{
		return 0;
	}
	fid->mode = mode;
	return 1;
}

static Cfid *
fs9open(Arena *a, Cfsys *fs, String8 name, u32 mode)
{
	if (fs == NULL || fs->root == NULL)
	{
		return NULL;
	}
	Cfid *fid = fswalk(a, fs->root, name);
	if (fid == NULL)
	{
		return NULL;
	}
	if (!fsfopen(a, fid, mode))
	{
		fsclose(a, fid);
		return NULL;
	}
	return fid;
}

static s64
fspread(Arena *a, Cfid *fid, void *buf, u64 n, s64 offset)
{
	if (fid == NULL || buf == NULL)
	{
		return -1;
	}
	u32 msize = fid->fs->msize - IOHDRSZ;
	if (n > msize)
	{
		n = msize;
	}
	Fcall tx = {0};
	tx.type = Tread;
	tx.fid = fid->fid;
	tx.offset = (offset == -1) ? fid->offset : offset;
	tx.count = n;
	Fcall rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rread)
	{
		return -1;
	}
	s64 nr = rx.data.len;
	if (nr > (s64)n)
	{
		nr = n;
	}
	if (nr > 0)
	{
		memcpy(buf, rx.data.str, nr);
		if (offset == -1)
		{
			fid->offset += nr;
		}
	}
	return nr;
}

static s64
fsread(Arena *a, Cfid *fid, void *buf, u64 n)
{
	return fspread(a, fid, buf, n, -1);
}

static s64
fsreadn(Arena *a, Cfid *fid, void *buf, u64 n)
{
	u64 nread = 0;
	u64 nleft = n;
	u8 *p = buf;
	while (nleft > 0)
	{
		s64 nr = fsread(a, fid, p + nread, nleft);
		if (nr <= 0)
		{
			if (nread == 0)
			{
				return nr;
			}
			break;
		}
		nread += nr;
		nleft -= nr;
	}
	return nread;
}

static s64
fspwrite(Arena *a, Cfid *fid, void *buf, u64 n, s64 offset)
{
	if (fid == NULL || buf == NULL)
	{
		return -1;
	}
	u32 msize = fid->fs->msize - IOHDRSZ;
	u64 nwrite = 0;
	u64 nleft = n;
	u8 *p = buf;
	while (nleft > 0)
	{
		u64 want = nleft;
		if (want > msize)
		{
			want = msize;
		}
		Fcall tx = {0};
		tx.type = Twrite;
		tx.fid = fid->fid;
		tx.offset = (offset == -1) ? fid->offset : offset + nwrite;
		tx.data.len = want;
		tx.data.str = p + nwrite;
		Fcall rx = fsrpc(a, fid->fs, tx);
		if (rx.type != Rwrite)
		{
			if (nwrite == 0)
			{
				return -1;
			}
			break;
		}
		u32 got = rx.count;
		if (got == 0)
		{
			if (nwrite == 0)
			{
				return -1;
			}
			break;
		}
		nwrite += got;
		nleft -= got;
		if (offset == -1)
		{
			fid->offset += got;
		}
		if (got < want)
		{
			break;
		}
	}
	return nwrite;
}

static s64
fswrite(Arena *a, Cfid *fid, void *buf, u64 n)
{
	return fspwrite(a, fid, buf, n, -1);
}

static s64
dirpackage(Arena *a, u8 *buf, s64 ts, Dirlist *list)
{
	*list = (Dirlist){0};
	s64 n = 0;
	u64 i = 0;
	while (i < (u64)ts)
	{
		if (i + 2 > (u64)ts)
		{
			return -1;
		}
		u64 m = 2 + getb2(&buf[i]);
		if (i + m > (u64)ts)
		{
			return -1;
		}
		String8 dirmsg = {.str = &buf[i], .len = m};
		Dir d = dirdecode(dirmsg);
		if (d.name.len == 0 && m > 2)
		{
			return -1;
		}
		dirlistpush(a, list, d);
		n++;
		i += m;
	}
	return n;
}

static s64
fsdirread(Arena *a, Cfid *fid, Dirlist *list)
{
	if (fid == NULL || list == NULL)
	{
		return -1;
	}
	Temp scratch = tempbegin(a);
	u8 *buf = pusharrnoz(a, u8, DIRMAX);
	s64 ts = fsread(a, fid, buf, DIRMAX);
	if (ts >= 0)
	{
		ts = dirpackage(a, buf, ts, list);
	}
	tempend(scratch);
	return ts;
}

static s64
fsdirreadall(Arena *a, Cfid *fid, Dirlist *list)
{
	if (fid == NULL || list == NULL)
	{
		return -1;
	}
	u8 *buf = pusharrnoz(a, u8, DIRBUFMAX);
	s64 ts = 0;
	u64 nleft = DIRBUFMAX;
	s64 n = 0;
	while (nleft >= DIRMAX)
	{
		n = fsread(a, fid, buf + ts, DIRMAX);
		if (n <= 0)
		{
			break;
		}
		ts += n;
		nleft -= n;
	}
	if (ts >= 0)
	{
		ts = dirpackage(a, buf, ts, list);
	}
	if (ts == 0 && n < 0)
	{
		return -1;
	}
	return ts;
}

static Dir
fsdirfstat(Arena *a, Cfid *fid)
{
	Dir errd = {0};
	if (fid == NULL)
	{
		return errd;
	}
	Fcall tx = {0};
	tx.type = Tstat;
	tx.fid = fid->fid;
	Fcall rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rstat)
	{
		return errd;
	}
	Dir d = dirdecode(rx.stat);
	return d;
}

static Dir
fsdirstat(Arena *a, Cfsys *fs, String8 name)
{
	Dir errd = {0};
	if (fs == NULL || fs->root == NULL)
	{
		return errd;
	}
	Cfid *fid = fswalk(a, fs->root, name);
	if (fid == NULL)
	{
		return errd;
	}
	Dir d = fsdirfstat(a, fid);
	fsclose(a, fid);
	return d;
}

static b32
fsdirfwstat(Arena *a, Cfid *fid, Dir d)
{
	if (fid == NULL)
	{
		return 0;
	}
	Temp scratch = tempbegin(a);
	String8 stat = direncode(scratch.a, d);
	if (stat.len == 0)
	{
		return 0;
	}
	Fcall tx = {0};
	tx.type = Twstat;
	tx.fid = fid->fid;
	tx.stat = stat;
	Fcall rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rwstat)
	{
		return 0;
	}
	return 1;
}

static b32
fsdirwstat(Arena *a, Cfsys *fs, String8 name, Dir d)
{
	if (fs == NULL || fs->root == NULL)
	{
		return 0;
	}
	Cfid *fid = fswalk(a, fs->root, name);
	if (fid == NULL)
	{
		return 0;
	}
	b32 ok = fsdirfwstat(a, fid, d);
	fsclose(a, fid);
	return ok;
}

static b32
fsaccess(Arena *a, Cfsys *fs, String8 name, u32 mode)
{
	if (fs == NULL || fs->root == NULL)
	{
		return 0;
	}
	if (mode == AEXIST)
	{
		Dir d = fsdirstat(a, fs, name);
		if (d.name.len == 0)
		{
			return 0;
		}
		return 1;
	}
	Cfid *fid = fs9open(a, fs, name, omodetab[mode & 7]);
	if (fid == NULL)
	{
		return 0;
	}
	fsclose(a, fid);
	return 1;
}

static s64
fsseek(Arena *a, Cfid *fid, s64 offset, u32 type)
{
	if (fid == NULL)
	{
		return -1;
	}
	s64 pos = 0;
	switch (type)
	{
		case SEEKSET:
		{
			pos = offset;
			fid->offset = offset;
		}
		break;
		case SEEKCUR:
		{
			pos = (s64)fid->offset + offset;
			if (pos < 0)
			{
				return -1;
			}
			fid->offset = pos;
		}
		break;
		case SEEKEND:
		{
			Dir d = fsdirfstat(a, fid);
			if (d.name.len == 0)
			{
				return -1;
			}
			pos = (s64)d.len + offset;
			if (pos < 0)
			{
				return -1;
			}
			fid->offset = pos;
		}
		break;
		default:
		{
			return -1;
		}
	}
	return pos;
}
