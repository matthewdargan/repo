#include <curl/curl.h>
#include <dirent.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <stdio.h>
#include <sys/mman.h>

#include <libtorrent/session.hpp>
#include <libtorrent/torrent_info.hpp>

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

enum { MAXTORS = 75 };

typedef struct Params Params;
struct Params {
	u64 topresults;
	String8 filter;
	String8 category;
	String8 user;
	String8 sort;
	String8 order;
	String8 query;
};

readonly static String8 filters[] = {str8lit("0"), str8lit("1"), str8lit("2")};
readonly static String8 categories[] = {
    str8lit("0_0"), str8lit("1_0"), str8lit("1_1"), str8lit("1_2"), str8lit("1_3"), str8lit("1_4"),
    str8lit("2_0"), str8lit("2_1"), str8lit("2_2"), str8lit("3_0"), str8lit("3_1"), str8lit("3_2"),
    str8lit("3_3"), str8lit("4_0"), str8lit("4_1"), str8lit("4_2"), str8lit("4_3"), str8lit("4_4"),
    str8lit("5_0"), str8lit("5_1"), str8lit("5_2"), str8lit("6_0"), str8lit("6_1"), str8lit("6_2"),
};
readonly static String8 sorts[] = {
    str8lit("comments"), str8lit("size"), str8lit("id"), str8lit("seeders"), str8lit("leechers"), str8lit("downloads"),
};
readonly static String8 orders[] = {str8lit("asc"), str8lit("desc")};

static b32
validparams(Params ps)
{
	b32 filterok, categoryok, sortok, orderok;
	u64 i;

	if (ps.query.len == 0) {
		fprintf(stderr, "query is required\n");
		return 0;
	}
	if (ps.topresults <= 0 || ps.topresults > MAXTORS) {
		fprintf(stderr, "invalid top results: %lu\n", ps.topresults);
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
		fprintf(stderr, "invalid filter: %s\n", ps.filter.str);
		return 0;
	}
	if (!categoryok) {
		fprintf(stderr, "invalid category: %s\n", ps.category.str);
		return 0;
	}
	if (ps.sort.len > 0 && !sortok) {
		fprintf(stderr, "invalid sort: %s\n", ps.sort.str);
		return 0;
	}
	if (ps.order.len > 0 && !orderok) {
		fprintf(stderr, "invalid order: %s\n", ps.order.str);
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
		fprintf(stderr, "torrss: HTTP response too large for chunk\n");
		return 0;
	}
	memcpy(&(chunk->str[chunk->len]), contents, sz);
	chunk->len += sz;
	chunk->str[chunk->len] = 0;
	return sz;
}

