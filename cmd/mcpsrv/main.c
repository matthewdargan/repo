#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* clang-format off */
#include "libu/u.h"
#include "libu/arena.h"
#include "libu/string.h"
#include "libu/os.h"
#include "libu/json.h"
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/os.c"
#include "libu/json.c"
#include <tree_sitter/api.h>
#include "parser_c.c"
/* clang-format on */

extern const TSLanguage *tree_sitter_c(void);

typedef struct Symbol Symbol;
struct Symbol
{
	String8 name;
	String8 type;
	String8 file;
	u32 line;
	String8 signature;
};

typedef struct Symbolnode Symbolnode;
struct Symbolnode
{
	Symbol symbol;
	Symbolnode *next;
};

typedef struct Symbollist Symbollist;
struct Symbollist
{
	Symbolnode *start;
	Symbolnode *end;
	u64 count;
};

readonly static String8 directories[] = {str8litc("libu"), str8litc("lib9p"), str8litc("cmd")};

static String8
cleansignature(Arena *a, String8 signature)
{
	if (signature.len == 0)
	{
		return signature;
	}
	u64 newlinepos = str8index(signature, 0, str8lit("\n"), 0);
	String8 cleaned = (newlinepos < signature.len) ? str8prefix(signature, newlinepos) : signature;
	return pushstr8cpy(a, cleaned);
}

static String8
extractfunctionsignature(Arena *a, String8 source, TSNode node)
{
	u32 startbyte = ts_node_start_byte(node);
	u32 endbyte = ts_node_end_byte(node);
	if (startbyte >= source.len || endbyte > source.len || endbyte <= startbyte)
	{
		return str8zero();
	}
	String8 signature = str8substr(source, rng1u64(startbyte, endbyte));
	b32 foundopenbrace = 0;
	u64 i = 0;
	for (; i < signature.len; i++)
	{
		if (signature.str[i] == '{')
		{
			foundopenbrace = 1;
			break;
		}
		if (signature.str[i] == ';')
		{
			break;
		}
	}
	if (foundopenbrace && i < signature.len)
	{
		signature = str8prefix(signature, i);
	}
	return cleansignature(a, signature);
}

static b32
symbolexists(Symbollist *list, Symbol symbol)
{
	for (Symbolnode *node = list->start; node != NULL; node = node->next)
	{
		if (str8cmp(node->symbol.name, symbol.name, 0) && str8cmp(node->symbol.type, symbol.type, 0) &&
		    str8cmp(node->symbol.file, symbol.file, 0) && node->symbol.line == symbol.line)
		{
			return 1;
		}
	}
	return 0;
}

static void
symbollistpush(Arena *a, Symbollist *list, Symbol symbol)
{
	if (symbolexists(list, symbol))
	{
		return;
	}
	Symbolnode *node = pusharr(a, Symbolnode, 1);
	*node = (Symbolnode){.symbol = symbol, .next = NULL};
	if (list->end == NULL)
	{
		list->start = node;
		list->end = node;
	}
	else
	{
		list->end->next = node;
		list->end = node;
	}
	list->count++;
}

static String8array
listcfiles(Arena *a, String8 dirpath)
{
	String8array result = {0};
	String8list files = {0};
	String8 dirpathcpy = pushstr8cpy(a, dirpath);
	DIR *dp = opendir((char *)dirpathcpy.str);
	if (dp == NULL)
	{
		return result;
	}
	struct dirent *entry = NULL;
	while ((entry = readdir(dp)) != NULL)
	{
		if (entry->d_type != DT_REG)
		{
			continue;
		}
		String8 name = str8cstr(entry->d_name);
		String8 ext = str8ext(name);
		if (!str8cmp(ext, str8lit(".c"), 0) || !str8cmp(ext, str8lit(".h"), 0))
		{
			String8 fullpath = pushstr8f(a, "%.*s/%.*s", str8varg(dirpath), str8varg(name));
			str8listpush(a, &files, fullpath);
		}
	}
	closedir(dp);
	if (files.nnode == 0)
	{
		return result;
	}
	result.v = pusharr(a, String8, files.nnode);
	u64 i = 0;
	for (String8node *node = files.start; node != NULL; node = node->next)
	{
		result.v[i++] = node->str;
	}
	result.cnt = files.nnode;
	return result;
}

