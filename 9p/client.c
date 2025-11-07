static Cfsys *
fsinit(Arena *a, u64 fd)
{
	Cfsys *fs = push_array(a, Cfsys, 1);
	fs->fd = fd;
	fs->nexttag = 1;
	fs->nextfid = 1;
	if(!fsversion(a, fs, 8192))
	{
		fs9unmount(a, fs);
		return 0;
	}
	return fs;
}

static String8
getuser()
{
	uid_t uid = getuid();
	struct passwd *pw = getpwuid(uid);
	if(pw == 0)
	{
		return str8_lit("none");
	}
	return str8_cstring(pw->pw_name);
}

static Cfsys *
fs9mount(Arena *a, u64 fd, String8 aname)
{
	Cfsys *fs = fsinit(a, fd);
	if(fs == 0)
	{
		return 0;
	}
	String8 user = getuser();
	Cfid *fid = fsattach(a, fs, FID_NONE, user, aname);
	if(fid == 0)
	{
		fs9unmount(a, fs);
		return 0;
	}
	fs->root = fid;
	return fs;
}

static void
fs9unmount(Arena *a, Cfsys *fs)
{
	fsclose(a, fs->root);
	fs->root = 0;
	close(fs->fd);
	fs->fd = -1;
}

static void
debug9pprint(Arena *a, String8 dir, Message9P fc)
{
	if(!debug9pclient)
	{
		return;
	}
	switch(fc.type)
	{
		case Msg9P_Tversion:
		{
			log_infof("%S Msg9P_Tversion tag=%u msize=%u version='%.*s'\n", dir, fc.tag, fc.max_message_size,
			          str8_varg(fc.protocol_version));
		}
		break;
		case Msg9P_Rversion:
		{
			log_infof("%S Msg9P_Rversion tag=%u msize=%u version='%.*s'\n", dir, fc.tag, fc.max_message_size,
			          str8_varg(fc.protocol_version));
		}
		break;
		case Msg9P_Tauth:
		{
			log_infof("%S Msg9P_Tauth tag=%u afid=%u uname='%.*s' aname='%.*s'\n", dir, fc.tag, fc.auth_fid,
			          str8_varg(fc.user_name), str8_varg(fc.attach_path));
		}
		break;
		case Msg9P_Rauth:
		{
			log_infof("%S Msg9P_Rauth tag=%u qid=(type=%u vers=%u path=%llu)\n", dir, fc.tag, fc.auth_qid.type,
			          fc.auth_qid.version, fc.auth_qid.path);
		}
		break;
		case Msg9P_Rerror:
		{
			log_infof("%S Msg9P_Rerror tag=%u ename='%.*s'\n", dir, fc.tag, str8_varg(fc.error_message));
		}
		break;
		case Msg9P_Tattach:
		{
			log_infof("%S Msg9P_Tattach tag=%u fid=%u afid=%u uname='%.*s' aname='%.*s'\n", dir, fc.tag, fc.fid, fc.auth_fid,
			          str8_varg(fc.user_name), str8_varg(fc.attach_path));
		}
		break;
		case Msg9P_Rattach:
		{
			log_infof("%S Msg9P_Rattach tag=%u qid=(type=%u vers=%u path=%llu)\n", dir, fc.tag, fc.qid.type, fc.qid.version,
			          fc.qid.path);
		}
		break;
		case Msg9P_Twalk:
		{
			String8 msg = str8f(a, "%S Msg9P_Twalk tag=%u fid=%u newfid=%u nwname=%u", dir, fc.tag, fc.fid, fc.new_fid,
			                    fc.walk_name_count);
			for(u64 i = 0; i < fc.walk_name_count; i += 1)
			{
				msg = str8f(a, "%S '%.*s'", msg, str8_varg(fc.walk_names[i]));
			}
			log_infof("%S\n", msg);
		}
		break;
		case Msg9P_Rwalk:
		{
			String8 msg = str8f(a, "%S Msg9P_Rwalk tag=%u nwqid=%u", dir, fc.tag, fc.walk_qid_count);
			for(u64 i = 0; i < fc.walk_qid_count; i += 1)
			{
				msg = str8f(a, "%S qid%u=(type=%u vers=%u path=%llu)", msg, i, fc.walk_qids[i].type, fc.walk_qids[i].version,
				            fc.walk_qids[i].path);
			}
			log_infof("%S\n", msg);
		}
		break;
		case Msg9P_Topen:
		{
			log_infof("%S Msg9P_Topen tag=%u fid=%u mode=%u\n", dir, fc.tag, fc.fid, fc.open_mode);
		}
		break;
		case Msg9P_Ropen:
		{
			log_infof("%S Msg9P_Ropen tag=%u qid=(type=%u vers=%u path=%llu) iounit=%u\n", dir, fc.tag, fc.qid.type,
			          fc.qid.version, fc.qid.path, fc.io_unit_size);
		}
		break;
		case Msg9P_Tcreate:
		{
			log_infof("%S Msg9P_Tcreate tag=%u fid=%u name='%.*s' perm=%u mode=%u\n", dir, fc.tag, fc.fid, str8_varg(fc.name),
			          fc.permissions, fc.open_mode);
		}
		break;
		case Msg9P_Rcreate:
		{
			log_infof("%S Msg9P_Rcreate tag=%u qid=(type=%u vers=%u path=%llu) iounit=%u\n", dir, fc.tag, fc.qid.type,
			          fc.qid.version, fc.qid.path, fc.io_unit_size);
		}
		break;
		case Msg9P_Tread:
		{
			log_infof("%S Msg9P_Tread tag=%u fid=%u offset=%llu count=%u\n", dir, fc.tag, fc.fid, fc.file_offset,
			          fc.byte_count);
		}
		break;
		case Msg9P_Rread:
		{
			log_infof("%S Msg9P_Rread tag=%u count=%llu\n", dir, fc.tag, fc.payload_data.size);
		}
		break;
		case Msg9P_Twrite:
		{
			log_infof("%S Msg9P_Twrite tag=%u fid=%u offset=%llu count=%llu\n", dir, fc.tag, fc.fid, fc.file_offset,
			          fc.payload_data.size);
		}
		break;
		case Msg9P_Rwrite:
		{
			log_infof("%S Msg9P_Rwrite tag=%u count=%u\n", dir, fc.tag, fc.byte_count);
		}
		break;
		case Msg9P_Tclunk:
		{
			log_infof("%S Msg9P_Tclunk tag=%u fid=%u\n", dir, fc.tag, fc.fid);
		}
		break;
		case Msg9P_Rclunk:
		{
			log_infof("%S Msg9P_Rclunk tag=%u\n", dir, fc.tag);
		}
		break;
		case Msg9P_Tremove:
		{
			log_infof("%S Msg9P_Tremove tag=%u fid=%u\n", dir, fc.tag, fc.fid);
		}
		break;
		case Msg9P_Rremove:
		{
			log_infof("%S Msg9P_Rremove tag=%u\n", dir, fc.tag);
		}
		break;
		case Msg9P_Tstat:
		{
			log_infof("%S Msg9P_Tstat tag=%u fid=%u\n", dir, fc.tag, fc.fid);
		}
		break;
		case Msg9P_Rstat:
		{
			log_infof("%S Msg9P_Rstat tag=%u stat.size=%llu\n", dir, fc.tag, fc.stat_data.size);
		}
		break;
		case Msg9P_Twstat:
		{
			log_infof("%S Msg9P_Twstat tag=%u fid=%u stat.size=%llu\n", dir, fc.tag, fc.fid, fc.stat_data.size);
		}
		break;
		case Msg9P_Rwstat:
		{
			log_infof("%S Msg9P_Rwstat tag=%u\n", dir, fc.tag);
		}
		break;
		default:
		{
			log_infof("%S unknown type=%u tag=%u\n", dir, fc.type, fc.tag);
		}
		break;
	}
}

