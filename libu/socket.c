static u64
socketlisten(String8 port)
{
	char portbuf[6];
	struct addrinfo hints, *res;
	int fd, opt;

	if (port.len == 0 || port.len >= sizeof portbuf)
		return 0;
	memcpy(portbuf, port.str, port.len);
	portbuf[port.len] = 0;
	memset(&hints, 0, sizeof hints);
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(NULL, portbuf, &hints, &res) < 0)
		return 0;
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		freeaddrinfo(res);
		return 0;
	}
	opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) < 0) {
		closefd(fd);
		freeaddrinfo(res);
		return 0;
	}
	if (bind(fd, res->ai_addr, res->ai_addrlen) < 0) {
		closefd(fd);
		freeaddrinfo(res);
		return 0;
	}
	freeaddrinfo(res);
	if (listen(fd, 1) < 0) {
		closefd(fd);
		return 0;
	}
	return (u64)fd;
}

static u64
socketaccept(u64 fd)
{
	int connfd, opt;

	connfd = accept(fd, NULL, NULL);
	if (connfd < 0)
		return 0;
	opt = 1;
	if (setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt) < 0) {
		closefd(connfd);
		return 0;
	}
	return (u64)connfd;
}

static u64
socketconnect(String8 host, String8 port)
{
	char portbuf[6], hostbuf[1024];
	struct addrinfo hints, *res;
	int fd, opt;

	if (port.len == 0 || port.len >= sizeof portbuf)
		return 0;
	memcpy(portbuf, port.str, port.len);
	portbuf[port.len] = 0;
	if (host.len == 0)
		host = str8lit("localhost");
	if (host.len >= sizeof hostbuf)
		return 0;
	memcpy(hostbuf, host.str, host.len);
	hostbuf[host.len] = 0;
	memset(&hints, 0, sizeof hints);
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(hostbuf, portbuf, &hints, &res) < 0)
		return 0;
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		freeaddrinfo(res);
		return 0;
	}
	opt = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt) < 0) {
		closefd(fd);
		freeaddrinfo(res);
		return 0;
	}
	if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
		closefd(fd);
		freeaddrinfo(res);
		return 0;
	}
	freeaddrinfo(res);
	return (u64)fd;
}

static u64
socketread(u64 fd, void *buf, u64 size)
{
	u64 nread, nleft;
	u8 *p;
	ssize_t n;

	if (fd == 0)
		return 0;
	nread = 0;
	nleft = size;
	p = buf;
	while (nleft > 0) {
		n = read(fd, p + nread, nleft);
		if (n >= 0) {
			nread += n;
			nleft -= n;
		} else if (errno != EINTR)
			break;
	}
	return nread;
}

static String8
socketreadmsg(Arena *a, u64 fd)
{
	u64 nread, size;
	String8 data;

	nread = socketread(fd, &size, sizeof size);
	if (nread != sizeof size || size == 0)
		return str8zero();
	data.str = pusharr(a, u8, size);
	nread = socketread(fd, data.str, size);
	if (nread != size) {
		data.str = NULL;
		return str8zero();
	}
	data.len = size;
	return data;
}

static String8
socketreadhttp(Arena *a, u64 fd)
{
	u64 buflen, hdrend, bodylen, clpos, start, end, bodyinbuf, remaining, nread;
	ssize_t n;
	u8 buf[4096];
	String8 hdr, cl, data, lenstr;

	buflen = 0;
	hdrend = 0;
	while (buflen < sizeof buf) {
		n = read(fd, buf + buflen, sizeof buf - buflen);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return str8zero();
		}
		if (n == 0)
			break;
		buflen += n;
		hdrend = str8index(str8(buf, buflen), 0, str8lit("\r\n\r\n"), 0);
		if (hdrend < buflen) {
			hdrend += 4;
			break;
		}
	}
	if (hdrend == 0)
		return str8zero();
	hdr = str8(buf, hdrend);
	bodylen = 0;
	cl = str8lit("content-length: ");
	clpos = str8index(hdr, 0, cl, CASEINSENSITIVE);
	if (clpos < hdr.len) {
		start = clpos + cl.len;
		end = str8index(hdr, start, str8lit("\r\n"), 0);
		if (end < hdr.len) {
			lenstr = str8substr(hdr, rng1u64(start, end));
			str8tou64ok(lenstr, &bodylen);
		}
	}
	data.len = hdrend + bodylen;
	data.str = pusharrnoz(a, u8, data.len);
	memcpy(data.str, buf, hdrend);
	bodyinbuf = buflen - hdrend;
	if (bodyinbuf > bodylen)
		bodyinbuf = bodylen;
	memcpy(data.str + hdrend, buf + hdrend, bodyinbuf);
	remaining = bodylen - bodyinbuf;
	if (remaining > 0) {
		nread = socketread(fd, data.str + hdrend + bodyinbuf, remaining);
		if (nread < remaining)
			data.len = hdrend + bodyinbuf + nread;
	}
	return data;
}

static b32
socketwrite(u64 fd, String8 data)
{
	u64 nwrite, nleft;
	u8 *p;
	ssize_t n;

	if (fd == 0)
		return 0;
	nwrite = 0;
	nleft = data.len;
	p = data.str;
	while (nleft > 0) {
		n = write(fd, p + nwrite, nleft);
		if (n >= 0) {
			nwrite += n;
			nleft -= n;
		} else if (errno != EINTR)
			break;
	}
	return nwrite == data.len;
}
