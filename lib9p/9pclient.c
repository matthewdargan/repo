static Cfsys *
fsinit(Arena *a, u64 fd)
{
	Cfsys *fs;

	fs = pusharr(a, Cfsys, 1);
	fs->fd = fd;
	fs->nexttag = 1;
	fs->nextfid = 1;
	if (!fsversion(a, fs, 8192)) {
		fs9unmount(a, fs);
		return NULL;
	}
	return fs;
}

static String8
getuser()
{
	uid_t uid;
	struct passwd *pw;

	uid = getuid();
	pw = getpwuid(uid);
	if (pw == NULL)
		return str8lit("none");
	return str8cstr(pw->pw_name);
}

static Cfsys *
fs9mount(Arena *a, u64 fd, String8 aname)
{
	Cfsys *fs;
	String8 user;
	Cfid *fid;

	fs = fsinit(a, fd);
	if (fs == NULL)
		return NULL;
	user = getuser();
	fid = fsattach(a, fs, NOFID, user, aname);
	if (fid == NULL) {
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
	u32 i;

	if (!debug9p)
		return;
	fprintf(stderr, "%.*s ", dir.len, dir.str);
	switch (fc.type) {
		case Tversion:
			fprintf(stderr, "Tversion tag=%u msize=%u version='%.*s'\n", fc.tag, fc.msize, fc.version.len,
			        fc.version.str);
			break;
		case Rversion:
			fprintf(stderr, "Rversion tag=%u msize=%u version='%.*s'\n", fc.tag, fc.msize, fc.version.len,
			        fc.version.str);
			break;
		case Tauth:
			fprintf(stderr, "Tauth tag=%u afid=%u uname='%.*s' aname='%.*s'\n", fc.tag, fc.afid, fc.uname.len,
			        fc.uname.str, fc.aname.len, fc.aname.str);
			break;
		case Rauth:
			fprintf(stderr, "Rauth tag=%u qid=(type=%u vers=%u path=%llu)\n", fc.tag, fc.aqid.type, fc.aqid.vers,
			        fc.aqid.path);
			break;
		case Rerror:
			fprintf(stderr, "Rerror tag=%u ename='%.*s'\n", fc.tag, fc.ename.len, fc.ename.str);
			break;
		case Tattach:
			fprintf(stderr, "Tattach tag=%u fid=%u afid=%u uname='%.*s' aname='%.*s'\n", fc.tag, fc.fid, fc.afid,
			        fc.uname.len, fc.uname.str, fc.aname.len, fc.aname.str);
			break;
		case Rattach:
			fprintf(stderr, "Rattach tag=%u qid=(type=%u vers=%u path=%llu)\n", fc.tag, fc.qid.type, fc.qid.vers,
			        fc.qid.path);
			break;
		case Twalk:
			fprintf(stderr, "Twalk tag=%u fid=%u newfid=%u nwname=%u", fc.tag, fc.fid, fc.newfid, fc.nwname);
			for (i = 0; i < fc.nwname; i++) {
				fprintf(stderr, " '%.*s'", fc.wname[i].len, fc.wname[i].str);
			}
			fprintf(stderr, "\n");
			break;
		case Rwalk:
			fprintf(stderr, "Rwalk tag=%u nwqid=%u", fc.tag, fc.nwqid);
			for (i = 0; i < fc.nwqid; i++) {
				fprintf(stderr, " qid%u=(type=%u vers=%u path=%llu)", i, fc.wqid[i].type, fc.wqid[i].vers,
				        fc.wqid[i].path);
			}
			fprintf(stderr, "\n");
			break;
		case Topen:
			fprintf(stderr, "Topen tag=%u fid=%u mode=%u\n", fc.tag, fc.fid, fc.mode);
			break;
		case Ropen:
			fprintf(stderr, "Ropen tag=%u qid=(type=%u vers=%u path=%llu) iounit=%u\n", fc.tag, fc.qid.type,
			        fc.qid.vers, fc.qid.path, fc.iounit);
			break;
		case Tcreate:
			fprintf(stderr, "Tcreate tag=%u fid=%u name='%.*s' perm=%u mode=%u\n", fc.tag, fc.fid, fc.name.len,
			        fc.name.str, fc.perm, fc.mode);
			break;
		case Rcreate:
			fprintf(stderr, "Rcreate tag=%u qid=(type=%u vers=%u path=%llu) iounit=%u\n", fc.tag, fc.qid.type,
			        fc.qid.vers, fc.qid.path, fc.iounit);
			break;
		case Tread:
			fprintf(stderr, "Tread tag=%u fid=%u offset=%llu count=%u\n", fc.tag, fc.fid, fc.offset, fc.count);
			break;
		case Rread:
			fprintf(stderr, "Rread tag=%u count=%llu\n", fc.tag, fc.data.len);
			break;
		case Twrite:
			fprintf(stderr, "Twrite tag=%u fid=%u offset=%llu count=%llu\n", fc.tag, fc.fid, fc.offset, fc.data.len);
			break;
		case Rwrite:
			fprintf(stderr, "Rwrite tag=%u count=%u\n", fc.tag, fc.count);
			break;
		case Tclunk:
			fprintf(stderr, "Tclunk tag=%u fid=%u\n", fc.tag, fc.fid);
			break;
		case Rclunk:
			fprintf(stderr, "Rclunk tag=%u\n", fc.tag);
			break;
		case Tremove:
			fprintf(stderr, "Tremove tag=%u fid=%u\n", fc.tag, fc.fid);
			break;
		case Rremove:
			fprintf(stderr, "Rremove tag=%u\n", fc.tag);
			break;
		case Tstat:
			fprintf(stderr, "Tstat tag=%u fid=%u\n", fc.tag, fc.fid);
			break;
		case Rstat:
			fprintf(stderr, "Rstat tag=%u stat.len=%llu\n", fc.tag, fc.stat.len);
			break;
		case Twstat:
			fprintf(stderr, "Twstat tag=%u fid=%u stat.len=%llu\n", fc.tag, fc.fid, fc.stat.len);
			break;
		case Rwstat:
			fprintf(stderr, "Rwstat tag=%u\n", fc.tag);
			break;
		default:
			fprintf(stderr, "unknown type=%u tag=%u\n", fc.type, fc.tag);
			break;
	}
}

static Fcall
fsrpc(Arena *a, Cfsys *fs, Fcall tx)
{
	Fcall rx, errfc;
	String8 txmsg, rxmsg;
	ssize_t n;

	memset(&errfc, 0, sizeof errfc);
	if (tx.type != Tversion) {
		tx.tag = fs->nexttag++;
		if (fs->nexttag == NOTAG)
			fs->nexttag = 1;
	}
	debug9pprint(str8lit("<-"), tx);
	txmsg = fcallencode(a, tx);
	if (txmsg.len == 0)
		return errfc;
	n = write(fs->fd, txmsg.str, txmsg.len);
	if (n < 0 || (u64)n != txmsg.len)
		return errfc;
	rxmsg = read9pmsg(a, fs->fd);
	if (rxmsg.len == 0)
		return errfc;
	rx = fcalldecode(rxmsg);
	if (rx.type == 0 || rx.type == Rerror || rx.type != tx.type + 1)
		return errfc;
	if (rx.tag != tx.tag)
		return errfc;
	debug9pprint(str8lit("->"), rx);
	return rx;
}

static b32
fsversion(Arena *a, Cfsys *fs, u32 msize)
{
	Fcall tx, rx;

	memset(&tx, 0, sizeof tx);
	tx.type = Tversion;
	tx.tag = NOTAG;
	tx.msize = msize;
	tx.version = version9p;
	rx = fsrpc(a, fs, tx);
	if (rx.type != Rversion)
		return 0;
	fs->msize = rx.msize;
	if (!str8cmp(rx.version, version9p, 0))
		return 0;
	return 1;
}

static Cfid *
fsauth(Arena *a, Cfsys *fs, String8 uname, String8 aname)
{
	Fcall tx, rx;
	Cfid *afid;

	afid = pusharr(a, Cfid, 1);
	afid->fid = fs->nextfid++;
	afid->fs = fs;
	memset(&tx, 0, sizeof tx);
	tx.type = Tauth;
	tx.afid = afid->fid;
	tx.uname = uname;
	tx.aname = aname;
	rx = fsrpc(a, fs, tx);
	if (rx.type != Rauth)
		return NULL;
	afid->qid = rx.aqid;
	return afid;
}

static Cfid *
fsattach(Arena *a, Cfsys *fs, u32 afid, String8 uname, String8 aname)
{
	Fcall tx, rx;
	Cfid *fid;

	fid = pusharr(a, Cfid, 1);
	fid->fid = fs->nextfid++;
	fid->fs = fs;
	memset(&tx, 0, sizeof tx);
	tx.type = Tattach;
	tx.fid = fid->fid;
	tx.afid = afid;
	tx.uname = uname;
	tx.aname = aname;
	rx = fsrpc(a, fs, tx);
	if (rx.type != Rattach)
		return NULL;
	fid->qid = rx.qid;
	return fid;
}

static void
fsclose(Arena *a, Cfid *fid)
{
	Fcall tx;

	if (fid == NULL)
		return;
	memset(&tx, 0, sizeof tx);
	tx.type = Tclunk;
	tx.fid = fid->fid;
	fsrpc(a, fid->fs, tx);
}

static Cfid *
fswalk(Arena *a, Cfid *fid, String8 path)
{
	Temp scratch;
	Fcall tx, rx;
	Cfid *wfid;
	b32 firstwalk;
	String8list parts;
	String8node *node;
	String8 part;
	u64 i;

	if (fid == NULL)
		return NULL;
	wfid = pusharr(a, Cfid, 1);
	scratch = tempbegin(a);
	wfid->fid = fid->fs->nextfid++;
	wfid->qid = fid->qid;
	wfid->fs = fid->fs;
	firstwalk = 1;
	parts = str8split(scratch.a, path, (u8 *)"/", 1, 0);
	node = parts.start;
	memset(&tx, 0, sizeof tx);
	tx.type = Twalk;
	tx.fid = fid->fid;
	tx.newfid = wfid->fid;
	if (node == NULL) {
		rx = fsrpc(a, fid->fs, tx);
		if (rx.type != Rwalk || rx.nwqid != tx.nwname)
			return NULL;
		return wfid;
	}
	while (node != NULL) {
		tx.fid = firstwalk ? fid->fid : wfid->fid;
		for (i = 0; node != NULL && i < MAXWELEM; node = node->next) {
			part = node->str;
			if (part.str[0] == '.' && part.len == 1)
				continue;
			tx.wname[i] = part;
			i++;
		}
		tx.nwname = i;
		rx = fsrpc(a, fid->fs, tx);
		if (rx.type != Rwalk || rx.nwqid != tx.nwname) {
			if (!firstwalk)
				fsclose(a, wfid);
			return NULL;
		}
		if (rx.nwqid > 0)
			wfid->qid = rx.wqid[rx.nwqid - 1];
		firstwalk = 0;
	}
	return wfid;
}

static b32
fsfcreate(Arena *a, Cfid *fid, String8 name, u32 mode, u32 perm)
{
	Fcall tx, rx;

	if (fid == NULL)
		return 0;
	memset(&tx, 0, sizeof tx);
	tx.type = Tcreate;
	tx.fid = fid->fid;
	tx.name = name;
	tx.perm = perm;
	tx.mode = mode;
	rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rcreate)
		return 0;
	fid->mode = mode;
	return 1;
}