static Message9P
fsrpc(Arena *a, Cfsys *fs, Message9P tx)
{
	Message9P errfc = {0};
	if(tx.type != Msg9P_Tversion)
	{
		tx.tag = fs->nexttag;
		fs->nexttag += 1;
		if(fs->nexttag == TAG_NONE)
		{
			fs->nexttag = 1;
		}
	}
	debug9pprint(a, str8_lit("<-"), tx);
	String8 txmsg = str8_from_msg9p(a, tx);
	if(txmsg.size == 0)
	{
		return errfc;
	}
	ssize_t n = write(fs->fd, txmsg.str, txmsg.size);
	if(n < 0 || (u64)n != txmsg.size)
	{
		return errfc;
	}
	String8 rxmsg = read_9p_msg(a, fs->fd);
	if(rxmsg.size == 0)
	{
		return errfc;
	}
	Message9P rx = msg9p_from_str8(rxmsg);
	debug9pprint(a, str8_lit("->"), rx);
	if(rx.type == 0 || rx.type == Msg9P_Rerror || rx.type != tx.type + 1)
	{
		return errfc;
	}
	if(rx.tag != tx.tag)
	{
		return errfc;
	}
	return rx;
}

static b32
fsversion(Arena *a, Cfsys *fs, u32 msize)
{
	Message9P tx = {0};
	tx.type = Msg9P_Tversion;
	tx.tag = TAG_NONE;
	tx.max_message_size = msize;
	tx.protocol_version = version_9p;
	Message9P rx = fsrpc(a, fs, tx);
	if(rx.type != Msg9P_Rversion)
	{
		return 0;
	}
	fs->msize = rx.max_message_size;
	if(!str8_match(rx.protocol_version, version_9p, 0))
	{
		return 0;
	}
	return 1;
}