static void
symbolresult(Arena *a, Jsonbuilder *b, Symbollist *symbols)
{
	jsonbobjstart(b);
	jsonbobjkey(b, str8lit("content"));
	jsonbarrstart(b);

	jsonbobjstart(b);
	jsonbobjkey(b, str8lit("type"));
	jsonbwritestr(b, str8lit("text"));
	jsonbobjcomma(b);
	jsonbobjkey(b, str8lit("text"));

	u64 estsize = 1024 + symbols->count * 200;
	Jsonbuilder textbuilder = jsonbuilder(a, estsize);
	jsonbobjstart(&textbuilder);
	jsonbobjkey(&textbuilder, str8lit("symbols"));
	jsonbarrstart(&textbuilder);

	for (Symbolnode *node = symbols->start; node != NULL; node = node->next)
	{
		Symbol *sym = &node->symbol;
		if (node != symbols->start)
		{
			jsonbarrcomma(&textbuilder);
		}
		jsonbobjstart(&textbuilder);
		jsonbobjkey(&textbuilder, str8lit("name"));
		jsonbwritestr(&textbuilder, sym->name);
		jsonbobjcomma(&textbuilder);
		jsonbobjkey(&textbuilder, str8lit("type"));
		jsonbwritestr(&textbuilder, sym->type);
		jsonbobjcomma(&textbuilder);
		jsonbobjkey(&textbuilder, str8lit("file"));
		jsonbwritestr(&textbuilder, sym->file);
		jsonbobjcomma(&textbuilder);
		jsonbobjkey(&textbuilder, str8lit("line"));
		jsonbwritenum(&textbuilder, sym->line);
		jsonbobjcomma(&textbuilder);
		jsonbobjkey(&textbuilder, str8lit("signature"));
		jsonbwritestr(&textbuilder, sym->signature);
		jsonbobjend(&textbuilder);
	}

	jsonbarrend(&textbuilder);
	jsonbobjend(&textbuilder);
	String8 textjson = jsonbfinish(&textbuilder);
	jsonbwritestr(b, textjson);

	jsonbobjend(b);
	jsonbarrend(b);
	jsonbobjend(b);
}

static void
printmcpresponse(Arena *a, String8 requestid, String8 result)
{
	u64 estsize = 100 + requestid.len + result.len;
	Jsonbuilder b = jsonbuilder(a, estsize);
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("jsonrpc"));
	jsonbwritestr(&b, str8lit("2.0"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("id"));
	if (requestid.len > 0)
	{
		jsonbwritestr(&b, requestid);
	}
	else
	{
		jsonbwritenull(&b);
	}
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("result"));
	jsonbwrite(&b, result);
	jsonbobjend(&b);

	String8 output = jsonbfinish(&b);
	printf("%.*s\n", str8varg(output));
	fflush(stdout);
}

static void
printmcperror(Arena *a, String8 requestid, s32 code, String8 message)
{
	u64 estsize = 150 + requestid.len + message.len;
	Jsonbuilder b = jsonbuilder(a, estsize);
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("jsonrpc"));
	jsonbwritestr(&b, str8lit("2.0"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("id"));
	if (requestid.len > 0)
	{
		jsonbwritestr(&b, requestid);
	}
	else
	{
		jsonbwritenull(&b);
	}
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("error"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("code"));
	jsonbwritenum(&b, code);
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8lit("message"));
	jsonbwritestr(&b, message);
	jsonbobjend(&b);
	jsonbobjend(&b);

	String8 output = jsonbfinish(&b);
	printf("%.*s\n", str8varg(output));
	fflush(stdout);
}

