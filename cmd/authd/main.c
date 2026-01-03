// clang-format off
#include "base/inc.h"
#include "http/inc.h"
#include "base/inc.c"
#include "http/inc.c"
// clang-format on

////////////////////////////////
//~ Globals & Configuration

typedef struct Session Session;
typedef struct SessionTable SessionTable;

struct Session
{
	String8 session_id;
	u64 created_us;
	u64 expiry_us;
};

struct SessionTable
{
	Mutex mutex;
	Arena *arena;
	Session *sessions;
	u64 count;
	u64 capacity;
};

global String8 auth_username;
global String8 auth_password;
global u64 auth_session_duration_us;
global SessionTable *sessions;

////////////////////////////////
//~ Session & Authentication

internal SessionTable *
session_table_alloc(Arena *arena, u64 capacity)
{
	SessionTable *table = push_array(arena, SessionTable, 1);
	table->mutex = mutex_alloc();
	table->arena = arena_alloc();
	table->sessions = push_array(table->arena, Session, capacity);
	table->capacity = capacity;
	return table;
}

internal String8
session_generate_id(Arena *arena)
{
	u8 random_bytes[32];
	if(getentropy(random_bytes, sizeof(random_bytes)) != 0)
	{
		MemoryZero(random_bytes, sizeof(random_bytes));
	}

	read_only local_persist u8 hex_table[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
	                                            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
	u8 *buffer = push_array(arena, u8, 64);
	for(u64 i = 0; i < 32; i += 1)
	{
		u8 byte = random_bytes[i];
		buffer[i * 2] = hex_table[byte >> 4];
		buffer[i * 2 + 1] = hex_table[byte & 0xF];
	}

	String8 session_id = {buffer, 64};
	return session_id;
}

internal String8
session_create(SessionTable *table, u64 duration_us)
{
	String8 session_id = str8_zero();
	u64 now = os_now_microseconds();

	MutexScope(table->mutex)
	{
		for(u64 i = 0; i < table->capacity; i += 1)
		{
			Session *s = &table->sessions[i];
			if(s->session_id.size > 0 && s->expiry_us <= now)
			{
				MemoryZeroStruct(s);
				table->count -= 1;
			}
		}

		Session *slot = 0;
		for(u64 i = 0; i < table->capacity; i += 1)
		{
			if(table->sessions[i].session_id.size == 0)
			{
				slot = &table->sessions[i];
				break;
			}
		}

		if(slot == 0)
		{
			return str8_zero();
		}

		session_id = session_generate_id(table->arena);
		slot->session_id = session_id;
		slot->created_us = now;
		slot->expiry_us = now + duration_us;
		table->count += 1;
	}

	return session_id;
}

internal b32
session_validate(SessionTable *table, String8 session_id)
{
	b32 result = 0;
	u64 now = os_now_microseconds();

	MutexScope(table->mutex)
	{
		for(u64 i = 0; i < table->capacity; i += 1)
		{
			Session *s = &table->sessions[i];
			if(s->session_id.size > 0 && str8_match(s->session_id, session_id, 0))
			{
				if(s->expiry_us > now)
				{
					result = 1;
				}
				break;
			}
		}
	}

	return result;
}

internal void
session_delete(SessionTable *table, String8 session_id)
{
	MutexScope(table->mutex)
	{
		for(u64 i = 0; i < table->capacity; i += 1)
		{
			Session *s = &table->sessions[i];
			if(s->session_id.size > 0 && str8_match(s->session_id, session_id, 0))
			{
				MemoryZeroStruct(s);
				table->count -= 1;
				break;
			}
		}
	}
}

internal String8
cookie_parse(String8 cookie_header, String8 name)
{
	for(u64 i = 0; i < cookie_header.size;)
	{
		for(; i < cookie_header.size && (cookie_header.str[i] == ' ' || cookie_header.str[i] == '\t'); i += 1)
		{
		}

		u64 name_start = i;
		for(; i < cookie_header.size && cookie_header.str[i] != '=' && cookie_header.str[i] != ';'; i += 1)
		{
		}
		u64 name_end = i;

		if(i >= cookie_header.size || cookie_header.str[i] != '=')
		{
			for(; i < cookie_header.size && cookie_header.str[i] != ';'; i += 1)
			{
			}
			if(i < cookie_header.size)
			{
				i += 1;
			}
			continue;
		}

		String8 cookie_name = str8_range(cookie_header.str + name_start, cookie_header.str + name_end);
		i += 1;

		u64 value_start = i;
		for(; i < cookie_header.size && cookie_header.str[i] != ';'; i += 1)
		{
		}
		u64 value_end = i;

		if(str8_match(cookie_name, name, 0))
		{
			return str8_range(cookie_header.str + value_start, cookie_header.str + value_end);
		}

		if(i < cookie_header.size)
		{
			i += 1;
		}
	}

	return str8_zero();
}

internal String8
domain_extract_base(String8 host)
{
	u64 dot_count = 0;
	for(u64 i = 0; i < host.size; i += 1)
	{
		if(host.str[i] == '.')
		{
			dot_count += 1;
		}
	}

	if(dot_count < 2)
	{
		return host;
	}

	u64 dot_pos = 0;
	for(u64 i = 0; i < host.size; i += 1)
	{
		if(host.str[i] == '.')
		{
			dot_pos = i;
			break;
		}
	}

	String8 base = str8_skip(host, dot_pos);
	return base;
}

internal String8
cookie_serialize(Arena *arena, String8 name, String8 value, u64 max_age_seconds, b32 is_deletion, String8 domain)
{
	String8 result;
	if(is_deletion && domain.size > 0)
	{
		result = str8f(arena, "%S=deleted; Path=/; HttpOnly; SameSite=Lax; Max-Age=0; Domain=%S", name, domain);
	}
	else if(is_deletion)
	{
		result = str8f(arena, "%S=deleted; Path=/; HttpOnly; SameSite=Lax; Max-Age=0", name);
	}
	else if(domain.size > 0)
	{
		result = str8f(arena, "%S=%S; Path=/; HttpOnly; SameSite=Lax; Max-Age=%u; Domain=%S", name, value, max_age_seconds,
		               domain);
	}
	else
	{
		result = str8f(arena, "%S=%S; Path=/; HttpOnly; SameSite=Lax; Max-Age=%u", name, value, max_age_seconds);
	}
	return result;
}

internal String8
url_decode(Arena *arena, String8 encoded)
{
	read_only local_persist u8 hex_decode[256] = {
	    ['0'] = 0,  ['1'] = 1,  ['2'] = 2,  ['3'] = 3,  ['4'] = 4,  ['5'] = 5,  ['6'] = 6,  ['7'] = 7,
	    ['8'] = 8,  ['9'] = 9,  ['A'] = 10, ['B'] = 11, ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15,
	    ['a'] = 10, ['b'] = 11, ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15,
	};

	u8 *buffer = push_array(arena, u8, encoded.size);
	u64 write_pos = 0;

	for(u64 i = 0; i < encoded.size; i += 1)
	{
		u8 c = encoded.str[i];
		if(c == '%' && i + 2 < encoded.size)
		{
			buffer[write_pos] = (hex_decode[encoded.str[i + 1]] << 4) | hex_decode[encoded.str[i + 2]];
			write_pos += 1;
			i += 2;
		}
		else if(c == '+')
		{
			buffer[write_pos] = ' ';
			write_pos += 1;
		}
		else
		{
			buffer[write_pos] = c;
			write_pos += 1;
		}
	}

	String8 result = {buffer, write_pos};
	return result;
}

internal b32
auth_validate_request(HTTP_Request *req)
{
	String8 cookie_header = http_header_get(&req->headers, str8_lit("Cookie"));
	String8 session_id = cookie_parse(cookie_header, str8_lit("session"));
	if(session_id.size == 0 || sessions == 0)
	{
		return 0;
	}
	return session_validate(sessions, session_id);
}

////////////////////////////////
//~ HTTP Request Handling

internal void
socket_write_all(OS_Handle socket, String8 data)
{
	int fd = (int)socket.u64[0];
	for(u64 written = 0; written < data.size;)
	{
		ssize_t result = write(fd, data.str + written, data.size - written);
		if(result <= 0)
		{
			break;
		}
		written += result;
	}
}

internal void
send_error_response(OS_Handle socket, HTTP_Status status, String8 message)
{
	Temp scratch = scratch_begin(0, 0);

	log_infof("authd: error response %u %S\n", (u32)status, message.size > 0 ? message : str8_zero());

	HTTP_Response *res = http_response_alloc(scratch.arena, status);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/plain"));
	http_header_add(scratch.arena, &res->headers, str8_lit("Connection"), str8_lit("close"));

	if(message.size > 0)
	{
		res->body = message;
	}
	else
	{
		res->body = res->status_text;
	}

	String8 content_length_str = str8_from_u64(scratch.arena, res->body.size, 10, 0, 0);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), content_length_str);

	String8 response_data = http_response_serialize(scratch.arena, res);
	socket_write_all(socket, response_data);

	scratch_end(scratch);
}