static Cfid *
fsauth(Arena *a, Cfsys *fs, String8 uname, String8 aname)
{
	Cfid *afid = push_array(a, Cfid, 1);
	afid->fid = fs->nextfid;
	fs->nextfid += 1;
	afid->fs = fs;
	Message9P tx = {0};
	tx.type = Msg9P_Tauth;
	tx.auth_fid = afid->fid;
	tx.user_name = uname;
	tx.attach_path = aname;
	Message9P rx = fsrpc(a, fs, tx);
	if(rx.type != Msg9P_Rauth)
	{
		return 0;
	}
	afid->qid = rx.auth_qid;
	return afid;
}

static Cfid *
fsattach(Arena *a, Cfsys *fs, u32 afid, String8 uname, String8 aname)
{
	Cfid *fid = push_array(a, Cfid, 1);
	fid->fid = fs->nextfid;
	fs->nextfid += 1;
	fid->fs = fs;
	Message9P tx = {0};
	tx.type = Msg9P_Tattach;
	tx.fid = fid->fid;
	tx.auth_fid = afid;
	tx.user_name = uname;
	tx.attach_path = aname;
	Message9P rx = fsrpc(a, fs, tx);
	if(rx.type != Msg9P_Rattach)
	{
		return 0;
	}
	fid->qid = rx.qid;
	return fid;
}

static void
fsclose(Arena *a, Cfid *fid)
{
	if(fid == 0)
	{
		return;
	}
	Message9P tx = {0};
	tx.type = Msg9P_Tclunk;
	tx.fid = fid->fid;
	fsrpc(a, fid->fs, tx);
}

