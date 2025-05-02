#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// clang-format off
#include "libu/u.h"
#include "libu/arena.h"
#include "libu/string.h"
#include "libu/cmd.h"
#include "libu/os.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/cmd.c"
#include "libu/os.c"
// clang-format on

#define CHUNK_SIZE MB(1)
#define RESULTS_PER_PAGE 75

typedef struct Params Params;
struct Params {
	String8 filter;
	String8 category;
	String8 user;
	String8 sort;
	String8 order;
	String8 query;
};

typedef struct Torrent Torrent;
struct Torrent {
	String8 title;
	String8 magnet;
};

typedef struct TorrentArray TorrentArray;
struct TorrentArray {
	Torrent *v;
	u64 cnt;
};

read_only static String8 filters[] = {str8_lit_comp("0"), str8_lit_comp("1"), str8_lit_comp("2")};
read_only static String8 categories[] = {
    str8_lit_comp("0_0"), str8_lit_comp("1_0"), str8_lit_comp("1_1"), str8_lit_comp("1_2"), str8_lit_comp("1_3"),
    str8_lit_comp("1_4"), str8_lit_comp("2_0"), str8_lit_comp("2_1"), str8_lit_comp("2_2"), str8_lit_comp("3_0"),
    str8_lit_comp("3_1"), str8_lit_comp("3_2"), str8_lit_comp("3_3"), str8_lit_comp("4_0"), str8_lit_comp("4_1"),
    str8_lit_comp("4_2"), str8_lit_comp("4_3"), str8_lit_comp("4_4"), str8_lit_comp("5_0"), str8_lit_comp("5_1"),
    str8_lit_comp("5_2"), str8_lit_comp("6_0"), str8_lit_comp("6_1"), str8_lit_comp("6_2"),
};
read_only static String8 sorts[] = {
    str8_lit_comp("comments"), str8_lit_comp("size"),     str8_lit_comp("id"),
    str8_lit_comp("seeders"),  str8_lit_comp("leechers"), str8_lit_comp("downloads"),
};
read_only static String8 orders[] = {str8_lit_comp("asc"), str8_lit_comp("desc")};

static b32
valid_params(Params ps)
{
	if (ps.query.len == 0) {
		fprintf(stderr, "query is required\n");
		return 0;
	}
	b32 filter_valid = 0;
	b32 category_valid = 0;
	b32 sort_valid = 0;
	b32 order_valid = 0;
	for (u64 i = 0; i < ARRAY_COUNT(filters); ++i) {
		filter_valid |= str8_cmp(ps.filter, filters[i], 0);
	}
	for (u64 i = 0; i < ARRAY_COUNT(categories); ++i) {
		category_valid |= str8_cmp(ps.category, categories[i], 0);
	}
	if (ps.sort.len > 0) {
		for (u64 i = 0; i < ARRAY_COUNT(sorts); ++i) {
			sort_valid |= str8_cmp(ps.sort, sorts[i], 0);
		}
	}
	if (ps.order.len > 0) {
		for (u64 i = 0; i < ARRAY_COUNT(orders); ++i) {
			order_valid |= str8_cmp(ps.order, orders[i], 0);
		}
	}
	if (!filter_valid) {
		fprintf(stderr, "invalid filter: %s\n", (char *)ps.filter.str);
		return 0;
	}
	if (!category_valid) {
		fprintf(stderr, "invalid category: %s\n", (char *)ps.category.str);
		return 0;
	}
	if (ps.sort.len > 0 && !sort_valid) {
		fprintf(stderr, "invalid sort: %s\n", (char *)ps.sort.str);
		return 0;
	}
	if (ps.order.len > 0 && !order_valid) {
		fprintf(stderr, "invalid order: %s\n", (char *)ps.order.str);
		return 0;
	}
	return 1;
}

static size_t
write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
	u64 real_size = size * nmemb;
	String8 *chunk = (String8 *)userp;
	if (chunk->len + real_size + 1 > CHUNK_SIZE) {
		fprintf(stderr, "tor: HTTP response too large for chunk\n");
		return 0;
	}
	memcpy(&(chunk->str[chunk->len]), contents, real_size);
	chunk->len += real_size;
	chunk->str[chunk->len] = 0;
	return real_size;
}

static u64
get_total_pages(htmlDocPtr doc, xmlXPathCompExprPtr expr)
{
	xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
	if (ctx == NULL) {
		fprintf(stderr, "tor: unable to create XPath context\n");
		return 1;
	}
	xmlXPathObjectPtr res = xmlXPathCompiledEval(expr, ctx);
	if (res == NULL || xmlXPathNodeSetIsEmpty(res->nodesetval)) {
		return 1;
	}
	xmlNodePtr node = res->nodesetval->nodeTab[0];
	xmlChar *data = xmlNodeGetContent(node);
	String8 s = str8((u8 *)data, xmlStrlen(data));
	String8 of = str8_lit("of ");
	String8 space = str8_lit(" ");
	u64 of_pos = str8_index(s, 0, of, 0);
	u64 num_pos = of_pos + of.len;
	u64 space_pos = str8_index(s, num_pos, space, 0);
	if (num_pos >= s.len || space_pos >= s.len) {
		return 1;
	}
	String8 total_str = str8_substr(s, rng1u64(num_pos, space_pos));
	u64 total = str8_to_u64(total_str, 10);
	return (total + RESULTS_PER_PAGE - 1) / RESULTS_PER_PAGE;
}