static String8array
gettorrents(Arena *a, Params ps)
{
	String8array torrents;
	xmlXPathCompExprPtr itemexpr, linkexpr;
	CURL *curl;
	char *curlencodedquery;
	String8 chunk, encodedquery, baseurl, user, query, sort, order, url;
	CURLcode res;
	long httpcode;
	xmlDocPtr doc;
	xmlXPathContextPtr context;
	xmlXPathObjectPtr itemres, linkres;
	u64 i;
	xmlNodePtr item, linknode;
	xmlChar *link;

	torrents = str8arrayreserve(a, ps.topresults);
	itemexpr = xmlXPathCompile(BAD_CAST "//item");
	linkexpr = xmlXPathCompile(BAD_CAST "./link");
	curl = curl_easy_init();
	if (curl == NULL) {
		fprintf(stderr, "torrss: could not initialize curl\n");
		return torrents;
	}
	curlencodedquery = curl_easy_escape(curl, (const char *)ps.query.str, ps.query.len);
	if (curlencodedquery == NULL) {
		fprintf(stderr, "torrss: could not URL encode query\n");
		curl_easy_cleanup(curl);
		return torrents;
	}
	chunk.str = pusharrnoz(a, u8, 0x100000);
	chunk.len = 0;
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "torrss/1.0");
	encodedquery = pushstr8cpy(a, str8cstr(curlencodedquery));
	baseurl = str8lit("https://nyaa.si?page=rss");
	user = ps.user.len > 0 ? pushstr8cat(a, str8lit("&u="), ps.user) : str8zero();
	query = pushstr8f(a, (char *)"&f=%s&c=%s&q=%s", ps.filter.str, ps.category.str, encodedquery.str);
	sort = ps.sort.len > 0 ? pushstr8cat(a, str8lit("&s="), ps.sort) : str8zero();
	order = ps.order.len > 0 ? pushstr8cat(a, str8lit("&o="), ps.order) : str8zero();
	url = pushstr8cat(a, baseurl, user);
	url = pushstr8cat(a, url, query);
	url = pushstr8cat(a, url, sort);
	url = pushstr8cat(a, url, order);
	curl_easy_setopt(curl, CURLOPT_URL, url.str);
	res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		fprintf(stderr, "torrss: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		curl_easy_cleanup(curl);
		return torrents;
	}
	httpcode = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcode);
	if (httpcode != 200) {
		fprintf(stderr, "torrss: HTTP error %ld\n", httpcode);
		curl_easy_cleanup(curl);
		return torrents;
	}
	curl_easy_cleanup(curl);
	doc = xmlReadMemory((const char *)chunk.str, chunk.len, (const char *)url.str, NULL,
	                    XML_PARSE_NOWARNING | XML_PARSE_NOERROR);
	if (doc == NULL) {
		fprintf(stderr, "torrss: failed to parse XML\n");
		return torrents;
	}
	context = xmlXPathNewContext(doc);
	if (context == NULL) {
		fprintf(stderr, "torrss: unable to create XPath context\n");
		return torrents;
	}
	itemres = xmlXPathCompiledEval(itemexpr, context);
	if (itemres == NULL || xmlXPathNodeSetIsEmpty(itemres->nodesetval))
		return torrents;
	torrents.cnt = min(ps.topresults, (u64)itemres->nodesetval->nodeNr);
	for (i = 0; i < torrents.cnt; i++) {
		item = itemres->nodesetval->nodeTab[i];
		context->node = item;
		linkres = xmlXPathCompiledEval(linkexpr, context);
		if (linkres && !xmlXPathNodeSetIsEmpty(linkres->nodesetval)) {
			linknode = linkres->nodesetval->nodeTab[0];
			link = xmlNodeGetContent(linknode);
			torrents.v[i] = pushstr8cpy(a, str8((u8 *)link, xmlStrlen(link)));
		}
	}
	return torrents;
}