static void
extractsymbolsfromnode(Arena *a, Symbollist *symbols, TSNode node, String8 filepath, String8 source)
{
	TSSymbol symboltype = ts_node_symbol(node);
	const char *typename = ts_language_symbol_name(tree_sitter_c(), symboltype);
	if (strcmp(typename, "function_definition") == 0 || strcmp(typename, "declaration") == 0)
	{
		u32 childcount = ts_node_child_count(node);
		for (u32 i = 0; i < childcount; i++)
		{
			TSNode child = ts_node_child(node, i);
			TSSymbol childsymbol = ts_node_symbol(child);
			const char *childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "function_declarator") == 0 || strcmp(childtype, "identifier") == 0)
			{
				if (strcmp(childtype, "function_declarator") == 0)
				{
					TSNode declaratorchild = ts_node_child(child, 0);
					if (ts_node_child_count(child) > 0)
					{
						child = declaratorchild;
					}
				}
				TSPoint startpoint = ts_node_start_point(child);
				TSPoint endpoint = ts_node_end_point(child);
				u32 startline = startpoint.row;
				u32 endline = endpoint.row;
				if (startline < source.len && endline < source.len)
				{
					u32 startbyte = ts_node_start_byte(child);
					u32 endbyte = ts_node_end_byte(child);
					if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
					{
						String8 identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
						if (identifiertext.len > 0)
						{
							Symbol symbol = {.name = pushstr8cpy(a, identifiertext),
							                 .type = str8lit("function"),
							                 .file = pushstr8cpy(a, filepath),
							                 .line = startline + 1,
							                 .signature = extractfunctionsignature(a, source, node)};
							symbollistpush(a, symbols, symbol);
						}
					}
				}
				break;
			}
		}
	}
	else if (strcmp(typename, "struct_specifier") == 0)
	{
		u32 childcount = ts_node_child_count(node);
		for (u32 i = 0; i < childcount; i++)
		{
			TSNode structchild = ts_node_child(node, i);
			TSSymbol childsymbol = ts_node_symbol(structchild);
			const char *childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "type_identifier") == 0)
			{
				TSPoint startpoint = ts_node_start_point(structchild);
				u32 startbyte = ts_node_start_byte(structchild);
				u32 endbyte = ts_node_end_byte(structchild);
				if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
				{
					String8 identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.len > 0)
					{
						Symbol symbol = {0};
						symbol.name = pushstr8cpy(a, identifiertext);
						symbol.type = str8lit("struct");
						symbol.file = pushstr8cpy(a, filepath);
						symbol.line = startpoint.row + 1;
						u32 startbyte = ts_node_start_byte(node);
						u32 endbyte = ts_node_end_byte(node);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
						{
							String8 nodetext = str8substr(source, rng1u64(startbyte, endbyte));
							String8list lines = str8split(a, nodetext, (u8 *)"\n", 1, 0);
							if (lines.start != NULL)
							{
								symbol.signature = pushstr8cpy(a, lines.start->str);
							}
						}
						symbollistpush(a, symbols, symbol);
					}
				}
				break;
			}
		}
	}
	else if (strcmp(typename, "enum_specifier") == 0)
	{
		u32 childcount = ts_node_child_count(node);
		for (u32 i = 0; i < childcount; i++)
		{
			TSNode enumchild = ts_node_child(node, i);
			TSSymbol childsymbol = ts_node_symbol(enumchild);
			const char *childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "type_identifier") == 0)
			{
				TSPoint startpoint = ts_node_start_point(enumchild);
				u32 startbyte = ts_node_start_byte(enumchild);
				u32 endbyte = ts_node_end_byte(enumchild);
				if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
				{
					String8 identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.len > 0)
					{
						Symbol symbol = {0};
						symbol.name = pushstr8cpy(a, identifiertext);
						symbol.type = str8lit("enum");
						symbol.file = pushstr8cpy(a, filepath);
						symbol.line = startpoint.row + 1;
						u32 startbyte = ts_node_start_byte(node);
						u32 endbyte = ts_node_end_byte(node);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
						{
							String8 nodetext = str8substr(source, rng1u64(startbyte, endbyte));
							String8list lines = str8split(a, nodetext, (u8 *)"\n", 1, 0);
							if (lines.start != NULL)
							{
								symbol.signature = pushstr8cpy(a, lines.start->str);
							}
						}
						symbollistpush(a, symbols, symbol);
					}
				}
				break;
			}
			else if (strcmp(childtype, "enumerator_list") == 0)
			{
				u32 enumlistcount = ts_node_child_count(enumchild);
				for (u32 j = 0; j < enumlistcount; j++)
				{
					TSNode enumlistchild = ts_node_child(enumchild, j);
					TSSymbol enumlistsymbol = ts_node_symbol(enumlistchild);
					const char *enumlisttype = ts_language_symbol_name(tree_sitter_c(), enumlistsymbol);
					if (strcmp(enumlisttype, "enumerator") == 0)
					{
						TSNode enumnamechild = ts_node_child(enumlistchild, 0);
						TSPoint startpoint = ts_node_start_point(enumnamechild);
						u32 startbyte = ts_node_start_byte(enumnamechild);
						u32 endbyte = ts_node_end_byte(enumnamechild);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
						{
							String8 identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
							if (identifiertext.len > 0)
							{
								Symbol symbol = {0};
								symbol.name = pushstr8cpy(a, identifiertext);
								symbol.type = str8lit("enum");
								symbol.file = pushstr8cpy(a, filepath);
								symbol.line = startpoint.row + 1;
								u32 startbyte = ts_node_start_byte(enumlistchild);
								u32 endbyte = ts_node_end_byte(enumlistchild);
								if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
								{
									String8 nodetext = str8substr(source, rng1u64(startbyte, endbyte));
									symbol.signature = cleansignature(a, nodetext);
								}
								symbollistpush(a, symbols, symbol);
							}
						}
					}
				}
			}
		}
	}
	else if (strcmp(typename, "type_definition") == 0)
	{
		u32 childcount = ts_node_child_count(node);
		for (u32 i = 0; i < childcount; i++)
		{
			TSNode typedefchild = ts_node_child(node, i);
			TSSymbol childsymbol = ts_node_symbol(typedefchild);
			const char *childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "type_identifier") == 0)
			{
				TSPoint startpoint = ts_node_start_point(typedefchild);
				u32 startbyte = ts_node_start_byte(typedefchild);
				u32 endbyte = ts_node_end_byte(typedefchild);
				if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
				{
					String8 identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.len > 0)
					{
						Symbol symbol = {0};
						symbol.name = pushstr8cpy(a, identifiertext);
						symbol.type = str8lit("typedef");
						symbol.file = pushstr8cpy(a, filepath);
						symbol.line = startpoint.row + 1;
						u32 startbyte = ts_node_start_byte(node);
						u32 endbyte = ts_node_end_byte(node);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
						{
							String8 nodetext = str8substr(source, rng1u64(startbyte, endbyte));
							String8list lines = str8split(a, nodetext, (u8 *)"\n", 1, 0);
							if (lines.start != NULL)
							{
								symbol.signature = pushstr8cpy(a, lines.start->str);
							}
						}
						symbollistpush(a, symbols, symbol);
					}
				}
				break;
			}
		}
	}
	else if (strcmp(typename, "preproc_def") == 0)
	{
		u32 childcount = ts_node_child_count(node);
		for (u32 i = 0; i < childcount; i++)
		{
			TSNode child = ts_node_child(node, i);
			TSSymbol childsymbol = ts_node_symbol(child);
			const char *childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "identifier") == 0)
			{
				TSPoint startpoint = ts_node_start_point(child);
				u32 startbyte = ts_node_start_byte(child);
				u32 endbyte = ts_node_end_byte(child);
				if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
				{
					String8 identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.len > 0)
					{
						Symbol symbol = {0};
						symbol.name = pushstr8cpy(a, identifiertext);
						symbol.type = str8lit("macro");
						symbol.file = pushstr8cpy(a, filepath);
						symbol.line = startpoint.row + 1;
						u32 startbyte = ts_node_start_byte(node);
						u32 endbyte = ts_node_end_byte(node);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte)
						{
							String8 nodetext = str8substr(source, rng1u64(startbyte, endbyte));
							symbol.signature = cleansignature(a, nodetext);
						}
						symbollistpush(a, symbols, symbol);
					}
				}
				break;
			}
		}
	}
	u32 childcount = ts_node_child_count(node);
	for (u32 i = 0; i < childcount; i++)
	{
		TSNode child = ts_node_child(node, i);
		extractsymbolsfromnode(a, symbols, child, filepath, source);
	}
}