internal void
handle_login(HTTP_Request *req, OS_Handle client_socket)
{
	Temp scratch = scratch_begin(0, 0);

	if(req->method == HTTP_Method_GET)
	{
		String8 redirect = str8_lit("/");

		if(req->query.size > 0)
		{
			String8List parts = str8_split(scratch.arena, req->query, (u8 *)"&", 1, 0);
			for(String8Node *n = parts.first; n != 0; n = n->next)
			{
				String8 part = n->string;
				u64 eq_pos = str8_find_needle(part, 0, str8_lit("="), 0);
				if(eq_pos < part.size)
				{
					String8 key = str8_prefix(part, eq_pos);
					String8 value = str8_skip(part, eq_pos + 1);
					if(str8_match(key, str8_lit("redirect"), 0))
					{
						redirect = url_decode(scratch.arena, value);
					}
				}
			}
		}

		String8 html =
		    str8f(scratch.arena,
		          "<!DOCTYPE html>\n"
		          "<html lang=\"en\">\n"
		          "<head>\n"
		          "<meta charset=\"UTF-8\">\n"
		          "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
		          "<title>Login - Matt Dargan</title>\n"
		          "<style>\n"
		          "*{margin:0;padding:0;box-sizing:border-box}\n"
		          "html{font-size:1rem;background-color:#000;color:#fff;min-width:20rem}\n"
		          "body{font-family:georgia,times,serif;line-height:1.6;margin:0 auto;max-width:50rem;padding:0 1rem}\n"
		          "header{color:#878787;padding:1.5rem 0;border-bottom:1px solid #222}\n"
		          "header h1{font-size:1.5rem;font-weight:normal;color:#ededed}\n"
		          "header .subtitle{font-size:0.9rem;color:#878787}\n"
		          "main{padding:2rem 0}\n"
		          "form{max-width:400px;margin:2rem auto}\n"
		          "label{display:block;margin-bottom:0.5rem;color:#fff}\n"
		          "input{width:100%%;padding:0.5rem;margin-bottom:1rem;font-size:1rem;background:#1a1a1a;border:1px solid "
		          "#333;color:#fff;border-radius:3px}\n"
		          "button{width:100%%;padding:0.75rem;font-size:1rem;background:#336696;color:#fff;border:none;border-"
		          "radius:3px;cursor:pointer}\n"
		          "button:hover{background:#4477aa}\n"
		          "a{color:#6699cc;text-decoration:none}\n"
		          "a:hover{text-decoration:underline}\n"
		          "</style>\n"
		          "</head>\n"
		          "<body>\n"
		          "<header>\n"
		          "<h1>Matt Dargan</h1>\n"
		          "<div class=\"subtitle\">Login</div>\n"
		          "</header>\n"
		          "<main>\n"
		          "<form method=\"post\" action=\"/login\">\n"
		          "<input type=\"hidden\" name=\"redirect\" value=\"%S\">\n"
		          "<label for=\"username\">Username</label>\n"
		          "<input type=\"text\" id=\"username\" name=\"username\" required autofocus>\n"
		          "<label for=\"password\">Password</label>\n"
		          "<input type=\"password\" id=\"password\" name=\"password\" required>\n"
		          "<button type=\"submit\">Login</button>\n"
		          "</form>\n"
		          "</main>\n"
		          "</body>\n"
		          "</html>\n",
		          redirect);

		HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
		http_header_add(scratch.arena, &res->headers, str8_lit("Content-Type"), str8_lit("text/html; charset=utf-8"));
		http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), str8f(scratch.arena, "%u", html.size));
		res->body = html;

		String8 response_data = http_response_serialize(scratch.arena, res);
		socket_write_all(client_socket, response_data);
	}
	else if(req->method == HTTP_Method_POST)
	{
		String8 username = str8_zero();
		String8 password = str8_zero();
		String8 redirect_url = str8_lit("/");

		String8List parts = str8_split(scratch.arena, req->body, (u8 *)"&", 1, 0);
		for(String8Node *n = parts.first; n != 0; n = n->next)
		{
			String8 part = n->string;
			u64 eq_pos = str8_find_needle(part, 0, str8_lit("="), 0);
			if(eq_pos < part.size)
			{
				String8 key = str8_prefix(part, eq_pos);
				String8 value = str8_skip(part, eq_pos + 1);
				String8 decoded_value = url_decode(scratch.arena, value);

				if(str8_match(key, str8_lit("username"), 0))
				{
					username = decoded_value;
				}
				else if(str8_match(key, str8_lit("password"), 0))
				{
					password = decoded_value;
				}
				else if(str8_match(key, str8_lit("redirect"), 0))
				{
					redirect_url = decoded_value;
				}
			}
		}

		if(str8_match(username, auth_username, 0) && str8_match(password, auth_password, 0))
		{
			String8 session_id = session_create(sessions, auth_session_duration_us);
			if(session_id.size == 0)
			{
				log_infof("authd: login failed for user %S (session table full)\n", username);

				HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_302_Found);
				http_header_add(scratch.arena, &res->headers, str8_lit("Location"), str8_lit("/login"));
				http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), str8_lit("0"));
				http_header_add(scratch.arena, &res->headers, str8_lit("Connection"), str8_lit("close"));

				String8 response_data = http_response_serialize(scratch.arena, res);
				socket_write_all(client_socket, response_data);
			}
			else
			{
				u64 max_age = auth_session_duration_us / 1000000;

				String8 forwarded_host = http_header_get(&req->headers, str8_lit("X-Forwarded-Host"));
				String8 cookie_domain = str8_zero();
				if(forwarded_host.size > 0)
				{
					cookie_domain = domain_extract_base(forwarded_host);
				}

				HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_302_Found);
				http_header_add(scratch.arena, &res->headers, str8_lit("Location"), redirect_url);
				String8 cookie = cookie_serialize(scratch.arena, str8_lit("session"), session_id, max_age, 0, cookie_domain);
				http_header_add(scratch.arena, &res->headers, str8_lit("Set-Cookie"), cookie);
				http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), str8_lit("0"));

				String8 response_data = http_response_serialize(scratch.arena, res);
				socket_write_all(client_socket, response_data);

				log_infof("authd: user %S logged in, session=%S\n", username, str8_prefix(session_id, 16));
			}
		}
		else
		{
			log_infof("authd: login failed for user %S\n", username);

			HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_302_Found);
			http_header_add(scratch.arena, &res->headers, str8_lit("Location"), str8_lit("/login"));
			http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), str8_lit("0"));
			http_header_add(scratch.arena, &res->headers, str8_lit("Connection"), str8_lit("close"));

			String8 response_data = http_response_serialize(scratch.arena, res);
			socket_write_all(client_socket, response_data);
		}
	}
	else
	{
		send_error_response(client_socket, HTTP_Status_405_MethodNotAllowed,
		                    str8_from_http_status(HTTP_Status_405_MethodNotAllowed));
	}

	scratch_end(scratch);
}