static TorrentArray
extract_torrents(Arena *a, htmlDocPtr doc, xmlXPathCompExprPtr row_expr, xmlXPathCompExprPtr title_expr,
                 xmlXPathCompExprPtr magnet_expr)
{
	TorrentArray torrents = {0};
	xmlXPathContextPtr ctx = xmlXPathNewContext(doc);
	if (ctx == NULL) {
		fprintf(stderr, "tor: unable to create XPath context\n");
		return torrents;
	}
	xmlXPathObjectPtr res = xmlXPathCompiledEval(row_expr, ctx);
	if (res == NULL || xmlXPathNodeSetIsEmpty(res->nodesetval)) {
		return torrents;
	}
	torrents.cnt = res->nodesetval->nodeNr;
	torrents.v = push_array_no_zero(a, Torrent, torrents.cnt);
	for (u64 i = 0; i < torrents.cnt; ++i) {
		xmlNodePtr row = res->nodesetval->nodeTab[i];
		ctx->node = row;
		xmlXPathObjectPtr title_res = xmlXPathCompiledEval(title_expr, ctx);
		xmlXPathObjectPtr magnet_res = xmlXPathCompiledEval(magnet_expr, ctx);
		if (title_res && !xmlXPathNodeSetIsEmpty(title_res->nodesetval) && magnet_res &&
		    !xmlXPathNodeSetIsEmpty(magnet_res->nodesetval)) {
			xmlNodePtr title_node = title_res->nodesetval->nodeTab[0];
			xmlNodePtr magnet_node = magnet_res->nodesetval->nodeTab[0];
			xmlChar *title = xmlNodeGetContent(title_node);
			xmlChar *magnet_path = xmlNodeGetContent(magnet_node);
			torrents.v[i].title = push_str8_copy(a, str8((u8 *)title, xmlStrlen(title)));
			torrents.v[i].magnet = push_str8_copy(a, str8((u8 *)magnet_path, xmlStrlen(magnet_path)));
		}
	}
	return torrents;
}

static TorrentArray
get_torrents(Arena *a, Params ps)
{
	TorrentArray torrents = {0};
	xmlXPathCompExprPtr pagination_expr = xmlXPathCompile(BAD_CAST "//div[@class='pagination-page-info']");
	xmlXPathCompExprPtr row_expr = xmlXPathCompile(BAD_CAST "//table/tbody/tr");
	xmlXPathCompExprPtr title_expr = xmlXPathCompile(BAD_CAST "./td[2]/a[last()]");
	xmlXPathCompExprPtr magnet_expr = xmlXPathCompile(BAD_CAST "./td[3]/a[2]/@href");
	CURL *curl = curl_easy_init();
	if (curl == NULL) {
		fprintf(stderr, "tor: could not initialize curl\n");
		return torrents;
	}
	char *curl_encoded_query = curl_easy_escape(curl, (const char *)ps.query.str, ps.query.len);
	if (curl_encoded_query == NULL) {
		fprintf(stderr, "tor: could not URL encode query\n");
		curl_easy_cleanup(curl);
		return torrents;
	}
	String8 chunk = {
	    .str = push_array_no_zero(a, u8, CHUNK_SIZE),
	    .len = 0,
	};
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "tor/1.0");
	String8 encoded_query = push_str8_copy(a, str8_cstr(curl_encoded_query));
	String8 base_url = str8_lit("https://nyaa.si");
	String8 user = ps.user.len > 0 ? push_str8_cat(a, str8_lit("/user/"), ps.user) : str8_zero();
	String8 query = push_str8f(a, (char *)"?f=%s&c=%s&q=%s", ps.filter.str, ps.category.str, encoded_query.str);
	String8 sort = ps.sort.len > 0 ? push_str8_cat(a, str8_lit("&s="), ps.sort) : str8_zero();
	String8 order = ps.order.len > 0 ? push_str8_cat(a, str8_lit("&o="), ps.order) : str8_zero();
	String8 url = push_str8_cat(a, base_url, user);
	url = push_str8_cat(a, url, query);
	url = push_str8_cat(a, url, sort);
	url = push_str8_cat(a, url, order);
	for (u64 page = 1, total_pages = 1; page <= total_pages; ++page) {
		String8 query_url = push_str8f(a, (char *)"%s&p=%ld", url.str, page);
		chunk.len = 0;
		curl_easy_setopt(curl, CURLOPT_URL, query_url.str);
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			fprintf(stderr, "tor: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			continue;
		}
		long http_code = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
		if (http_code != 200) {
			fprintf(stderr, "tor: HTTP error %ld for page %lu\n", http_code, page);
			continue;
		}
		htmlDocPtr doc = htmlReadMemory((const char *)chunk.str, chunk.len, (const char *)query_url.str, NULL,
		                                HTML_PARSE_NOWARNING | HTML_PARSE_NOERROR);
		if (doc == NULL) {
			fprintf(stderr, "tor: failed to parse HTML for page %lu\n", page);
			continue;
		}
		if (page == 1) {
			total_pages = get_total_pages(doc, pagination_expr);
			torrents.v = push_array_no_zero(a, Torrent, total_pages * RESULTS_PER_PAGE);
		}
		TorrentArray page_torrents = extract_torrents(a, doc, row_expr, title_expr, magnet_expr);
		if (page_torrents.cnt > 0) {
			memcpy(torrents.v + torrents.cnt, page_torrents.v, sizeof(Torrent) * page_torrents.cnt);
			torrents.cnt += page_torrents.cnt;
		}
	}
	curl_easy_cleanup(curl);
	return torrents;
}

