static u64
resolveport(String8 port, String8 proto)
{
	if (str8_is_integer(port, 10))
	{
		return u64_from_str8(port, 10);
	}
	char portbuf[1024] = {0};
	if (port.size >= sizeof portbuf)
	{
		return 0;
	}
	memcpy(portbuf, port.str, port.size);
	portbuf[port.size] = 0;
	char protobuf[16] = {0};
	if (proto.size > 0 && proto.size < sizeof protobuf)
	{
		memcpy(protobuf, proto.str, proto.size);
		protobuf[proto.size] = 0;
	}
	struct servent *sv = getservbyname(portbuf, protobuf);
	if (sv != NULL)
	{
		endservent();
		return ntohs((u16)sv->s_port);
	}
	return 0;
}

static Netaddr
netaddr(Arena *a, String8 addr, String8 defnet, String8 defsrv)
{
	Netaddr na = {0};
	if (addr.size == 0)
	{
		return na;
	}
	if (defnet.size == 0)
	{
		defnet = str8_lit("tcp");
	}
	u64 bangpos = str8_find_needle(addr, 0, str8_lit("!"), 0);
	u64 colonpos = str8_find_needle(addr, 0, str8_lit(":"), 0);
	if (bangpos >= addr.size)
	{
		if (os_file_path_exists(addr))
		{
			na.net = str8_lit("unix");
			na.host = str8_copy(a, addr);
			na.isunix = 1;
			return na;
		}
		if (colonpos < addr.size)
		{
			String8 host = str8_prefix(addr, colonpos);
			String8 service = str8_skip(addr, colonpos + 1);
			na.net = str8_copy(a, defnet);
			na.host = str8_copy(a, host);
			na.port = resolveport(service, na.net);
			na.isunix = 0;
			return na;
		}
		na.net = str8_copy(a, defnet);
		na.host = str8_copy(a, addr);
		na.isunix = 0;
		if (defsrv.size > 0)
		{
			na.port = resolveport(defsrv, na.net);
		}
		return na;
	}
	String8 net = str8_prefix(addr, bangpos);
	String8 service = str8_skip(addr, bangpos + 1);
	bangpos = str8_find_needle(service, 0, str8_lit("!"), 0);
	if (bangpos < service.size)
	{
		String8 host = str8_prefix(service, bangpos);
		String8 port = str8_skip(service, bangpos + 1);
		na.net = str8_copy(a, net);
		na.host = str8_copy(a, host);
		na.port = resolveport(port, na.net);
		na.isunix = str8_match(na.net, str8_lit("unix"), 0);
		return na;
	}
	if (str8_match(net, str8_lit("unix"), 0))
	{
		na.net = str8_lit("unix");
		na.host = str8_copy(a, service);
		na.isunix = 1;
		return na;
	}
	na.net = str8_copy(a, net);
	na.host = str8_copy(a, service);
	na.isunix = 0;
	if (defsrv.size > 0)
	{
		na.port = resolveport(defsrv, na.net);
	}
	return na;
}

