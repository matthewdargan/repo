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
	return rx;
}

static b32
fsversion(Arena *a, Cfsys *fs, u32 msize)
{
	Temp scratch;
	Fcall tx, rx;

	scratch = tempbegin(a);
	memset(&tx, 0, sizeof tx);
	tx.type = Tversion;
	tx.tag = NOTAG;
	tx.msize = msize;
	tx.version = version9p;
	rx = fsrpc(scratch.a, fs, tx);
	if (rx.type != Rversion) {
		tempend(scratch);
		return 0;
	}
	fs->msize = rx.msize;
	if (!str8cmp(rx.version, version9p, 0)) {
		tempend(scratch);
		return 0;
	}
	tempend(scratch);
	return 1;
}

static Cfid *
fsauth(Arena *a, Cfsys *fs, String8 uname, String8 aname)
{
	Temp scratch;
	Fcall tx, rx;
	Cfid *afid;

	scratch = tempbegin(a);
	afid = pusharr(a, Cfid, 1);
	afid->fid = fs->nextfid++;
	afid->fs = fs;
	memset(&tx, 0, sizeof tx);
	tx.type = Tauth;
	tx.afid = afid->fid;
	tx.uname = uname;
	tx.aname = aname;
	rx = fsrpc(scratch.a, fs, tx);
	if (rx.type != Rauth) {
		tempend(scratch);
		return NULL;
	}
	afid->qid = rx.aqid;
	tempend(scratch);
	return afid;
}

static Cfid *
fsattach(Arena *a, Cfsys *fs, u32 afid, String8 uname, String8 aname)
{
	Temp scratch;
	Fcall tx, rx;
	Cfid *fid;

	scratch = tempbegin(a);
	fid = pusharr(a, Cfid, 1);
	fid->fid = fs->nextfid++;
	fid->fs = fs;
	memset(&tx, 0, sizeof tx);
	tx.type = Tattach;
	tx.fid = fid->fid;
	tx.afid = afid;
	tx.uname = uname;
	tx.aname = aname;
	rx = fsrpc(scratch.a, fs, tx);
	if (rx.type != Rattach) {
		tempend(scratch);
		return NULL;
	}
	fid->qid = rx.qid;
	tempend(scratch);
	return fid;
}

static void
fsclose(Arena *a, Cfid *fid)
{
	Temp scratch;
	Fcall tx;

	if (fid == NULL)
		return;
	scratch = tempbegin(a);
	memset(&tx, 0, sizeof tx);
	tx.type = Tclunk;
	tx.fid = fid->fid;
	fsrpc(scratch.a, fid->fs, tx);
	tempend(scratch);
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
	scratch = tempbegin(a);
	wfid = pusharr(a, Cfid, 1);
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
		rx = fsrpc(scratch.a, fid->fs, tx);
		if (rx.type != Rwalk || rx.nwqid != tx.nwname) {
			tempend(scratch);
			return NULL;
		}
		tempend(scratch);
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
		rx = fsrpc(scratch.a, fid->fs, tx);
		if (rx.type != Rwalk || rx.nwqid != tx.nwname) {
			if (!firstwalk)
				fsclose(scratch.a, wfid);
			tempend(scratch);
			return NULL;
		}
		if (rx.nwqid > 0)
			wfid->qid = rx.wqid[rx.nwqid - 1];
		firstwalk = 0;
	}
	tempend(scratch);
	return wfid;
}

static b32
fsfcreate(Arena *a, Cfid *fid, String8 name, u32 mode, u32 perm)
{
	Temp scratch;
	Fcall tx, rx;

	if (fid == NULL)
		return 0;
	scratch = tempbegin(a);
	memset(&tx, 0, sizeof tx);
	tx.type = Tcreate;
	tx.fid = fid->fid;
	tx.name = name;
	tx.perm = perm;
	tx.mode = mode;
	rx = fsrpc(scratch.a, fid->fs, tx);
	if (rx.type != Rcreate) {
		tempend(scratch);
		return 0;
	}
	fid->mode = mode;
	tempend(scratch);
	return 1;
}

static Cfid *
fscreate(Arena *a, Cfsys *fs, String8 name, u32 mode, u32 perm)
{
	Temp scratch;
	Cfid *fid;
	String8 dir, elem;

	if (fs == NULL || fs->root == NULL)
		return NULL;
	scratch = tempbegin(a);
	dir = str8dirname(name);
	elem = str8basename(name);
	fid = fswalk(a, fs->root, dir);
	if (fid == NULL) {
		tempend(scratch);
		return NULL;
	}
	if (!fsfcreate(scratch.a, fid, elem, mode, perm)) {
		fsclose(scratch.a, fid);
		tempend(scratch);
		return NULL;
	}
	tempend(scratch);
	return fid;
}