internal void
handle_logout(HTTP_Request *req, OS_Handle client_socket)
{
	Temp scratch = scratch_begin(0, 0);

	String8 cookie_header = http_header_get(&req->headers, str8_lit("Cookie"));
	String8 session_id = cookie_parse(cookie_header, str8_lit("session"));
	if(session_id.size > 0 && sessions != 0)
	{
		session_delete(sessions, session_id);
		log_infof("authd: session %S logged out\n", str8_prefix(session_id, 16));
	}

	String8 forwarded_host = http_header_get(&req->headers, str8_lit("X-Forwarded-Host"));
	String8 cookie_domain = str8_zero();
	if(forwarded_host.size > 0)
	{
		cookie_domain = domain_extract_base(forwarded_host);
	}

	HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_302_Found);
	http_header_add(scratch.arena, &res->headers, str8_lit("Location"), str8_lit("/"));
	String8 cookie = cookie_serialize(scratch.arena, str8_lit("session"), str8_zero(), 0, 1, cookie_domain);
	http_header_add(scratch.arena, &res->headers, str8_lit("Set-Cookie"), cookie);
	http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), str8_lit("0"));

	String8 response_data = http_response_serialize(scratch.arena, res);
	socket_write_all(client_socket, response_data);

	scratch_end(scratch);
}