static u64
socketdial(Netaddr na, Netaddr local)
{
	if (na.isunix)
	{
		if (local.net.size > 0)
		{
			return 0;
		}
		if (na.host.size == 0)
		{
			return 0;
		}
		char hostbuf[1024] = {0};
		if (na.host.size >= sizeof hostbuf)
		{
			return 0;
		}
		memcpy(hostbuf, na.host.str, na.host.size);
		hostbuf[na.host.size] = 0;
		int fd = open(hostbuf, O_RDWR);
		if (fd >= 0)
		{
			return (u64)fd;
		}
		fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
		{
			return 0;
		}
		struct sockaddr_un unaddr = {0};
		unaddr.sun_family = AF_UNIX;
		if (na.host.size >= sizeof unaddr.sun_path)
		{
			close(fd);
			return 0;
		}
		memcpy(unaddr.sun_path, na.host.str, na.host.size);
		if (connect(fd, (struct sockaddr *)&unaddr, sizeof unaddr) < 0)
		{
			close(fd);
			return 0;
		}
		return (u64)fd;
	}
	if (na.host.size > 0 && str8_match(na.host, str8_lit("*"), 0))
	{
		return 0;
	}
	if (na.port == 0)
	{
		return 0;
	}
	char hostbuf[1024] = {0};
	if (na.host.size == 0)
	{
		memcpy(hostbuf, "localhost", 9);
		hostbuf[9] = 0;
	}
	else
	{
		if (na.host.size >= sizeof hostbuf)
		{
			return 0;
		}
		memcpy(hostbuf, na.host.str, na.host.size);
		hostbuf[na.host.size] = 0;
	}
	int socktype = 0;
	if (str8_match(na.net, str8_lit("tcp"), 0))
	{
		socktype = SOCK_STREAM;
	}
	else if (str8_match(na.net, str8_lit("udp"), 0))
	{
		socktype = SOCK_DGRAM;
	}
	else
	{
		return 0;
	}
	struct addrinfo hints = {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = socktype;
	struct addrinfo *res = NULL;
	if (getaddrinfo(hostbuf, NULL, &hints, &res) != 0)
	{
		return 0;
	}
	switch (res->ai_family)
	{
		case AF_INET:
		{
			((struct sockaddr_in *)res->ai_addr)->sin_port = htons((u16)na.port);
		}
		break;
		case AF_INET6:
		{
			((struct sockaddr_in6 *)res->ai_addr)->sin6_port = htons((u16)na.port);
		}
		break;
		default:
		{
			freeaddrinfo(res);
			return 0;
		}
	}
	struct addrinfo *localres = NULL;
	if (local.net.size > 0)
	{
		if (local.isunix || local.port == 0)
		{
			freeaddrinfo(res);
			return 0;
		}
		char localhostbuf[1024] = {0};
		if (local.host.size == 0)
		{
			memcpy(localhostbuf, "localhost", 9);
			localhostbuf[9] = 0;
		}
		else
		{
			if (local.host.size >= sizeof localhostbuf)
			{
				freeaddrinfo(res);
				return 0;
			}
			memcpy(localhostbuf, local.host.str, local.host.size);
			localhostbuf[local.host.size] = 0;
		}
		if (getaddrinfo(localhostbuf, NULL, &hints, &localres) != 0)
		{
			freeaddrinfo(res);
			return 0;
		}
	}
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0)
	{
		freeaddrinfo(res);
		if (localres)
		{
			freeaddrinfo(localres);
		}
		return 0;
	}
	if (localres)
	{
		if (socktype == SOCK_STREAM)
		{
			int opt = 1;
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt);
		}
		if (bind(fd, localres->ai_addr, localres->ai_addrlen) < 0)
		{
			close(fd);
			freeaddrinfo(res);
			freeaddrinfo(localres);
			return 0;
		}
		freeaddrinfo(localres);
	}
	if (socktype == SOCK_STREAM)
	{
		int opt = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt);
	}
	if (socktype == SOCK_DGRAM)
	{
		int opt = 1;
		setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof opt);
	}
	if (connect(fd, res->ai_addr, res->ai_addrlen) < 0)
	{
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
	char portbuf[6] = {0};
	if (port.size == 0 || port.size >= sizeof portbuf)
	{
		return 0;
	}
	memcpy(portbuf, port.str, port.size);
	portbuf[port.size] = 0;
	struct addrinfo *res;
	if (getaddrinfo(NULL, portbuf, hints, &res) != 0)
	{
		return 0;
	}
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0)
	{
		freeaddrinfo(res);
		return 0;
	}
	int opt = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) < 0)
	{
		close(fd);
		freeaddrinfo(res);
		return 0;
	}
	if (bind(fd, res->ai_addr, res->ai_addrlen) < 0)
	{
		close(fd);
		freeaddrinfo(res);
		return 0;
	}
	freeaddrinfo(res);
	if (listen(fd, 1) < 0)
	{
		close(fd);
		return 0;
	}
	return (u64)fd;
}

static u64
socketaccept(u64 fd)
{
	int connfd = accept(fd, NULL, NULL);
	if (connfd < 0)
	{
		return 0;
	}
	int opt = 1;
	if (setsockopt(connfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt) < 0)
	{
		close(connfd);
		return 0;
	}
	return (u64)connfd;
}

