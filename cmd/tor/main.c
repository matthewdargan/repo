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

/* clang-format off */
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
/* clang-format on */

enum { RESULTSPERPAGE = 75 };

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

typedef struct Torrentarray Torrentarray;
struct Torrentarray {
	Torrent *v;
	u64 cnt;
};

readonly static String8 filters[] = {str8litc("0"), str8litc("1"), str8litc("2")};
readonly static String8 categories[] = {
    str8litc("0_0"), str8litc("1_0"), str8litc("1_1"), str8litc("1_2"), str8litc("1_3"), str8litc("1_4"),
    str8litc("2_0"), str8litc("2_1"), str8litc("2_2"), str8litc("3_0"), str8litc("3_1"), str8litc("3_2"),
    str8litc("3_3"), str8litc("4_0"), str8litc("4_1"), str8litc("4_2"), str8litc("4_3"), str8litc("4_4"),
    str8litc("5_0"), str8litc("5_1"), str8litc("5_2"), str8litc("6_0"), str8litc("6_1"), str8litc("6_2"),
};
readonly static String8 sorts[] = {
    str8litc("comments"), str8litc("size"),     str8litc("id"),
    str8litc("seeders"),  str8litc("leechers"), str8litc("downloads"),
};
readonly static String8 orders[] = {str8litc("asc"), str8litc("desc")};

static b32
validparams(Params ps)
{
	b32 filterok, categoryok, sortok, orderok;
	u64 i;

	if (ps.query.len == 0) {
		fprintf(stderr, "query is required\n");
		return 0;
	}
	filterok = 0;
	categoryok = 0;
	sortok = 0;
	orderok = 0;
	for (i = 0; i < nelem(filters); i++)
		filterok |= str8cmp(ps.filter, filters[i], 0);
	for (i = 0; i < nelem(categories); i++)
		categoryok |= str8cmp(ps.category, categories[i], 0);
	if (ps.sort.len > 0)
		for (i = 0; i < nelem(sorts); i++)
			sortok |= str8cmp(ps.sort, sorts[i], 0);
	if (ps.order.len > 0)
		for (i = 0; i < nelem(orders); i++)
			orderok |= str8cmp(ps.order, orders[i], 0);
	if (!filterok) {
		fprintf(stderr, "invalid filter: %s\n", (char *)ps.filter.str);
		return 0;
	}
	if (!categoryok) {
		fprintf(stderr, "invalid category: %s\n", (char *)ps.category.str);
		return 0;
	}
	if (ps.sort.len > 0 && !sortok) {
		fprintf(stderr, "invalid sort: %s\n", (char *)ps.sort.str);
		return 0;
	}
	if (ps.order.len > 0 && !orderok) {
		fprintf(stderr, "invalid order: %s\n", (char *)ps.order.str);
		return 0;
	}
	return 1;
}

static size_t
writecb(void *contents, size_t size, size_t nmemb, void *userp)
{
	u64 sz;
	String8 *chunk;

	sz = size * nmemb;
	chunk = (String8 *)userp;
	if (chunk->len + sz + 1 > 0x100000) {
		fprintf(stderr, "tor: HTTP response too large for chunk\n");
		return 0;
	}
	memcpy(&(chunk->str[chunk->len]), contents, sz);
	chunk->len += sz;
	chunk->str[chunk->len] = 0;
	return sz;
}

static u64
gettotalpages(htmlDocPtr doc, xmlXPathCompExprPtr expr)
{
	xmlXPathContextPtr ctx;
	xmlXPathObjectPtr res;
	xmlNodePtr node;
	xmlChar *data;
	String8 s, of, space, totalstr;
	u64 ofpos, numpos, spacepos, total;

	ctx = xmlXPathNewContext(doc);
	if (ctx == NULL) {
		fprintf(stderr, "tor: unable to create XPath context\n");
		return 1;
	}
	res = xmlXPathCompiledEval(expr, ctx);
	if (res == NULL || xmlXPathNodeSetIsEmpty(res->nodesetval))
		return 1;
	node = res->nodesetval->nodeTab[0];
	data = xmlNodeGetContent(node);
	s = str8(data, xmlStrlen(data));
	of = str8lit("of ");
	space = str8lit(" ");
	ofpos = str8index(s, 0, of, 0);
	numpos = ofpos + of.len;
	spacepos = str8index(s, numpos, space, 0);
	if (numpos >= s.len || spacepos >= s.len)
		return 1;
	totalstr = str8substr(s, rng1u64(numpos, spacepos));
	total = str8tou64(totalstr, 10);
	return (total + RESULTSPERPAGE - 1) / RESULTSPERPAGE;
}