static Cfid *
fscreate(Arena *a, Cfsys *fs, String8 name, u32 mode, u32 perm)
{
	Cfid *fid;
	String8 dir, elem;

	if (fs == NULL || fs->root == NULL)
		return NULL;
	dir = str8dirname(name);
	elem = str8basename(name);
	fid = fswalk(a, fs->root, dir);
	if (fid == NULL) {
		return NULL;
	}
	if (!fsfcreate(a, fid, elem, mode, perm)) {
		fsclose(a, fid);
		return NULL;
	}
	return fid;
}

static b32
fsfremove(Arena *a, Cfid *fid)
{
	Fcall tx, rx;

	if (fid == NULL)
		return 0;
	memset(&tx, 0, sizeof tx);
	tx.type = Tremove;
	tx.fid = fid->fid;
	rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rremove)
		return 0;
	return 1;
}

static b32
fsremove(Arena *a, Cfsys *fs, String8 name)
{
	Cfid *fid;

	if (fs == NULL || fs->root == NULL)
		return 0;
	fid = fswalk(a, fs->root, name);
	if (fid == NULL) {
		return 0;
	}
	if (!fsfremove(a, fid))
		return 0;
	return 1;
}

static b32
fsfopen(Arena *a, Cfid *fid, u32 mode)
{
	Fcall tx, rx;

	if (fid == NULL)
		return 0;
	memset(&tx, 0, sizeof tx);
	tx.type = Topen;
	tx.fid = fid->fid;
	tx.mode = mode;
	rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Ropen)
		return 0;
	fid->mode = mode;
	return 1;
}