static Cfid *
fswalk(Arena *a, Cfid *fid, String8 path)
{
	if(fid == 0)
	{
		return 0;
	}
	Cfid *wfid = push_array(a, Cfid, 1);
	Temp scratch = temp_begin(a);
	wfid->fid = fid->fs->nextfid;
	fid->fs->nextfid += 1;
	wfid->qid = fid->qid;
	wfid->fs = fid->fs;
	b32 firstwalk = 1;
	String8List parts = str8_split(scratch.arena, path, (u8 *)"/", 1, 0);
	String8Node *node = parts.first;
	Message9P tx = {0};
	tx.type = Msg9P_Twalk;
	tx.fid = fid->fid;
	tx.new_fid = wfid->fid;
	if(node == 0)
	{
		Message9P rx = fsrpc(a, fid->fs, tx);
		if(rx.type != Msg9P_Rwalk || rx.walk_qid_count != tx.walk_name_count)
		{
			return 0;
		}
		return wfid;
	}
	for(; node != 0;)
	{
		tx.fid = firstwalk ? fid->fid : wfid->fid;
		u64 i = 0;
		for(; node != 0 && i < MAX_WALK_ELEM_COUNT;)
		{
			String8 part = node->string;
			if(str8_match(part, str8_lit("."), 0))
			{
				node = node->next;
				continue;
			}
			tx.walk_names[i] = part;
			i += 1;
			node = node->next;
		}
		tx.walk_name_count = i;
		Message9P rx = fsrpc(a, fid->fs, tx);
		if(rx.type != Msg9P_Rwalk || rx.walk_qid_count != tx.walk_name_count)
		{
			if(!firstwalk)
			{
				fsclose(a, wfid);
			}
			return 0;
		}
		if(rx.walk_qid_count > 0)
		{
			wfid->qid = rx.walk_qids[rx.walk_qid_count - 1];
		}
		firstwalk = 0;
	}
	return wfid;
}

static b32
fsfcreate(Arena *a, Cfid *fid, String8 name, u32 mode, u32 perm)
{
	if(fid == 0)
	{
		return 0;
	}
	Message9P tx = {0};
	tx.type = Msg9P_Tcreate;
	tx.fid = fid->fid;
	tx.name = name;
	tx.permissions = perm;
	tx.open_mode = mode;
	Message9P rx = fsrpc(a, fid->fs, tx);
	if(rx.type != Msg9P_Rcreate)
	{
		return 0;
	}
	fid->mode = mode;
	return 1;
}

static Cfid *
fscreate(Arena *a, Cfsys *fs, String8 name, u32 mode, u32 perm)
{
	if(fs == 0 || fs->root == 0)
	{
		return 0;
	}
	String8 dir = str8_chop_last_slash(name);
	String8 elem = str8_skip_last_slash(name);
	Cfid *fid = fswalk(a, fs->root, dir);
	if(fid == 0)
	{
		return 0;
	}
	if(!fsfcreate(a, fid, elem, mode, perm))
	{
		fsclose(a, fid);
		return 0;
	}
	return fid;
}

static b32
fsfremove(Arena *a, Cfid *fid)
{
	if(fid == 0)
	{
		return 0;
	}
	Message9P tx = {0};
	tx.type = Msg9P_Tremove;
	tx.fid = fid->fid;
	Message9P rx = fsrpc(a, fid->fs, tx);
	if(rx.type != Msg9P_Rremove)
	{
		return 0;
	}
	return 1;
}

static b32
fsremove(Arena *a, Cfsys *fs, String8 name)
{
	if(fs == 0 || fs->root == 0)
	{
		return 0;
	}
	Cfid *fid = fswalk(a, fs->root, name);
	if(fid == 0)
	{
		return 0;
	}
	if(!fsfremove(a, fid))
	{
		return 0;
	}
	return 1;
}

static b32
fsfopen(Arena *a, Cfid *fid, u32 mode)
{
	if(fid == 0)
	{
		return 0;
	}
	Message9P tx = {0};
	tx.type = Msg9P_Topen;
	tx.fid = fid->fid;
	tx.open_mode = mode;
	Message9P rx = fsrpc(a, fid->fs, tx);
	if(rx.type != Msg9P_Ropen)
	{
		return 0;
	}
	fid->mode = mode;
	return 1;
}

static Cfid *
fs9open(Arena *a, Cfsys *fs, String8 name, u32 mode)
{
	if(fs == 0 || fs->root == 0)
	{
		return 0;
	}
	Cfid *fid = fswalk(a, fs->root, name);
	if(fid == 0)
	{
		return 0;
	}
	if(!fsfopen(a, fid, mode))
	{
		fsclose(a, fid);
		return 0;
	}
	return fid;
}