static void *
arena_malloc_callback(u64 size)
{
	return push_array_no_zero(arena, u8, size);
}

static void
arena_free_callback(void *)
{
}

static void *
arena_realloc_callback(void *p, u64 size)
{
	if (p == NULL) {
		return arena_malloc_callback(size);
	}
	if (size == 0) {
		return NULL;
	}
	u8 *new_p = push_array_no_zero(arena, u8, size);
	memmove(new_p, p, size);
	return new_p;
}

static char *
arena_strdup_callback(const char *s)
{
	u64 len = cstr8_len((u8 *)s);
	u8 *dup = push_array_no_zero(arena, u8, len + 1);
	memcpy(dup, s, len);
	dup[len] = 0;
	return (char *)dup;
}

static void *
arena_calloc_callback(u64 nmemb, u64 size)
{
	u64 total_size = nmemb * size;
	u8 *p = push_array(arena, u8, total_size);
	return p;
}

int
main(int argc, char *argv[])
{
	sys_info.nprocs = (u32)sysconf(_SC_NPROCESSORS_ONLN);
	sys_info.page_size = (u64)sysconf(_SC_PAGESIZE);
	sys_info.large_page_size = MB(2);
	arena = arena_alloc((ArenaParams){
	    .flags = arena_default_flags, .res_size = arena_default_res_size, .cmt_size = arena_default_cmt_size});
	String8List args = os_args(arena, argc, argv);
	Cmd parsed = cmd_parse(arena, args);
	Temp scratch = temp_begin(arena);
	TorrentArray torrents = {0};
	String8 filter = str8_lit("0");
	String8 category = str8_lit("0_0");
	String8 user = str8_zero();
	String8 sort = str8_zero();
	String8 order = str8_zero();
	String8 query = str8_zero();
	if (cmd_has_arg(&parsed, str8_lit("f"))) {
		filter = cmd_str(&parsed, str8_lit("f"));
	}
	if (cmd_has_arg(&parsed, str8_lit("c"))) {
		category = cmd_str(&parsed, str8_lit("c"));
	}
	if (cmd_has_arg(&parsed, str8_lit("u"))) {
		user = cmd_str(&parsed, str8_lit("u"));
	}
	if (cmd_has_arg(&parsed, str8_lit("s"))) {
		sort = cmd_str(&parsed, str8_lit("s"));
	}
	if (cmd_has_arg(&parsed, str8_lit("o"))) {
		order = cmd_str(&parsed, str8_lit("o"));
	}
	if (cmd_has_arg(&parsed, str8_lit("q"))) {
		query = cmd_str(&parsed, str8_lit("q"));
	}
	Params ps = {
	    .filter = filter,
	    .category = category,
	    .user = user,
	    .sort = sort,
	    .order = order,
	    .query = query,
	};
	if (!valid_params(ps)) {
		temp_end(scratch);
		arena_release(arena);
		return 1;
	}
	curl_global_init_mem(CURL_GLOBAL_DEFAULT, arena_malloc_callback, arena_free_callback, arena_realloc_callback,
	                     arena_strdup_callback, arena_calloc_callback);
	xmlMemSetup(arena_free_callback, arena_malloc_callback, arena_realloc_callback, arena_strdup_callback);
	xmlInitParser();
	torrents = get_torrents(scratch.a, ps);
	if (torrents.cnt == 0) {
		fprintf(stderr, "tor: no torrents found\n");
		xmlCleanupParser();
		curl_global_cleanup();
		temp_end(scratch);
		arena_release(arena);
		return 1;
	}
	String8 data = str8_zero();
	for (u64 i = 0; i < torrents.cnt; ++i) {
		String8 line = push_str8f(scratch.a, (char *)"%s\t%s\n", torrents.v[i].title.str, torrents.v[i].magnet.str);
		data = push_str8_cat(scratch.a, data, line);
	}
	fwrite(data.str, 1, data.len, stdout);
	xmlCleanupParser();
	curl_global_cleanup();
	temp_end(scratch);
	arena_release(arena);
	return 0;
}