static Symbollist
parsecfiletreesitter(Arena *a, String8 filepath, String8 source)
{
	Symbollist symbols = {0};
	TSParser *parser = ts_parser_new();
	if (!ts_parser_set_language(parser, tree_sitter_c()))
	{
		fprintf(stderr, "mcpsrv: failed to set tree-sitter language for C\n");
		ts_parser_delete(parser);
		return symbols;
	}
	char *sourcecstr = (char *)pusharr(a, u8, source.len + 1);
	memcpy(sourcecstr, source.str, source.len);
	sourcecstr[source.len] = '\0';
	TSTree *tree = ts_parser_parse_string(parser, NULL, sourcecstr, source.len);
	if (tree == NULL)
	{
		fprintf(stderr, "mcpsrv: failed to parse source file '%.*s'\n", str8varg(filepath));
		ts_parser_delete(parser);
		return symbols;
	}
	TSNode rootnode = ts_tree_root_node(tree);
	extractsymbolsfromnode(a, &symbols, rootnode, filepath, source);
	ts_tree_delete(tree);
	ts_parser_delete(parser);
	return symbols;
}

static String8
symbolsearch(Arena *a, String8 pattern)
{
	Symbollist allsymbols = {0};
	Symbollist filtered = {0};
	for (u64 d = 0; d < nelem(directories); d++)
	{
		String8array files = listcfiles(a, directories[d]);
		for (u64 i = 0; i < files.cnt; i++)
		{
			String8 source = readfile(a, files.v[i]);
			if (source.len > 0 && source.str != NULL)
			{
				Symbollist filesymbols = parsecfiletreesitter(a, files.v[i], source);
				if (filesymbols.count == 0)
				{
					fprintf(stderr, "mcpsrv: failed to parse symbols from '%.*s' using tree-sitter\n", str8varg(files.v[i]));
				}
				else
				{
					for (Symbolnode *symnode = filesymbols.start; symnode != NULL; symnode = symnode->next)
					{
						symbollistpush(a, &allsymbols, symnode->symbol);
					}
				}
			}
		}
	}
	for (Symbolnode *node = allsymbols.start; node != NULL; node = node->next)
	{
		Symbol *sym = &node->symbol;
		if (pattern.len == 0 ||
		    (sym->name.len >= pattern.len && str8index(sym->name, 0, pattern, CASEINSENSITIVE) < sym->name.len))
		{
			symbollistpush(a, &filtered, *sym);
		}
	}
	u64 estsize = 1024 + filtered.count * 200;
	Jsonbuilder b = jsonbuilder(a, estsize);
	symbolresult(a, &b, &filtered);
	return jsonbfinish(&b);
}

