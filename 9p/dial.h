#ifndef _9P_DIAL_H
#define _9P_DIAL_H

// Dial Protocol Types
typedef u32 Dial9PProtocol;
enum
{
	Dial9PProtocol_TCP,
	Dial9PProtocol_Unix,
};

// Dial Address
typedef struct Dial9PAddress Dial9PAddress;
struct Dial9PAddress
{
	Dial9PProtocol protocol;
	String8 host;
	u16 port;
};

// Dial String Parsing
static u16 dial9p_resolve_port(String8 port, String8 protocol);
static Dial9PAddress dial9p_parse(Arena *arena, String8 dial_string, String8 default_protocol, String8 default_port);

// Dial Operations
static OS_Handle dial9p_connect(Arena *scratch, String8 dial_string, String8 default_protocol, String8 default_port);
static OS_Handle dial9p_listen(String8 dial_string, String8 default_protocol, String8 default_port);

#endif // _9P_DIAL_H