static u64
socketconnect(String8 host, String8 port, struct addrinfo *hints)
{
	char portbuf[6] = {0};
	if (port.size == 0 || port.size >= sizeof portbuf)
	{
		return 0;
	}
	memcpy(portbuf, port.str, port.size);
	portbuf[port.size] = 0;
	if (host.size == 0)
	{
		host = str8_lit("localhost");
	}
	char hostbuf[1024] = {0};
	if (host.size >= sizeof hostbuf)
	{
		return 0;
	}
	memcpy(hostbuf, host.str, host.size);
	hostbuf[host.size] = 0;
	struct addrinfo *res;
	if (getaddrinfo(hostbuf, portbuf, hints, &res) != 0)
	{
		return 0;
	}
	int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0)
	{
		freeaddrinfo(res);
		return 0;
	}
	int opt = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof opt) < 0)
	{
		close(fd);
		freeaddrinfo(res);
		return 0;
	}
	if (connect(fd, res->ai_addr, res->ai_addrlen) < 0)
	{
		close(fd);
		freeaddrinfo(res);
		return 0;
	}
	freeaddrinfo(res);
	return (u64)fd;
}

static u64
socketread(u64 fd, void *buf, u64 size)
{
	if (fd == 0)
	{
		return 0;
	}
	u64 nread = 0;
	u64 nleft = size;
	u8 *p = buf;
	while (nleft > 0)
	{
		ssize_t n = read(fd, p + nread, nleft);
		if (n >= 0)
		{
			nread += n;
			nleft -= n;
		}
		else if (errno != EINTR)
		{
			break;
		}
	}
	return nread;
}

static String8
socketreadmsg(Arena *a, u64 fd)
{
	u64 size = 0;
	u64 nread = socketread(fd, &size, sizeof size);
	if (nread != sizeof size || size == 0)
	{
		return str8_zero();
	}
	String8 data = str8_zero();
	data.str = push_array(a, u8, size);
	nread = socketread(fd, data.str, size);
	if (nread != size)
	{
		data.str = NULL;
		return str8_zero();
	}
	data.size = size;
	return data;
}

static String8
socketreadhttp(Arena *a, u64 fd)
{
	u8 buf[4096];
	u64 buflen = 0;
	u64 hdrend = 0;
	while (buflen < sizeof buf)
	{
		ssize_t n = read(fd, buf + buflen, sizeof buf - buflen);
		if (n < 0)
		{
			if (errno == EINTR)
			{
				continue;
			}
			return str8_zero();
		}
		if (n == 0)
		{
			break;
		}
		buflen += n;
		hdrend = str8_find_needle(str8(buf, buflen), 0, str8_lit("\r\n\r\n"), 0);
		if (hdrend < buflen)
		{
			hdrend += 4;
			break;
		}
	}
	if (hdrend == 0)
	{
		return str8_zero();
	}
	String8 hdr = str8(buf, hdrend);
	u64 bodylen = 0;
	String8 cl = str8_lit("content-length: ");
	u64 clpos = str8_find_needle(hdr, 0, cl, StringMatchFlag_CaseInsensitive);
	if (clpos < hdr.size)
	{
		u64 start = clpos + cl.size;
		u64 end = str8_find_needle(hdr, start, str8_lit("\r\n"), 0);
		if (end < hdr.size)
		{
			String8 lenstr = str8_substr(hdr, rng1u64(start, end));
			try_u64_from_str8(lenstr, &bodylen);
		}
	}
	String8 data = {
	    .str = push_array_no_zero(a, u8, hdrend + bodylen),
	    .size = hdrend + bodylen,
	};
	memcpy(data.str, buf, hdrend);
	u64 bodyinbuf = buflen - hdrend;
	if (bodyinbuf > bodylen)
	{
		bodyinbuf = bodylen;
	}
	memcpy(data.str + hdrend, buf + hdrend, bodyinbuf);
	u64 remaining = bodylen - bodyinbuf;
	if (remaining > 0)
	{
		u64 nread = socketread(fd, data.str + hdrend + bodyinbuf, remaining);
		if (nread < remaining)
		{
			data.size = hdrend + bodyinbuf + nread;
		}
	}
	return data;
}

static b32
socketwrite(u64 fd, String8 data)
{
	if (fd == 0)
	{
		return 0;
	}
	u64 nwrite = 0;
	u64 nleft = data.size;
	u8 *p = data.str;
	while (nleft > 0)
	{
		ssize_t n = write(fd, p + nwrite, nleft);
		if (n >= 0)
		{
			nwrite += n;
			nleft -= n;
		}
		else if (errno != EINTR)
		{
			break;
		}
	}
	return nwrite == data.size;
}