static Torrentarray
extracttorrents(Arena *a, htmlDocPtr doc, xmlXPathCompExprPtr rowexpr, xmlXPathCompExprPtr titleexpr,
                xmlXPathCompExprPtr magnetexpr)
{
	Torrentarray torrents;
	xmlXPathContextPtr ctx;
	xmlXPathObjectPtr res;
	u64 i;
	xmlNodePtr row, titlenode, magnetnode;
	xmlXPathObjectPtr titleres, magnetres;
	xmlChar *title, *magnetpath;

	memset(&torrents, 0, sizeof(Torrentarray));
	ctx = xmlXPathNewContext(doc);
	if (ctx == NULL) {
		fprintf(stderr, "tor: unable to create XPath context\n");
		return torrents;
	}
	res = xmlXPathCompiledEval(rowexpr, ctx);
	if (res == NULL || xmlXPathNodeSetIsEmpty(res->nodesetval))
		return torrents;
	torrents.cnt = res->nodesetval->nodeNr;
	torrents.v = pusharrnoz(a, Torrent, torrents.cnt);
	for (i = 0; i < torrents.cnt; i++) {
		row = res->nodesetval->nodeTab[i];
		ctx->node = row;
		titleres = xmlXPathCompiledEval(titleexpr, ctx);
		magnetres = xmlXPathCompiledEval(magnetexpr, ctx);
		if (titleres && !xmlXPathNodeSetIsEmpty(titleres->nodesetval) && magnetres &&
		    !xmlXPathNodeSetIsEmpty(magnetres->nodesetval)) {
			titlenode = titleres->nodesetval->nodeTab[0];
			magnetnode = magnetres->nodesetval->nodeTab[0];
			title = xmlNodeGetContent(titlenode);
			magnetpath = xmlNodeGetContent(magnetnode);
			torrents.v[i].title = pushstr8cpy(a, str8((u8 *)title, xmlStrlen(title)));
			torrents.v[i].magnet = pushstr8cpy(a, str8((u8 *)magnetpath, xmlStrlen(magnetpath)));
		}
	}
	return torrents;
}

static Torrentarray
gettorrents(Arena *a, Params ps)
{
	Torrentarray torrents, pagetorrents;
	xmlXPathCompExprPtr pagexpr, rowexpr, titleexpr, magnetexpr;
	CURL *curl;
	char *curlencodedquery;
	String8 chunk, encodedquery, baseurl, user, query, sort, order, url, queryurl;
	u64 page, npages;
	CURLcode res;
	long httpcode;
	htmlDocPtr doc;

	memset(&torrents, 0, sizeof(Torrentarray));
	pagexpr = xmlXPathCompile(BAD_CAST "//div[@class='pagination-page-info']");
	rowexpr = xmlXPathCompile(BAD_CAST "//table/tbody/tr");
	titleexpr = xmlXPathCompile(BAD_CAST "./td[2]/a[last()]");
	magnetexpr = xmlXPathCompile(BAD_CAST "./td[3]/a[2]/@href");
	curl = curl_easy_init();
	if (curl == NULL) {
		fprintf(stderr, "tor: could not initialize curl\n");
		return torrents;
	}
	curlencodedquery = curl_easy_escape(curl, (const char *)ps.query.str, ps.query.len);
	if (curlencodedquery == NULL) {
		fprintf(stderr, "tor: could not URL encode query\n");
		curl_easy_cleanup(curl);
		return torrents;
	}
	chunk.str = pusharrnoz(a, u8, 0x100000);
	chunk.len = 0;
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "tor/1.0");
	encodedquery = pushstr8cpy(a, str8cstr(curlencodedquery));
	baseurl = str8lit("https://nyaa.si");
	user = ps.user.len > 0 ? pushstr8cat(a, str8lit("/user/"), ps.user) : str8zero();
	query = pushstr8f(a, (char *)"?f=%s&c=%s&q=%s", ps.filter.str, ps.category.str, encodedquery.str);
	sort = ps.sort.len > 0 ? pushstr8cat(a, str8lit("&s="), ps.sort) : str8zero();
	order = ps.order.len > 0 ? pushstr8cat(a, str8lit("&o="), ps.order) : str8zero();
	url = pushstr8cat(a, baseurl, user);
	url = pushstr8cat(a, url, query);
	url = pushstr8cat(a, url, sort);
	url = pushstr8cat(a, url, order);
	for (page = 1, npages = 1; page <= npages; page++) {
		queryurl = pushstr8f(a, (char *)"%s&p=%ld", url.str, page);
		chunk.len = 0;
		curl_easy_setopt(curl, CURLOPT_URL, queryurl.str);
		res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			fprintf(stderr, "tor: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			continue;
		}
		httpcode = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcode);
		if (httpcode != 200) {
			fprintf(stderr, "tor: HTTP error %ld for page %lu\n", httpcode, page);
			continue;
		}
		doc = htmlReadMemory((const char *)chunk.str, chunk.len, (const char *)queryurl.str, NULL,
		                     HTML_PARSE_NOWARNING | HTML_PARSE_NOERROR);
		if (doc == NULL) {
			fprintf(stderr, "tor: failed to parse HTML for page %lu\n", page);
			continue;
		}
		if (page == 1) {
			npages = gettotalpages(doc, pagexpr);
			torrents.v = pusharrnoz(a, Torrent, npages * RESULTSPERPAGE);
		}
		pagetorrents = extracttorrents(a, doc, rowexpr, titleexpr, magnetexpr);
		if (pagetorrents.cnt > 0) {
			memcpy(torrents.v + torrents.cnt, pagetorrents.v, sizeof(Torrent) * pagetorrents.cnt);
			torrents.cnt += pagetorrents.cnt;
		}
	}
	curl_easy_cleanup(curl);
	return torrents;
}