internal void
handle_http_request(HTTP_Request *req, OS_Handle client_socket)
{
	if(str8_match(req->path, str8_lit("/auth"), 0))
	{
		Temp scratch = scratch_begin(0, 0);
		HTTP_Response *res;
		if(auth_validate_request(req))
		{
			res = http_response_alloc(scratch.arena, HTTP_Status_200_OK);
		}
		else
		{
			res = http_response_alloc(scratch.arena, HTTP_Status_401_Unauthorized);
		}
		http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), str8_lit("0"));
		String8 response_data = http_response_serialize(scratch.arena, res);
		socket_write_all(client_socket, response_data);
		scratch_end(scratch);
		return;
	}

	if(str8_match(req->path, str8_lit("/login"), 0))
	{
		handle_login(req, client_socket);
		return;
	}

	if(str8_match(req->path, str8_lit("/logout"), 0))
	{
		if(!auth_validate_request(req))
		{
			Temp scratch = scratch_begin(0, 0);
			HTTP_Response *res = http_response_alloc(scratch.arena, HTTP_Status_302_Found);
			http_header_add(scratch.arena, &res->headers, str8_lit("Location"), str8_lit("/login?redirect=/logout"));
			http_header_add(scratch.arena, &res->headers, str8_lit("Content-Length"), str8_lit("0"));
			http_header_add(scratch.arena, &res->headers, str8_lit("Connection"), str8_lit("close"));
			String8 response_data = http_response_serialize(scratch.arena, res);
			socket_write_all(client_socket, response_data);
			scratch_end(scratch);
			return;
		}
		handle_logout(req, client_socket);
		return;
	}

	send_error_response(client_socket, HTTP_Status_404_NotFound, str8_lit("Not found"));
}