static s64
fspread(Arena *a, Cfid *fid, void *buf, u64 n, s64 offset)
{
	if(fid == 0 || buf == 0)
	{
		return -1;
	}
	u32 msize = fid->fs->msize - MESSAGE_HEADER_SIZE;
	if(n > msize)
	{
		n = msize;
	}
	Message9P tx = {0};
	tx.type = Msg9P_Tread;
	tx.fid = fid->fid;
	tx.file_offset = (offset == -1) ? fid->offset : offset;
	tx.byte_count = n;
	Message9P rx = fsrpc(a, fid->fs, tx);
	if(rx.type != Msg9P_Rread)
	{
		return -1;
	}
	s64 nr = rx.payload_data.size;
	if(nr > (s64)n)
	{
		nr = n;
	}
	if(nr > 0)
	{
		MemoryCopy(buf, rx.payload_data.str, nr);
		if(offset == -1)
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
	u64 total_bytes_read = 0;
	u64 total_bytes_left_to_read = n;
	u8 *p = buf;
	for(; total_bytes_left_to_read > 0;)
	{
		s64 nr = fsread(a, fid, p + total_bytes_read, total_bytes_left_to_read);
		if(nr <= 0)
		{
			if(total_bytes_read == 0)
			{
				return nr;
			}
			break;
		}
		total_bytes_read += nr;
		total_bytes_left_to_read -= nr;
	}
	return total_bytes_read;
}

static s64
fspwrite(Arena *a, Cfid *fid, void *buf, u64 n, s64 offset)
{
	if(fid == 0 || buf == 0)
	{
		return -1;
	}
	u32 msize = fid->fs->msize - MESSAGE_HEADER_SIZE;
	u64 total_bytes_written = 0;
	u64 total_bytes_left_to_write = n;
	u8 *p = buf;
	for(; total_bytes_left_to_write > 0;)
	{
		u64 want = total_bytes_left_to_write;
		if(want > msize)
		{
			want = msize;
		}
		Message9P tx = {0};
		tx.type = Msg9P_Twrite;
		tx.fid = fid->fid;
		tx.file_offset = (offset == -1) ? fid->offset : offset + total_bytes_written;
		tx.payload_data.size = want;
		tx.payload_data.str = p + total_bytes_written;
		Message9P rx = fsrpc(a, fid->fs, tx);
		if(rx.type != Msg9P_Rwrite)
		{
			if(total_bytes_written == 0)
			{
				return -1;
			}
			break;
		}
		u32 got = rx.byte_count;
		if(got == 0)
		{
			if(total_bytes_written == 0)
			{
				return -1;
			}
			break;
		}
		total_bytes_written += got;
		total_bytes_left_to_write -= got;
		if(offset == -1)
		{
			fid->offset += got;
		}
		if(got < want)
		{
			break;
		}
	}
	return total_bytes_written;
}

static s64
fswrite(Arena *a, Cfid *fid, void *buf, u64 n)
{
	return fspwrite(a, fid, buf, n, -1);
}

static s64
dirpackage(Arena *a, u8 *buf, s64 ts, DirList *list)
{
	*list = (DirList){0};
	s64 n = 0;
	u64 i = 0;
	for(; i < (u64)ts;)
	{
		if(i + 2 > (u64)ts)
		{
			return -1;
		}
		u64 m = 2 + from_le_u16(read_u16(&buf[i]));
		if(i + m > (u64)ts)
		{
			return -1;
		}
		String8 dirmsg = {.str = &buf[i], .size = m};
		Dir d = dir_from_str8(dirmsg);
		if(d.name.size == 0 && m > 2)
		{
			return -1;
		}
		dir_list_push(a, list, d);
		n += 1;
		i += m;
	}
	return n;
}

static s64
fsdirread(Arena *a, Cfid *fid, DirList *list)
{
	if(fid == 0 || list == 0)
	{
		return -1;
	}
	Temp scratch = temp_begin(a);
	u8 *buf = push_array_no_zero(a, u8, DIR_ENTRY_MAX);
	s64 ts = fsread(a, fid, buf, DIR_ENTRY_MAX);
	if(ts >= 0)
	{
		ts = dirpackage(a, buf, ts, list);
	}
	temp_end(scratch);
	return ts;
}

static s64
fsdirreadall(Arena *a, Cfid *fid, DirList *list)
{
	if(fid == 0 || list == 0)
	{
		return -1;
	}
	u8 *buf = push_array_no_zero(a, u8, DIR_BUFFER_MAX);
	s64 total_bytes_read = 0;
	u64 buffer_space_left = DIR_BUFFER_MAX;
	s64 n = 0;
	for(; buffer_space_left >= DIR_ENTRY_MAX;)
	{
		n = fsread(a, fid, buf + total_bytes_read, DIR_ENTRY_MAX);
		if(n <= 0)
		{
			break;
		}
		total_bytes_read += n;
		buffer_space_left -= n;
	}
	if(total_bytes_read >= 0)
	{
		total_bytes_read = dirpackage(a, buf, total_bytes_read, list);
	}
	if(total_bytes_read == 0 && n < 0)
	{
		return -1;
	}
	return total_bytes_read;
}

static Dir
fsdirfstat(Arena *a, Cfid *fid)
{
	Dir errd = {0};
	if(fid == 0)
	{
		return errd;
	}
	Message9P tx = {0};
	tx.type = Msg9P_Tstat;
	tx.fid = fid->fid;
	Message9P rx = fsrpc(a, fid->fs, tx);
	if(rx.type != Msg9P_Rstat)
	{
		return errd;
	}
	Dir d = dir_from_str8(rx.stat_data);
	return d;
}

static Dir
fsdirstat(Arena *a, Cfsys *fs, String8 name)
{
	Dir errd = {0};
	if(fs == 0 || fs->root == 0)
	{
		return errd;
	}
	Cfid *fid = fswalk(a, fs->root, name);
	if(fid == 0)
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
	if(fid == 0)
	{
		return 0;
	}
	Temp scratch = temp_begin(a);
	String8 stat = str8_from_dir(scratch.arena, d);
	if(stat.size == 0)
	{
		return 0;
	}
	Message9P tx = {0};
	tx.type = Msg9P_Twstat;
	tx.fid = fid->fid;
	tx.stat_data = stat;
	Message9P rx = fsrpc(a, fid->fs, tx);
	if(rx.type != Msg9P_Rwstat)
	{
		return 0;
	}
	return 1;
}

static b32
fsdirwstat(Arena *a, Cfsys *fs, String8 name, Dir d)
{
	if(fs == 0 || fs->root == 0)
	{
		return 0;
	}
	Cfid *fid = fswalk(a, fs->root, name);
	if(fid == 0)
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
	if(fs == 0 || fs->root == 0)
	{
		return 0;
	}
	if(mode == AccessFlag_Exist)
	{
		Dir d = fsdirstat(a, fs, name);
		if(d.name.size == 0)
		{
			return 0;
		}
		return 1;
	}
	Cfid *fid = fs9open(a, fs, name, omodetab[mode & 7]);
	if(fid == 0)
	{
		return 0;
	}
	fsclose(a, fid);
	return 1;
}

static s64
fsseek(Arena *a, Cfid *fid, s64 offset, u32 type)
{
	if(fid == 0)
	{
		return -1;
	}
	s64 pos = 0;
	switch(type)
	{
		case SeekWhence_Set:
		{
			pos = offset;
			fid->offset = offset;
		}
		break;
		case SeekWhence_Cur:
		{
			pos = (s64)fid->offset + offset;
			if(pos < 0)
			{
				return -1;
			}
			fid->offset = pos;
		}
		break;
		case SeekWhence_End:
		{
			Dir d = fsdirfstat(a, fid);
			if(d.name.size == 0)
			{
				return -1;
			}
			pos = (s64)d.length + offset;
			if(pos < 0)
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