static void
initialize(Arena *a, String8 requestid)
{
	Jsonbuilder b = jsonbuilder(a, 512);
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("protocolVersion"));
	jsonbwritestr(&b, str8lit("2024-11-05"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("capabilities"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("tools"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("listChanged"));
	jsonbwritebool(&b, 0);
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("serverInfo"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("name"));
	jsonbwritestr(&b, str8lit("mcpsrv"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8lit("version"));
	jsonbwritestr(&b, str8lit("0.1.0"));
	jsonbobjend(&b);
	jsonbobjend(&b);

	String8 result = jsonbfinish(&b);
	printmcpresponse(a, requestid, result);
}

static void
toolslist(Arena *a, String8 requestid)
{
	Jsonbuilder b = jsonbuilder(a, 2048);
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("tools"));
	jsonbarrstart(&b);

	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("name"));
	jsonbwritestr(&b, str8lit("symbol_search"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8lit("description"));
	jsonbwritestr(&b, str8lit("Search for symbols in C codebase"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("inputSchema"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("type"));
	jsonbwritestr(&b, str8lit("object"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("properties"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("pattern"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("type"));
	jsonbwritestr(&b, str8lit("string"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8lit("description"));
	jsonbwritestr(&b, str8lit("Search pattern for symbol names"));
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("required"));
	jsonbarrstart(&b);
	jsonbarrend(&b);
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbarrcomma(&b);

	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("name"));
	jsonbwritestr(&b, str8lit("symbol_info"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8lit("description"));
	jsonbwritestr(&b, str8lit("Get detailed information about a specific symbol"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("inputSchema"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("type"));
	jsonbwritestr(&b, str8lit("object"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("properties"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("name"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("type"));
	jsonbwritestr(&b, str8lit("string"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8lit("description"));
	jsonbwritestr(&b, str8lit("Symbol name to look up"));
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("required"));
	jsonbarrstart(&b);
	jsonbwritestr(&b, str8lit("name"));
	jsonbarrend(&b);
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbarrcomma(&b);

	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("name"));
	jsonbwritestr(&b, str8lit("file_symbols"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8lit("description"));
	jsonbwritestr(&b, str8lit("List all symbols in a specific file"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("inputSchema"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("type"));
	jsonbwritestr(&b, str8lit("object"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("properties"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("file"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8lit("type"));
	jsonbwritestr(&b, str8lit("string"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8lit("description"));
	jsonbwritestr(&b, str8lit("File path to analyze"));
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8lit("required"));
	jsonbarrstart(&b);
	jsonbwritestr(&b, str8lit("file"));
	jsonbarrend(&b);
	jsonbobjend(&b);
	jsonbobjend(&b);

	jsonbarrend(&b);
	jsonbobjend(&b);
	String8 result = jsonbfinish(&b);
	printmcpresponse(a, requestid, result);
}

static String8
symbolinfo(Arena *a, String8 symbolname)
{
	Symbollist allsymbols = {0};
	Symbollist filtered = {0};
	for (u64 d = 0; d < nelem(directories); d++)
	{
		String8array files = listcfiles(a, directories[d]);
		for (u64 i = 0; i < files.cnt; i++)
		{
			String8 source = readfile(a, files.v[i]);
			if (source.len > 0 && source.str != NULL)
			{
				Symbollist filesymbols = parsecfiletreesitter(a, files.v[i], source);
				if (filesymbols.count > 0)
				{
					for (Symbolnode *symnode = filesymbols.start; symnode != NULL; symnode = symnode->next)
					{
						symbollistpush(a, &allsymbols, symnode->symbol);
					}
				}
			}
		}
	}
	for (Symbolnode *node = allsymbols.start; node != NULL; node = node->next)
	{
		Symbol *sym = &node->symbol;
		if (str8cmp(sym->name, symbolname, 0))
		{
			symbollistpush(a, &filtered, *sym);
		}
	}
	u64 estsize = 1024 + filtered.count * 200;
	Jsonbuilder b = jsonbuilder(a, estsize);
	symbolresult(a, &b, &filtered);
	return jsonbfinish(&b);
}

static String8
filesymbols(Arena *a, String8 filepath)
{
	Symbollist symbols = {0};
	String8 source = readfile(a, filepath);
	if (source.len == 0 || source.str == NULL)
	{
		Jsonbuilder b = jsonbuilder(a, 512);
		jsonbobjstart(&b);
		jsonbobjkey(&b, str8lit("content"));
		jsonbarrstart(&b);

		jsonbobjstart(&b);
		jsonbobjkey(&b, str8lit("type"));
		jsonbwritestr(&b, str8lit("text"));
		jsonbobjcomma(&b);
		jsonbobjkey(&b, str8lit("text"));
		jsonbwritec(&b, '"');
		jsonbwrite(&b, str8lit("{\\\"error\\\":\\\"Failed to read file '"));
		jsonbwrite(&b, filepath);
		jsonbwrite(&b, str8lit("'\\\"}"));
		jsonbwritec(&b, '"');
		jsonbobjend(&b);

		jsonbarrend(&b);
		jsonbobjend(&b);
		return jsonbfinish(&b);
	}
	symbols = parsecfiletreesitter(a, filepath, source);
	u64 estsize = 1024 + symbols.count * 200;
	Jsonbuilder b = jsonbuilder(a, estsize);
	symbolresult(a, &b, &symbols);
	return jsonbfinish(&b);
}

static void
toolcall(Arena *a, String8 requestid, String8 toolname, String8 arguments)
{
	Jsonvalue argsobj = jsonparse(a, arguments);
	if (str8cmp(toolname, str8lit("symbol_search"), 0))
	{
		String8 pattern = str8zero();
		if (argsobj.type == JSON_OBJECT)
		{
			Jsonvalue patternval = jsonget(argsobj, str8lit("pattern"));
			if (patternval.type == JSON_STRING)
			{
				pattern = patternval.string;
			}
		}
		String8 result = symbolsearch(a, pattern);
		printmcpresponse(a, requestid, result);
	}
	else if (str8cmp(toolname, str8lit("symbol_info"), 0))
	{
		String8 symbolname = str8zero();
		if (argsobj.type == JSON_OBJECT)
		{
			Jsonvalue nameval = jsonget(argsobj, str8lit("name"));
			if (nameval.type == JSON_STRING)
			{
				symbolname = nameval.string;
			}
		}
		String8 result = symbolinfo(a, symbolname);
		printmcpresponse(a, requestid, result);
	}
	else if (str8cmp(toolname, str8lit("file_symbols"), 0))
	{
		String8 filepath = str8zero();
		if (argsobj.type == JSON_OBJECT)
		{
			Jsonvalue fileval = jsonget(argsobj, str8lit("file"));
			if (fileval.type == JSON_STRING)
			{
				filepath = fileval.string;
			}
		}
		String8 result = filesymbols(a, filepath);
		printmcpresponse(a, requestid, result);
	}
	else
	{
		printmcperror(a, requestid, -32601, pushstr8f(a, "Unknown tool: %.*s", str8varg(toolname)));
	}
}

static void
mcprequest(Arena *a, String8 line)
{
	String8 requestid = str8zero();
	u64 idstart = str8index(line, 0, str8lit("\"id\":"), 0);
	if (idstart < line.len)
	{
		idstart += 5;
		if (idstart < line.len)
		{
			if (line.str[idstart] == '"')
			{
				idstart++;
				u64 idend = str8index(line, idstart, str8lit("\""), 0);
				if (idend < line.len)
				{
					requestid = str8substr(line, rng1u64(idstart, idend));
				}
			}
			else
			{
				u64 idend = idstart;
				while (idend < line.len && (line.str[idend] >= '0' && line.str[idend] <= '9'))
				{
					idend++;
				}
				if (idend > idstart)
				{
					requestid = str8substr(line, rng1u64(idstart, idend));
				}
			}
		}
	}
	if (str8index(line, 0, str8lit("\"method\":\"initialize\""), 0) < line.len)
	{
		initialize(a, requestid);
	}
	else if (str8index(line, 0, str8lit("\"method\":\"tools/list\""), 0) < line.len)
	{
		toolslist(a, requestid);
	}
	else if (str8index(line, 0, str8lit("\"method\":\"tools/call\""), 0) < line.len)
	{
		u64 namestart = str8index(line, 0, str8lit("\"name\":\""), 0);
		if (namestart < line.len)
		{
			namestart += 8;
			u64 nameend = str8index(line, namestart, str8lit("\""), 0);
			if (nameend < line.len)
			{
				String8 toolname = str8substr(line, rng1u64(namestart, nameend));
				u64 argsstart = str8index(line, 0, str8lit("\"arguments\":{"), 0);
				String8 arguments = str8zero();
				if (argsstart < line.len)
				{
					argsstart += 12;
					u64 bracecount = 1;
					u64 argsend = argsstart;
					while (argsend < line.len && bracecount > 0)
					{
						if (line.str[argsend] == '{')
						{
							bracecount++;
						}
						else if (line.str[argsend] == '}')
						{
							bracecount--;
						}
						argsend++;
					}
					if (bracecount == 0)
					{
						if (argsend > argsstart)
						{
							arguments = str8substr(line, rng1u64(argsstart, argsend - 1));
						}
					}
				}
				toolcall(a, requestid, toolname, arguments);
			}
		}
	}
}

int
main(void)
{
	sysinfo = (Sysinfo){.nprocs = sysconf(_SC_NPROCESSORS_ONLN), .pagesz = sysconf(_SC_PAGESIZE), .lpagesz = 0x200000};
	Arenaparams ap = {.flags = arenaflags, .ressz = arenaressz, .cmtsz = arenacmtsz};
	Arena *arena = arenaalloc(ap);
	char line[8192] = {0};
	while (fgets(line, sizeof(line), stdin))
	{
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
		{
			line[len - 1] = '\0';
			len--;
		}
		String8 input = str8((u8 *)line, len);
		Temp temp = tempbegin(arena);
		mcprequest(arena, input);
		tempend(temp);
	}
	arenarelease(arena);
	return 0;
}