static void *
arenamalloccb(u64 size)
{
	return pusharrnoz(arena, u8, size);
}

static void
arenafreecb(void *)
{
}

static void *
arenarealloccb(void *p, u64 size)
{
	u8 *np;

	if (p == NULL)
		return arenamalloccb(size);
	if (size == 0)
		return NULL;
	np = pusharrnoz(arena, u8, size);
	memmove(np, p, size);
	return np;
}

static char *
arenastrdupcb(const char *s)
{
	u64 len;
	u8 *dup;

	len = cstrlen((u8 *)s);
	dup = pusharrnoz(arena, u8, len + 1);
	memcpy(dup, s, len);
	dup[len] = 0;
	return (char *)dup;
}

static void *
arenacalloccb(u64 nmemb, u64 size)
{
	u64 sz;
	u8 *p;

	sz = nmemb * size;
	p = pusharr(arena, u8, sz);
	return p;
}

int
main(int argc, char *argv[])
{
	Arenaparams ap;
	String8list args;
	Cmd parsed;
	Torrentarray torrents;
	String8 filter, category, user, sort, order, query, data, line;
	Params ps;
	u64 i;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	filter = str8lit("0");
	category = str8lit("0_0");
	user = str8zero();
	sort = str8zero();
	order = str8zero();
	query = str8zero();
	if (cmdhasarg(&parsed, str8lit("f")))
		filter = cmdstr(&parsed, str8lit("f"));
	if (cmdhasarg(&parsed, str8lit("c")))
		category = cmdstr(&parsed, str8lit("c"));
	if (cmdhasarg(&parsed, str8lit("u")))
		user = cmdstr(&parsed, str8lit("u"));
	if (cmdhasarg(&parsed, str8lit("s")))
		sort = cmdstr(&parsed, str8lit("s"));
	if (cmdhasarg(&parsed, str8lit("o")))
		order = cmdstr(&parsed, str8lit("o"));
	if (cmdhasarg(&parsed, str8lit("q")))
		query = cmdstr(&parsed, str8lit("q"));
	ps.filter = filter;
	ps.category = category;
	ps.user = user;
	ps.sort = sort;
	ps.order = order;
	ps.query = query;
	if (!validparams(ps)) {
		arenarelease(arena);
		return 1;
	}
	curl_global_init_mem(CURL_GLOBAL_DEFAULT, arenamalloccb, arenafreecb, arenarealloccb, arenastrdupcb, arenacalloccb);
	xmlMemSetup(arenafreecb, arenamalloccb, arenarealloccb, arenastrdupcb);
	xmlInitParser();
	torrents = gettorrents(arena, ps);
	if (torrents.cnt == 0) {
		fprintf(stderr, "tor: no torrents found\n");
		xmlCleanupParser();
		curl_global_cleanup();
		arenarelease(arena);
		return 1;
	}
	data = str8zero();
	for (i = 0; i < torrents.cnt; i++) {
		line = pushstr8f(arena, (char *)"%s\t%s\n", torrents.v[i].title.str, torrents.v[i].magnet.str);
		data = pushstr8cat(arena, data, line);
	}
	fwrite(data.str, 1, data.len, stdout);
	xmlCleanupParser();
	curl_global_cleanup();
	arenarelease(arena);
	return 0;
}