static void
downloadtorrents(Arena *a, String8array torrents)
{
	char *home;
	String8 cfgpath, historypath, historydata, tmppath, torrentstodl, torrentnl;
	libtorrent::settings_pack pack;
	CURL *curl;
	u64 i, j;
	FILE *fp;
	CURLcode res;
	long httpcode;
	libtorrent::add_torrent_params ps;
	libtorrent::error_code ec;
	time_t lastupdate, now;
	b32 done;
	std::vector<libtorrent::torrent_handle> handles;
	libtorrent::torrent_handle h;
	libtorrent::torrent_status s;
	u8 *statusstr;

	if (torrents.cnt == 0)
		return;
	home = getenv("HOME");
	cfgpath = pushstr8cat(a, str8cstr(home), str8lit("/.config/torrss"));
	if (!direxists(cfgpath))
		osmkdir(cfgpath);
	historypath = pushstr8cat(a, cfgpath, str8lit("/history"));
	historydata = str8zero();
	if (fileexists(historypath))
		historydata = readfile(a, historypath);
	pack.set_int(libtorrent::settings_pack::alert_mask,
	             libtorrent::alert::status_notification | libtorrent::alert::error_notification);
	libtorrent::session session(pack);
	curl = curl_easy_init();
	if (curl == NULL) {
		fprintf(stderr, "torrss: could not initialize curl\n");
		return;
	}
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "torrss/1.0");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	tmppath = str8lit("/tmp/torrss.torrent");
	torrentstodl = str8zero();
	for (i = 0; i < torrents.cnt; i++) {
		if (historydata.len > 0)
			if (str8index(historydata, 0, torrents.v[i], 0) != historydata.len) {
				fprintf(stderr, "torrss: already downloaded %s\n", torrents.v[i].str);
				continue;
			}
		fp = fopen((const char *)tmppath.str, "wb");
		if (fp == NULL) {
			fprintf(stderr, "torrss: could not write to temporary file: %s\n", tmppath.str);
			continue;
		}
		curl_easy_setopt(curl, CURLOPT_URL, torrents.v[i].str);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		res = curl_easy_perform(curl);
		fclose(fp);
		if (res != CURLE_OK) {
			fprintf(stderr, "torrss: curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
			continue;
		}
		httpcode = 0;
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpcode);
		if (httpcode != 200) {
			fprintf(stderr, "torrss: HTTP error %ld\n", httpcode);
			continue;
		}
		ps.ti = std::make_shared<libtorrent::torrent_info>((const char *)tmppath.str, ec);
		if (ec) {
			fprintf(stderr, "torrss: failed to parse torrent file: %s\n", ec.message().c_str());
			continue;
		}
		ps.save_path = ".";
		session.add_torrent(ps);
		torrentnl = pushstr8cat(a, torrents.v[i], str8lit("\n"));
		torrentstodl = pushstr8cat(a, torrentstodl, torrentnl);
	}
	unlink((const char *)tmppath.str);
	curl_easy_cleanup(curl);
	if (i == 0) {
		fprintf(stderr, "torrss: no torrents to download\n");
		return;
	}
	appendfile(historypath, torrentstodl);
	lastupdate = time(0);
	for (done = 0; !done;) {
		now = time(0);
		if (now - lastupdate >= 1) {
			lastupdate = now;
			handles = session.get_torrents();
			done = 1;
			for (j = 0; j < handles.size(); j++) {
				h = handles[j];
				if (!h.is_valid())
					continue;
				s = h.status();
				switch (s.state) {
					case libtorrent::torrent_status::seeding:
						statusstr = (u8 *)"seeding";
						break;
					case libtorrent::torrent_status::finished:
						statusstr = (u8 *)"finished";
						break;
					case libtorrent::torrent_status::downloading:
						statusstr = (u8 *)"downloading";
						break;
					default:
						statusstr = (u8 *)"other";
						break;
				}
				printf("torrss: name=%s, status=%s, downloaded=%ld, peers=%d\n", s.name.c_str(), statusstr,
				       s.total_done, s.num_peers);
				done &=
				    (s.state == libtorrent::torrent_status::seeding || s.state == libtorrent::torrent_status::finished);
			}
		}
		sleepms(100);
	}
	printf("torrss: downloaded %lu torrents\n", i);
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
	u64 topresults;
	String8 filter, category, user, sort, order, query, topresultsstr;
	Params ps;
	String8array torrents;

	sysinfo.nprocs = (u32)sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = (u64)sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	topresults = MAXTORS;
	filter = str8lit("0");
	category = str8lit("0_0");
	user = str8zero();
	sort = str8zero();
	order = str8zero();
	query = str8zero();
	args = osargs(arena, argc, argv);
	parsed = cmdparse(arena, args);
	if (cmdhasarg(&parsed, str8lit("t"))) {
		topresultsstr = cmdstr(&parsed, str8lit("t"));
		str8tou64ok(topresultsstr, &topresults);
	}
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
	ps.topresults = topresults;
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
		fprintf(stderr, "torrss: no torrents found\n");
		xmlCleanupParser();
		curl_global_cleanup();
		arenarelease(arena);
		return 1;
	}
	downloadtorrents(arena, torrents);
	xmlCleanupParser();
	curl_global_cleanup();
	arenarelease(arena);
	return 0;
}