static b32
fsfremove(Arena *a, Cfid *fid)
{
	Temp scratch;
	Fcall tx, rx;

	if (fid == NULL)
		return 0;
	scratch = tempbegin(a);
	memset(&tx, 0, sizeof tx);
	tx.type = Tremove;
	tx.fid = fid->fid;
	rx = fsrpc(scratch.a, fid->fs, tx);
	if (rx.type != Rremove) {
		tempend(scratch);
		return 0;
	}
	tempend(scratch);
	return 1;
}

static b32
fsremove(Arena *a, Cfsys *fs, String8 name)
{
	Temp scratch;
	Cfid *fid;

	if (fs == NULL || fs->root == NULL)
		return 0;
	scratch = tempbegin(a);
	fid = fswalk(a, fs->root, name);
	if (fid == NULL) {
		tempend(scratch);
		return 0;
	}
	if (!fsfremove(scratch.a, fid)) {
		tempend(scratch);
		return 0;
	}
	tempend(scratch);
	return 1;
}

static b32
fsfopen(Arena *a, Cfid *fid, u32 mode)
{
	Temp scratch;
	Fcall tx, rx;

	if (fid == NULL)
		return 0;
	scratch = tempbegin(a);
	memset(&tx, 0, sizeof tx);
	tx.type = Topen;
	tx.fid = fid->fid;
	tx.mode = mode;
	rx = fsrpc(scratch.a, fid->fs, tx);
	if (rx.type != Ropen) {
		tempend(scratch);
		return 0;
	}
	fid->mode = mode;
	tempend(scratch);
	return 1;
}

static Cfid *
fs9open(Arena *a, Cfsys *fs, String8 name, u32 mode)
{
	Temp scratch;
	Cfid *fid;

	if (fs == NULL || fs->root == NULL)
		return NULL;
	scratch = tempbegin(a);
	fid = fswalk(a, fs->root, name);
	if (fid == NULL) {
		tempend(scratch);
		return NULL;
	}
	if (!fsfopen(scratch.a, fid, mode)) {
		fsclose(scratch.a, fid);
		tempend(scratch);
		return NULL;
	}
	tempend(scratch);
	return fid;
}

static s64
fspread(Arena *a, Cfid *fid, void *buf, u64 n, s64 offset)
{
	Temp scratch;
	Fcall tx, rx;
	u32 msize;
	s64 nr;

	if (fid == NULL || buf == NULL)
		return -1;
	scratch = tempbegin(a);
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
	rx = fsrpc(scratch.a, fid->fs, tx);
	if (rx.type != Rread) {
		tempend(scratch);
		return -1;
	}
	nr = rx.data.len;
	if (nr > (s64)n)
		nr = n;
	if (nr > 0) {
		memcpy(buf, rx.data.str, nr);
		if (offset == -1)
			fid->offset += nr;
	}
	tempend(scratch);
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
	Temp scratch;
	Fcall tx, rx;
	u32 msize, got;
	u64 nwrite, nleft, want;
	u8 *p;

	if (fid == NULL || buf == NULL)
		return -1;
	scratch = tempbegin(a);
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
		rx = fsrpc(scratch.a, fid->fs, tx);
		if (rx.type != Rwrite) {
			if (nwrite == 0) {
				tempend(scratch);
				return -1;
			}
			break;
		}
		got = rx.count;
		if (got == 0) {
			if (nwrite == 0) {
				tempend(scratch);
				return -1;
			}
			break;
		}
		nwrite += got;
		nleft -= got;
		if (offset == -1)
			fid->offset += got;
		if (got < want)
			break;
	}
	tempend(scratch);
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
	buf = pusharrnoz(scratch.a, u8, DIRMAX);
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
	buf = pusharrnoz(scratch.a, u8, DIRBUFMAX);
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
	Temp scratch;
	Fcall tx, rx;
	Dir d, errd;

	memset(&errd, 0, sizeof errd);
	if (fid == NULL)
		return errd;
	scratch = tempbegin(a);
	memset(&tx, 0, sizeof tx);
	tx.type = Tstat;
	tx.fid = fid->fid;
	rx = fsrpc(scratch.a, fid->fs, tx);
	if (rx.type != Rstat) {
		tempend(scratch);
		return errd;
	}
	d = dirdecode(rx.stat);
	tempend(scratch);
	return d;
}

static Dir
fsdirstat(Arena *a, Cfsys *fs, String8 name)
{
	Temp scratch;
	Cfid *fid;
	Dir d, errd;

	memset(&errd, 0, sizeof errd);
	if (fs == NULL || fs->root == NULL)
		return errd;
	scratch = tempbegin(a);
	fid = fswalk(a, fs->root, name);
	if (fid == NULL) {
		tempend(scratch);
		return errd;
	}
	d = fsdirfstat(a, fid);
	fsclose(scratch.a, fid);
	tempend(scratch);
	return d;
}
