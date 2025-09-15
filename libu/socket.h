#ifndef SOCKET_H
#define SOCKET_H

typedef struct Netaddr Netaddr;
struct Netaddr {
	String8 net;
	String8 host;
	String8 port;
	b32 isunix;
};

static Netaddr netaddr(Arena *a, String8 addr, String8 defnet, String8 defsrv);
static b32 netaddrparse(Arena *a, String8 addr, Netaddr *parsed);
static u64 socketdial(Netaddr na, Netaddr local);
static u64 socketlisten(String8 port, struct addrinfo *hints);
static u64 socketaccept(u64 fd);
static u64 socketconnect(String8 host, String8 port, struct addrinfo *hints);
static u64 socketread(u64 fd, void *buf, u64 size);
static String8 socketreadmsg(Arena *a, u64 fd);
static String8 socketreadhttp(Arena *a, u64 fd);
static b32 socketwrite(u64 fd, String8 data);

#endif /* SOCKET_H */