static Cfid *
fs9open(Arena *a, Cfsys *fs, String8 name, u32 mode)
{
	Cfid *fid;

	if (fs == NULL || fs->root == NULL)
		return NULL;
	fid = fswalk(a, fs->root, name);
	if (fid == NULL) {
		return NULL;
	}
	if (!fsfopen(a, fid, mode)) {
		fsclose(a, fid);
		return NULL;
	}
	return fid;
}

static s64
fspread(Arena *a, Cfid *fid, void *buf, u64 n, s64 offset)
{
	Fcall tx, rx;
	u32 msize;
	s64 nr;

	if (fid == NULL || buf == NULL)
		return -1;
	msize = fid->fs->msize - IOHDRSZ;
	if (n > msize)
		n = msize;
	memset(&tx, 0, sizeof tx);
	tx.type = Tread;
	tx.fid = fid->fid;
	if (offset == -1)
		tx.offset = fid->offset;
	else
		tx.offset = offset;
	tx.count = n;
	rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rread)
		return -1;
	nr = rx.data.len;
	if (nr > (s64)n)
		nr = n;
	if (nr > 0) {
		memcpy(buf, rx.data.str, nr);
		if (offset == -1)
			fid->offset += nr;
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
	u64 nread, nleft;
	s64 nr;
	u8 *p;

	nread = 0;
	nleft = n;
	p = buf;
	while (nleft > 0) {
		nr = fsread(a, fid, p + nread, nleft);
		if (nr <= 0) {
			if (nread == 0)
				return nr;
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
	Fcall tx, rx;
	u32 msize, got;
	u64 nwrite, nleft, want;
	u8 *p;

	if (fid == NULL || buf == NULL)
		return -1;
	msize = fid->fs->msize - IOHDRSZ;
	nwrite = 0;
	nleft = n;
	p = buf;
	while (nleft > 0) {
		want = nleft;
		if (want > msize)
			want = msize;
		memset(&tx, 0, sizeof tx);
		tx.type = Twrite;
		tx.fid = fid->fid;
		if (offset == -1)
			tx.offset = fid->offset;
		else
			tx.offset = offset + nwrite;
		tx.data.len = want;
		tx.data.str = p + nwrite;
		rx = fsrpc(a, fid->fs, tx);
		if (rx.type != Rwrite) {
			if (nwrite == 0)
				return -1;
			break;
		}
		got = rx.count;
		if (got == 0) {
			if (nwrite == 0)
				return -1;
			break;
		}
		nwrite += got;
		nleft -= got;
		if (offset == -1)
			fid->offset += got;
		if (got < want)
			break;
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
	s64 n;
	u64 i, m;
	String8 dirmsg;
	Dir d;

	memset(list, 0, sizeof *list);
	n = 0;
	for (i = 0; i < (u64)ts; i += m) {
		if (i + 2 > (u64)ts)
			return -1;
		m = 2 + getb2(&buf[i]);
		if (i + m > (u64)ts)
			return -1;
		dirmsg.str = &buf[i];
		dirmsg.len = m;
		d = dirdecode(dirmsg);
		if (d.name.len == 0 && m > 2)
			return -1;
		dirlistpush(a, list, d);
		n++;
	}
	return n;
}

static s64
fsdirread(Arena *a, Cfid *fid, Dirlist *list)
{
	Temp scratch;
	u8 *buf;
	s64 ts;

	if (fid == NULL || list == NULL)
		return -1;
	scratch = tempbegin(a);
	buf = pusharrnoz(a, u8, DIRMAX);
	ts = fsread(a, fid, buf, DIRMAX);
	if (ts >= 0)
		ts = dirpackage(a, buf, ts, list);
	tempend(scratch);
	return ts;
}

static s64
fsdirreadall(Arena *a, Cfid *fid, Dirlist *list)
{
	Temp scratch;
	u8 *buf;
	s64 ts, n;
	u64 nleft;

	if (fid == NULL || list == NULL)
		return -1;
	scratch = tempbegin(a);
	buf = pusharrnoz(a, u8, DIRBUFMAX);
	ts = 0;
	nleft = DIRBUFMAX;
	while (nleft >= DIRMAX) {
		n = fsread(a, fid, buf + ts, DIRMAX);
		if (n <= 0)
			break;
		ts += n;
		nleft -= n;
	}
	if (ts >= 0)
		ts = dirpackage(a, buf, ts, list);
	tempend(scratch);
	if (ts == 0 && n < 0)
		return -1;
	return ts;
}

static Dir
fsdirfstat(Arena *a, Cfid *fid)
{
	Fcall tx, rx;
	Dir d, errd;

	memset(&errd, 0, sizeof errd);
	if (fid == NULL)
		return errd;
	memset(&tx, 0, sizeof tx);
	tx.type = Tstat;
	tx.fid = fid->fid;
	rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rstat)
		return errd;
	d = dirdecode(rx.stat);
	return d;
}

static Dir
fsdirstat(Arena *a, Cfsys *fs, String8 name)
{
	Cfid *fid;
	Dir d, errd;

	memset(&errd, 0, sizeof errd);
	if (fs == NULL || fs->root == NULL)
		return errd;
	fid = fswalk(a, fs->root, name);
	if (fid == NULL)
		return errd;
	d = fsdirfstat(a, fid);
	fsclose(a, fid);
	return d;
}

static b32
fsdirfwstat(Arena *a, Cfid *fid, Dir d)
{
	Temp scratch;
	Fcall tx, rx;
	String8 stat;

	if (fid == NULL)
		return 0;
	scratch = tempbegin(a);
	stat = direncode(scratch.a, d);
	if (stat.len == 0)
		return 0;
	memset(&tx, 0, sizeof tx);
	tx.type = Twstat;
	tx.fid = fid->fid;
	tx.stat = stat;
	rx = fsrpc(a, fid->fs, tx);
	if (rx.type != Rwstat)
		return 0;
	return 1;
}

static b32
fsdirwstat(Arena *a, Cfsys *fs, String8 name, Dir d)
{
	Cfid *fid;
	b32 ok;

	if (fs == NULL || fs->root == NULL)
		return 0;
	fid = fswalk(a, fs->root, name);
	if (fid == NULL)
		return 0;
	ok = fsdirfwstat(a, fid, d);
	fsclose(a, fid);
	return ok;
}

static b32
fsaccess(Arena *a, Cfsys *fs, String8 name, u32 mode)
{
	Cfid *fid;
	Dir d;

	if (fs == NULL || fs->root == NULL)
		return 0;
	if (mode == AEXIST) {
		d = fsdirstat(a, fs, name);
		if (d.name.len == 0)
			return 0;
		return 1;
	}
	fid = fs9open(a, fs, name, omodetab[mode & 7]);
	if (fid == NULL)
		return 0;
	fsclose(a, fid);
	return 1;
}

static s64
fsseek(Arena *a, Cfid *fid, s64 offset, u32 type)
{
	Dir d;
	s64 pos;

	if (fid == NULL)
		return -1;
	switch (type) {
		case SEEKSET:
			pos = offset;
			fid->offset = offset;
		case SEEKCUR:
			pos = (s64)fid->offset + offset;
			if (pos < 0)
				return -1;
			fid->offset = pos;
			break;
		case SEEKEND:
			d = fsdirfstat(a, fid);
			if (d.name.len == 0)
				return -1;
			pos = (s64)d.len + offset;
			if (pos < 0)
				return -1;
			fid->offset = pos;
			break;
		default:
			return -1;
	}
	return pos;
}