////////////////////////////////
//~ Connection Handler

internal void
handle_connection(OS_Handle connection_socket)
{
	read_only local_persist u64 max_request_size = MB(1);
	read_only local_persist u64 read_buffer_size = KB(16);

	Temp scratch = scratch_begin(0, 0);
	Arena *connection_arena = arena_alloc();
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	int client_fd = (int)connection_socket.u64[0];

	struct sockaddr_storage client_addr_storage;
	socklen_t client_addr_len = sizeof(client_addr_storage);
	String8 client_ip = str8_lit("unknown");
	u16 client_port = 0;

	if(getpeername(client_fd, (struct sockaddr *)&client_addr_storage, &client_addr_len) == 0)
	{
		if(client_addr_storage.ss_family == AF_INET)
		{
			struct sockaddr_in *addr_in = (struct sockaddr_in *)&client_addr_storage;
			u8 *ip = (u8 *)&addr_in->sin_addr.s_addr;
			client_ip = str8f(scratch.arena, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
			client_port = ntohs(addr_in->sin_port);
		}
		else if(client_addr_storage.ss_family == AF_INET6)
		{
			struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&client_addr_storage;
			u8 *ip = addr_in6->sin6_addr.s6_addr;
			client_ip =
			    str8f(scratch.arena, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", ip[0], ip[1],
			          ip[2], ip[3], ip[4], ip[5], ip[6], ip[7], ip[8], ip[9], ip[10], ip[11], ip[12], ip[13], ip[14], ip[15]);
			client_port = ntohs(addr_in6->sin6_port);
		}
	}

	log_infof("authd: connection from %S:%u\n", client_ip, client_port);

	u8 buffer[read_buffer_size];
	ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

	if(bytes_read > 0)
	{
		if((u64)bytes_read > max_request_size)
		{
			send_error_response(connection_socket, HTTP_Status_413_PayloadTooLarge, str8_lit("Request too large"));
		}
		else
		{
			String8 request_data = str8(buffer, (u64)bytes_read);
			HTTP_Request *req = http_request_parse(connection_arena, request_data);

			if(req->method != HTTP_Method_Unknown && req->path.size > 0)
			{
				handle_http_request(req, connection_socket);
			}
			else
			{
				send_error_response(connection_socket, HTTP_Status_400_BadRequest, str8_lit("Invalid HTTP request"));
			}
		}
	}

	os_file_close(connection_socket);
	log_info(str8_lit("authd: connection closed\n"));
	log_scope_flush(scratch.arena);
	log_release(log);
	arena_release(connection_arena);
	scratch_end(scratch);
}

internal void
handle_connection_task(void *params)
{
	OS_Handle connection;
	connection.u64[0] = (u64)params;
	handle_connection(connection);
}

////////////////////////////////
//~ Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
	read_only local_persist u64 session_duration_days = 7;
	read_only local_persist u64 session_table_max_capacity = 4096;

	Temp scratch = scratch_begin(0, 0);
	Arena *arena = arena_alloc();
	Log *setup_log = log_alloc();
	log_select(setup_log);
	log_scope_begin();

	u16 listen_port = 8080;
	auth_username = cmd_line_string(cmd_line, str8_lit("auth-user"));

	char *auth_password_cstr = getenv("AUTH_PASSWORD");
	if(auth_password_cstr != 0)
	{
		auth_password = str8_cstring(auth_password_cstr);
	}
	else
	{
		auth_password = str8_zero();
	}

	auth_session_duration_us = session_duration_days * 24 * 60 * 60 * 1000000ULL;

	if(auth_username.size > 0 && auth_password.size > 0)
	{
		sessions = session_table_alloc(arena, session_table_max_capacity);
		log_infof("authd: authentication enabled for user %S (session duration: %u days, max sessions: %u)\n",
		          auth_username, session_duration_days, session_table_max_capacity);
	}
	else
	{
		log_info(str8_lit("authd: authentication disabled (no --auth-user or --auth-password specified)\n"));
	}

	log_infof("authd: listening on port %u\n", listen_port);

	OS_Handle listen_socket = os_socket_listen_tcp(listen_port);
	if(os_handle_match(listen_socket, os_handle_zero()))
	{
		log_errorf("authd: failed to listen on port %u\n", listen_port);
		log_scope_flush(scratch.arena);
		log_release(setup_log);
		arena_release(arena);
		scratch_end(scratch);
		return;
	}

	log_info(str8_lit("authd: server ready\n"));
	log_scope_flush(scratch.arena);

	for(;;)
	{
		Temp accept_scratch = scratch_begin(0, 0);
		Log *accept_log = log_alloc();
		log_select(accept_log);
		log_scope_begin();

		OS_Handle connection_socket = os_socket_accept(listen_socket);
		if(os_handle_match(connection_socket, os_handle_zero()))
		{
			log_error(str8_lit("authd: failed to accept connection\n"));
			log_scope_flush(accept_scratch.arena);
			log_release(accept_log);
			scratch_end(accept_scratch);
			continue;
		}

		log_info(str8_lit("authd: accepted connection\n"));
		log_scope_flush(accept_scratch.arena);
		log_release(accept_log);
		scratch_end(accept_scratch);

		Thread conn_thread = thread_launch(handle_connection_task, (void *)connection_socket.u64[0]);
		thread_detach(conn_thread);
	}

	os_file_close(listen_socket);
	log_scope_flush(scratch.arena);
	log_release(setup_log);
	arena_release(arena);
	scratch_end(scratch);
}
