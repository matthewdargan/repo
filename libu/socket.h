#ifndef SOCKET_H
#define SOCKET_H

static u64 socketlisten(String8 port, struct addrinfo *hints);
static u64 socketaccept(u64 fd);
static u64 socketconnect(String8 host, String8 port, struct addrinfo *hints);
static u64 socketread(u64 fd, void *buf, u64 size);
static String8 socketreadmsg(Arena *a, u64 fd);
static String8 socketreadhttp(Arena *a, u64 fd);
static b32 socketwrite(u64 fd, String8 data);

#endif /* SOCKET_H */
