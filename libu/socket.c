static u64
resolveport(String8 port, String8 proto)
{
	char portbuf[1024], protobuf[16];
	struct servent *sv;

	if (str8isint(port, 10))
		return str8tou64(port, 10);
	if (port.len >= sizeof portbuf)
		return 0;
	memcpy(portbuf, port.str, port.len);
	portbuf[port.len] = 0;
	protobuf[0] = 0;
	if (proto.len > 0 && proto.len < sizeof protobuf) {
		memcpy(protobuf, proto.str, proto.len);
		protobuf[proto.len] = 0;
	}
	sv = getservbyname(portbuf, protobuf);
	if (sv != NULL) {
		endservent();
		return ntohs((u16)sv->s_port);
	}
	return 0;
}

static Netaddr
netaddr(Arena *a, String8 addr, String8 defnet, String8 defsrv)
{
	Netaddr na;
	u64 bangpos, colonpos;
	String8 net, host, service, port;

	memset(&na, 0, sizeof na);
	if (addr.len == 0)
		return na;
	if (defnet.len == 0)
		defnet = str8lit("tcp");
	bangpos = str8index(addr, 0, str8lit("!"), 0);
	colonpos = str8index(addr, 0, str8lit(":"), 0);
	if (bangpos >= addr.len) {
		if (fileexists(a, addr)) {
			na.net = str8lit("unix");
			na.host = pushstr8cpy(a, addr);
			na.isunix = 1;
			return na;
		}
		if (colonpos < addr.len) {
			host = str8prefix(addr, colonpos);
			service = str8skip(addr, colonpos + 1);
			na.net = pushstr8cpy(a, defnet);
			na.host = pushstr8cpy(a, host);
			na.port = resolveport(service, na.net);
			na.isunix = 0;
			return na;
		}
		na.net = pushstr8cpy(a, defnet);
		na.host = pushstr8cpy(a, addr);
		na.isunix = 0;
		if (defsrv.len > 0)
			na.port = resolveport(defsrv, na.net);
		return na;
	}
	net = str8prefix(addr, bangpos);
	service = str8skip(addr, bangpos + 1);
	bangpos = str8index(service, 0, str8lit("!"), 0);
	if (bangpos < service.len) {
		host = str8prefix(service, bangpos);
		port = str8skip(service, bangpos + 1);
		na.net = pushstr8cpy(a, net);
		na.host = pushstr8cpy(a, host);
		na.port = resolveport(port, na.net);
		na.isunix = str8cmp(na.net, str8lit("unix"), 0);
		return na;
	}
	if (str8cmp(net, str8lit("unix"), 0)) {
		na.net = str8lit("unix");
		na.host = pushstr8cpy(a, service);
		na.isunix = 1;
		return na;
	}
	na.net = pushstr8cpy(a, net);
	na.host = pushstr8cpy(a, service);
	na.isunix = 0;
	if (defsrv.len > 0)
		na.port = resolveport(defsrv, na.net);
	return na;
}

static u64
socketdial(Netaddr na, Netaddr local)
{
	char hostbuf[1024], localhostbuf[1024];
	struct addrinfo hints, *res, *localres;
	struct sockaddr_un unaddr;
	int fd, opt, socktype;

	if (na.isunix) {
		if (local.net.len > 0)
			return 0;
		if (na.host.len == 0)
			return 0;
		memcpy(hostbuf, na.host.str, na.host.len);
		hostbuf[na.host.len] = 0;
		fd = open(hostbuf, O_RDWR);
		if (fd >= 0)
			return (u64)fd;
		fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
			return 0;
		memset(&unaddr, 0, sizeof unaddr);
		unaddr.sun_family = AF_UNIX;
		memcpy(unaddr.sun_path, na.host.str, na.host.len);
		if (connect(fd, (struct sockaddr *)&unaddr, sizeof unaddr) < 0) {
			close(fd);
			return 0;
		}
		return (u64)fd;
	}
	if (na.host.len > 0 && str8cmp(na.host, str8lit("*"), 0))
		return 0;
	if (na.port == 0)
		return 0;
	if (na.host.len == 0) {
		memcpy(hostbuf, "localhost", 9);
		hostbuf[9] = 0;
	} else {
		memcpy(hostbuf, na.host.str, na.host.len);
		hostbuf[na.host.len] = 0;
	}
	if (str8cmp(na.net, str8lit("tcp"), 0))
		socktype = SOCK_STREAM;
	else if (str8cmp(na.net, str8lit("udp"), 0))
		socktype = SOCK_DGRAM;
	else
		return 0;
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = socktype;
	if (getaddrinfo(hostbuf, NULL, &hints, &res) != 0)
		return 0;
	switch (res->ai_family) {
		case AF_INET:
			((struct sockaddr_in *)res->ai_addr)->sin_port = htons((u16)na.port);
			break;
		case AF_INET6:
			((struct sockaddr_in6 *)res->ai_addr)->sin6_port = htons((u16)na.port);
			break;
		default:
			freeaddrinfo(res);
			return 0;
	}
	if (local.net.len > 0) {
		if (local.isunix || local.port == 0) {
			freeaddrinfo(res);
			return 0;
		}
		if (local.host.len == 0) {
			memcpy(localhostbuf, "localhost", 9);
			localhostbuf[9] = 0;
		} else {
			memcpy(localhostbuf, local.host.str, local.host.len);
			localhostbuf[local.host.len] = 0;
		}
		if (getaddrinfo(localhostbuf, NULL, &hints, &localres) != 0) {
			freeaddrinfo(res);
			return 0;
		}
	}
	fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0) {
		freeaddrinfo(res);
		if (localres)
			freeaddrinfo(localres);
		return 0;
	}
	if (localres) {
		if (socktype == SOCK_STREAM) {
			opt = 1;
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
		}
		if (bind(fd, localres->ai_addr, localres->ai_addrlen) < 0) {
			close(fd);
			freeaddrinfo(res);
			freeaddrinfo(localres);
			return 0;
		}
		freeaddrinfo(localres);
	}
	if (socktype == SOCK_STREAM) {
		opt = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt);
	}
	if (socktype == SOCK_DGRAM) {
		opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof opt);
	}
	if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
		close(fd);
		freeaddrinfo(res);
		return 0;
	}
	freeaddrinfo(res);
	return (u64)fd;
}

static u64
socketlisten(String8 port, struct addrinfo *hints)
{
	char portbuf[6];
	struct addrinfo *res;
	int fd, opt;

	if (port.len == 0 || port.len >= sizeof portbuf)
		return 0;
	memcpy(portbuf, port.str, port.len);
	portbuf[port.len] = 0;
	if (getaddrinfo(NULL, portbuf, hints, &res) != 0)
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
socketconnect(String8 host, String8 port, struct addrinfo *hints)
{
	char portbuf[6], hostbuf[1024];
	struct addrinfo *res;
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
	if (getaddrinfo(hostbuf, portbuf, hints, &res) != 0)
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
