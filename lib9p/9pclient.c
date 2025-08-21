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
