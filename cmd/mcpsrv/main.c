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
#include "libu/u.c"
#include "libu/arena.c"
#include "libu/string.c"
#include "libu/os.c"
#include <tree_sitter/api.h>
#include "parser_c.c"
/* clang-format on */

extern const TSLanguage *tree_sitter_c(void);

typedef struct Symbol Symbol;
struct Symbol {
	String8 name;
	String8 type;
	String8 file;
	u32 line;
	String8 signature;
};

typedef struct Symbolnode Symbolnode;
struct Symbolnode {
	Symbol symbol;
	Symbolnode *next;
};

typedef struct Symbollist Symbollist;
struct Symbollist {
	Symbolnode *start;
	Symbolnode *end;
	u64 count;
};

readonly static String8 directories[] = {str8litc("libu"), str8litc("lib9p"), str8litc("cmd")};

static String8
cleansignature(Arena *a, String8 signature)
{
	String8 cleaned;
	u64 newlinepos;

	if (signature.len == 0)
		return signature;
	newlinepos = str8index(signature, 0, str8lit("\n"), 0);
	if (newlinepos < signature.len)
		cleaned = str8prefix(signature, newlinepos);
	else
		cleaned = signature;
	return pushstr8cpy(a, cleaned);
}

static String8
extractfunctionsignature(Arena *a, String8 source, TSNode node)
{
	String8 signature;
	String8list lines;
	String8node *linenode;
	u32 startbyte, endbyte;
	u64 bracecount, i;
	b32 foundopenbrace;

	startbyte = ts_node_start_byte(node);
	endbyte = ts_node_end_byte(node);
	if (startbyte >= source.len || endbyte > source.len || endbyte <= startbyte)
		return str8zero();
	signature = str8substr(source, rng1u64(startbyte, endbyte));
	lines = str8split(a, signature, (u8 *)"\n", 1, 0);
	foundopenbrace = 0;
	bracecount = 0;
	for (i = 0; i < signature.len; i++) {
		if (signature.str[i] == '{') {
			foundopenbrace = 1;
			break;
		}
		if (signature.str[i] == ';')
			break;
	}
	if (foundopenbrace && i < signature.len)
		signature = str8prefix(signature, i);
	return cleansignature(a, signature);
}

static b32
symbolexists(Symbollist *list, Symbol symbol)
{
	Symbolnode *node;

	for (node = list->start; node != NULL; node = node->next) {
		if (str8cmp(node->symbol.name, symbol.name, 0) && str8cmp(node->symbol.type, symbol.type, 0) &&
		    str8cmp(node->symbol.file, symbol.file, 0) && node->symbol.line == symbol.line)
			return 1;
	}
	return 0;
}

static void
symbollistpush(Arena *a, Symbollist *list, Symbol symbol)
{
	Symbolnode *node;

	if (symbolexists(list, symbol))
		return;
	node = pusharr(a, Symbolnode, 1);
	node->symbol = symbol;
	node->next = NULL;
	if (list->end == NULL) {
		list->start = node;
		list->end = node;
	} else {
		list->end->next = node;
		list->end = node;
	}
	list->count++;
}

static String8array
listcfiles(Arena *a, String8 dirpath)
{
	String8array result;
	String8list files;
	String8node *node;
	String8 dirpathcpy, name, ext, fullpath;
	DIR *dp;
	struct dirent *entry;
	u64 i;

	memset(&result, 0, sizeof(result));
	memset(&files, 0, sizeof(files));
	dirpathcpy = pushstr8cpy(a, dirpath);
	dp = opendir((char *)dirpathcpy.str);
	if (dp == NULL)
		return result;
	while ((entry = readdir(dp)) != NULL) {
		if (entry->d_type != DT_REG)
			continue;
		name = str8cstr(entry->d_name);
		ext = str8ext(name);
		if (!str8cmp(ext, str8lit(".c"), 0) || !str8cmp(ext, str8lit(".h"), 0)) {
			fullpath = pushstr8f(a, "%.*s/%.*s", str8varg(dirpath), str8varg(name));
			str8listpush(a, &files, fullpath);
		}
	}
	closedir(dp);
	if (files.nnode == 0)
		return result;
	result.v = pusharr(a, String8, files.nnode);
	i = 0;
	for (node = files.start; node != NULL; node = node->next)
		result.v[i++] = node->str;
	result.cnt = files.nnode;
	return result;
}

static void
printjsonescaped(String8 s)
{
	u64 i;
	u8 c;

	for (i = 0; i < s.len; i++) {
		c = s.str[i];
		switch (c) {
			case '"':
				printf("\\\"");
				break;
			case '\\':
				printf("\\\\");
				break;
			case '\n':
				printf("\\n");
				break;
			case '\r':
				printf("\\r");
				break;
			case '\t':
				printf("\\t");
				break;
			default:
				if (c >= 32 && c <= 126)
					putchar(c);
				else
					printf("\\u%04x", c);
				break;
		}
	}
}

static void
extractsymbolsfromnode(Arena *a, Symbollist *symbols, TSNode node, String8 filepath, String8 source)
{
	Symbol symbol;
	TSNode child, declaratorchild, structchild, enumchild, typedefchild, enumlistchild, enumnamechild;
	TSSymbol symboltype, childsymbol, enumlistsymbol;
	TSPoint startpoint, endpoint;
	String8 nodetext, identifiertext;
	String8list lines;
	const char *typename, *childtype, *enumlisttype;
	u32 childcount, i, startline, endline, startbyte, endbyte;
	u32 enumlistcount, j;

	symboltype = ts_node_symbol(node);
	typename = ts_language_symbol_name(tree_sitter_c(), symboltype);
	if (strcmp(typename, "function_definition") == 0 || strcmp(typename, "declaration") == 0) {
		childcount = ts_node_child_count(node);
		for (i = 0; i < childcount; i++) {
			child = ts_node_child(node, i);
			childsymbol = ts_node_symbol(child);
			childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "function_declarator") == 0 || strcmp(childtype, "identifier") == 0) {
				if (strcmp(childtype, "function_declarator") == 0) {
					declaratorchild = ts_node_child(child, 0);
					if (ts_node_child_count(child) > 0)
						child = declaratorchild;
				}
				startpoint = ts_node_start_point(child);
				endpoint = ts_node_end_point(child);
				startline = startpoint.row;
				endline = endpoint.row;
				if (startline < source.len && endline < source.len) {
					startbyte = ts_node_start_byte(child);
					endbyte = ts_node_end_byte(child);
					if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
						identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
						if (identifiertext.len > 0) {
							memset(&symbol, 0, sizeof(symbol));
							symbol.name = pushstr8cpy(a, identifiertext);
							symbol.type = str8lit("function");
							symbol.file = pushstr8cpy(a, filepath);
							symbol.line = startline + 1;
							symbol.signature = extractfunctionsignature(a, source, node);
							symbollistpush(a, symbols, symbol);
						}
					}
				}
				break;
			}
		}
	} else if (strcmp(typename, "struct_specifier") == 0) {
		childcount = ts_node_child_count(node);
		for (i = 0; i < childcount; i++) {
			structchild = ts_node_child(node, i);
			childsymbol = ts_node_symbol(structchild);
			childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "type_identifier") == 0) {
				startpoint = ts_node_start_point(structchild);
				startbyte = ts_node_start_byte(structchild);
				endbyte = ts_node_end_byte(structchild);
				if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
					identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.len > 0) {
						memset(&symbol, 0, sizeof(symbol));
						symbol.name = pushstr8cpy(a, identifiertext);
						symbol.type = str8lit("struct");
						symbol.file = pushstr8cpy(a, filepath);
						symbol.line = startpoint.row + 1;
						startbyte = ts_node_start_byte(node);
						endbyte = ts_node_end_byte(node);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
							nodetext = str8substr(source, rng1u64(startbyte, endbyte));
							lines = str8split(a, nodetext, (u8 *)"\n", 1, 0);
							if (lines.start != NULL)
								symbol.signature = pushstr8cpy(a, lines.start->str);
						}
						symbollistpush(a, symbols, symbol);
					}
				}
				break;
			}
		}
	} else if (strcmp(typename, "enum_specifier") == 0) {
		childcount = ts_node_child_count(node);
		for (i = 0; i < childcount; i++) {
			enumchild = ts_node_child(node, i);
			childsymbol = ts_node_symbol(enumchild);
			childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "type_identifier") == 0) {
				startpoint = ts_node_start_point(enumchild);
				startbyte = ts_node_start_byte(enumchild);
				endbyte = ts_node_end_byte(enumchild);
				if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
					identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.len > 0) {
						memset(&symbol, 0, sizeof(symbol));
						symbol.name = pushstr8cpy(a, identifiertext);
						symbol.type = str8lit("enum");
						symbol.file = pushstr8cpy(a, filepath);
						symbol.line = startpoint.row + 1;
						startbyte = ts_node_start_byte(node);
						endbyte = ts_node_end_byte(node);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
							nodetext = str8substr(source, rng1u64(startbyte, endbyte));
							lines = str8split(a, nodetext, (u8 *)"\n", 1, 0);
							if (lines.start != NULL)
								symbol.signature = pushstr8cpy(a, lines.start->str);
						}
						symbollistpush(a, symbols, symbol);
					}
				}
				break;
			} else if (strcmp(childtype, "enumerator_list") == 0) {
				enumlistcount = ts_node_child_count(enumchild);
				for (j = 0; j < enumlistcount; j++) {
					enumlistchild = ts_node_child(enumchild, j);
					enumlistsymbol = ts_node_symbol(enumlistchild);
					enumlisttype = ts_language_symbol_name(tree_sitter_c(), enumlistsymbol);
					if (strcmp(enumlisttype, "enumerator") == 0) {
						enumnamechild = ts_node_child(enumlistchild, 0);
						startpoint = ts_node_start_point(enumnamechild);
						startbyte = ts_node_start_byte(enumnamechild);
						endbyte = ts_node_end_byte(enumnamechild);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
							identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
							if (identifiertext.len > 0) {
								memset(&symbol, 0, sizeof(symbol));
								symbol.name = pushstr8cpy(a, identifiertext);
								symbol.type = str8lit("enum");
								symbol.file = pushstr8cpy(a, filepath);
								symbol.line = startpoint.row + 1;
								startbyte = ts_node_start_byte(enumlistchild);
								endbyte = ts_node_end_byte(enumlistchild);
								if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
									nodetext = str8substr(source, rng1u64(startbyte, endbyte));
									symbol.signature = cleansignature(a, nodetext);
								}
								symbollistpush(a, symbols, symbol);
							}
						}
					}
				}
			}
		}
	} else if (strcmp(typename, "type_definition") == 0) {
		childcount = ts_node_child_count(node);
		for (i = 0; i < childcount; i++) {
			typedefchild = ts_node_child(node, i);
			childsymbol = ts_node_symbol(typedefchild);
			childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "type_identifier") == 0) {
				startpoint = ts_node_start_point(typedefchild);
				startbyte = ts_node_start_byte(typedefchild);
				endbyte = ts_node_end_byte(typedefchild);
				if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
					identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.len > 0) {
						memset(&symbol, 0, sizeof(symbol));
						symbol.name = pushstr8cpy(a, identifiertext);
						symbol.type = str8lit("typedef");
						symbol.file = pushstr8cpy(a, filepath);
						symbol.line = startpoint.row + 1;
						startbyte = ts_node_start_byte(node);
						endbyte = ts_node_end_byte(node);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
							nodetext = str8substr(source, rng1u64(startbyte, endbyte));
							lines = str8split(a, nodetext, (u8 *)"\n", 1, 0);
							if (lines.start != NULL)
								symbol.signature = pushstr8cpy(a, lines.start->str);
						}
						symbollistpush(a, symbols, symbol);
					}
				}
				break;
			}
		}
	} else if (strcmp(typename, "preproc_def") == 0) {
		childcount = ts_node_child_count(node);
		for (i = 0; i < childcount; i++) {
			child = ts_node_child(node, i);
			childsymbol = ts_node_symbol(child);
			childtype = ts_language_symbol_name(tree_sitter_c(), childsymbol);
			if (strcmp(childtype, "identifier") == 0) {
				startpoint = ts_node_start_point(child);
				startbyte = ts_node_start_byte(child);
				endbyte = ts_node_end_byte(child);
				if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
					identifiertext = str8substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.len > 0) {
						memset(&symbol, 0, sizeof(symbol));
						symbol.name = pushstr8cpy(a, identifiertext);
						symbol.type = str8lit("macro");
						symbol.file = pushstr8cpy(a, filepath);
						symbol.line = startpoint.row + 1;
						startbyte = ts_node_start_byte(node);
						endbyte = ts_node_end_byte(node);
						if (startbyte < source.len && endbyte <= source.len && endbyte > startbyte) {
							nodetext = str8substr(source, rng1u64(startbyte, endbyte));
							symbol.signature = cleansignature(a, nodetext);
						}
						symbollistpush(a, symbols, symbol);
					}
				}
				break;
			}
		}
	}
	childcount = ts_node_child_count(node);
	for (i = 0; i < childcount; i++) {
		child = ts_node_child(node, i);
		extractsymbolsfromnode(a, symbols, child, filepath, source);
	}
}

static Symbollist
parsecfiletreesitter(Arena *a, String8 filepath, String8 source)
{
	Symbollist symbols;
	TSParser *parser;
	TSTree *tree;
	TSNode rootnode;
	char *sourcecstr;

	memset(&symbols, 0, sizeof(symbols));
	parser = ts_parser_new();
	if (!ts_parser_set_language(parser, tree_sitter_c())) {
		fprintf(stderr, "mcpsrv: failed to set tree-sitter language for C\n");
		ts_parser_delete(parser);
		return symbols;
	}
	sourcecstr = (char *)pusharr(a, u8, source.len + 1);
	memcpy(sourcecstr, source.str, source.len);
	sourcecstr[source.len] = '\0';
	tree = ts_parser_parse_string(parser, NULL, sourcecstr, source.len);
	if (tree == NULL) {
		fprintf(stderr, "mcpsrv: failed to parse source file '%.*s'\n", str8varg(filepath));
		ts_parser_delete(parser);
		return symbols;
	}
	rootnode = ts_tree_root_node(tree);
	extractsymbolsfromnode(a, &symbols, rootnode, filepath, source);
	ts_tree_delete(tree);
	ts_parser_delete(parser);
	return symbols;
}

static void
handlesymbolsearch(Arena *a, String8 pattern)
{
	Symbollist allsymbols, filesymbols;
	Symbolnode *node, *symnode;
	Symbol *sym;
	String8array files;
	String8 source;
	u64 d, i, matches;

	memset(&allsymbols, 0, sizeof(allsymbols));
	for (d = 0; d < nelem(directories); d++) {
		files = listcfiles(a, directories[d]);
		for (i = 0; i < files.cnt; i++) {
			source = readfile(a, files.v[i]);
			if (source.len > 0 && source.str != NULL) {
				filesymbols = parsecfiletreesitter(a, files.v[i], source);
				if (filesymbols.count == 0)
					fprintf(stderr, "mcpsrv: failed to parse symbols from '%.*s' using tree-sitter\n",
					        str8varg(files.v[i]));
				else {
					for (symnode = filesymbols.start; symnode != NULL; symnode = symnode->next)
						symbollistpush(a, &allsymbols, symnode->symbol);
				}
			}
		}
	}
	printf("{\"content\":[{\"type\":\"text\",\"text\":\"{\\\"symbols\\\":[");
	matches = 0;
	for (node = allsymbols.start; node != NULL; node = node->next) {
		sym = &node->symbol;
		if (pattern.len == 0 ||
		    (sym->name.len >= pattern.len && str8index(sym->name, 0, pattern, CASEINSENSITIVE) < sym->name.len)) {
			if (matches > 0)
				printf(",");
			printf("{\\\"name\\\":\\\"");
			printjsonescaped(sym->name);
			printf("\\\",\\\"type\\\":\\\"");
			printjsonescaped(sym->type);
			printf("\\\",\\\"file\\\":\\\"");
			printjsonescaped(sym->file);
			printf("\\\",\\\"line\\\":%u,\\\"signature\\\":\\\"", sym->line);
			printjsonescaped(sym->signature);
			printf("\\\"}");
			matches++;
		}
	}
	printf("]}\"}]}");
}

static void
handleinitialize(String8 requestid)
{
	printf("{\"jsonrpc\":\"2.0\",\"id\":");
	if (requestid.len > 0)
		printjsonescaped(requestid);
	else
		printf("null");
	printf(
	    ",\"result\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{"
	    "\"listChanged\":false}},\"serverInfo\":{\"name\":\"mcpsrv\",\"version\":\"0.1.0\"}}}\n");
	fflush(stdout);
}

static void
handletoolslist(String8 requestid)
{
	printf("{\"jsonrpc\":\"2.0\",\"id\":");
	if (requestid.len > 0)
		printjsonescaped(requestid);
	else
		printf("null");
	printf(",\"result\":{\"tools\":[");
	printf(
	    "{\"name\":\"symbol_search\",\"description\":\"Search for symbols in C "
	    "codebase\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\","
	    "\"description\":\"Search pattern for symbol names\"}},\"required\":[]}},");
	printf(
	    "{\"name\":\"symbol_info\",\"description\":\"Get detailed information about a specific "
	    "symbol\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"description\":"
	    "\"Symbol name to look up\"}},\"required\":[\"name\"]}},");
	printf(
	    "{\"name\":\"file_symbols\",\"description\":\"List all symbols in a specific "
	    "file\",\"inputSchema\":{\"type\":\"object\",\"properties\":{\"file\":{\"type\":\"string\",\"description\":"
	    "\"File path to analyze\"}},\"required\":[\"file\"]}}");
	printf("]}}\n");
	fflush(stdout);
}

static void
handlesymbolinfo(Arena *a, String8 symbolname)
{
	Symbollist allsymbols, filesymbols;
	Symbolnode *node, *symnode;
	Symbol *sym;
	String8array files;
	String8 source;
	u64 d, i, matches;

	memset(&allsymbols, 0, sizeof(allsymbols));
	for (d = 0; d < nelem(directories); d++) {
		files = listcfiles(a, directories[d]);
		for (i = 0; i < files.cnt; i++) {
			source = readfile(a, files.v[i]);
			if (source.len > 0 && source.str != NULL) {
				filesymbols = parsecfiletreesitter(a, files.v[i], source);
				if (filesymbols.count > 0) {
					for (symnode = filesymbols.start; symnode != NULL; symnode = symnode->next)
						symbollistpush(a, &allsymbols, symnode->symbol);
				}
			}
		}
	}
	printf("{\"content\":[{\"type\":\"text\",\"text\":\"{\\\"symbols\\\":[");
	matches = 0;
	for (node = allsymbols.start; node != NULL; node = node->next) {
		sym = &node->symbol;
		if (str8cmp(sym->name, symbolname, 0)) {
			if (matches > 0)
				printf(",");
			printf("{\\\"name\\\":\\\"");
			printjsonescaped(sym->name);
			printf("\\\",\\\"type\\\":\\\"");
			printjsonescaped(sym->type);
			printf("\\\",\\\"file\\\":\\\"");
			printjsonescaped(sym->file);
			printf("\\\",\\\"line\\\":%u,\\\"signature\\\":\\\"", sym->line);
			printjsonescaped(sym->signature);
			printf("\\\"}");
			matches++;
		}
	}
	printf("]}\"}]}");
}

static void
handlefilesymbols(Arena *a, String8 filepath)
{
	Symbollist symbols;
	Symbolnode *node;
	Symbol *sym;
	String8 source;
	u64 matches;

	memset(&symbols, 0, sizeof(symbols));
	source = readfile(a, filepath);
	if (source.len == 0 || source.str == NULL) {
		printf("{\"content\":[{\"type\":\"text\",\"text\":\"{\\\"error\\\":\\\"Failed to read file '%.*s'\\\"}\"}]}",
		       str8varg(filepath));
		return;
	}
	symbols = parsecfiletreesitter(a, filepath, source);
	if (symbols.count == 0) {
		printf("{\"content\":[{\"type\":\"text\",\"text\":\"{\\\"symbols\\\":[]}\"}]}");
		return;
	}
	printf("{\"content\":[{\"type\":\"text\",\"text\":\"{\\\"symbols\\\":[");
	matches = 0;
	for (node = symbols.start; node != NULL; node = node->next) {
		sym = &node->symbol;
		if (matches > 0)
			printf(",");
		printf("{\\\"name\\\":\\\"");
		printjsonescaped(sym->name);
		printf("\\\",\\\"type\\\":\\\"");
		printjsonescaped(sym->type);
		printf("\\\",\\\"file\\\":\\\"");
		printjsonescaped(sym->file);
		printf("\\\",\\\"line\\\":%u,\\\"signature\\\":\\\"", sym->line);
		printjsonescaped(sym->signature);
		printf("\\\"}");
		matches++;
	}
	printf("]}\"}]}");
}

static void
handletoolcall(Arena *a, String8 requestid, String8 toolname, String8 arguments)
{
	String8 pattern, filepath, symbolname;
	u64 patternstart, patternend, filestart, fileend, namestart, nameend;

	if (str8cmp(toolname, str8lit("symbol_search"), 0)) {
		pattern = str8zero();
		if (arguments.len > 10 && arguments.str != NULL) {
			patternstart = str8index(arguments, 0, str8lit("\"pattern\":\""), 0);
			if (patternstart < arguments.len) {
				patternstart += 11;
				if (patternstart < arguments.len) {
					patternend = str8index(arguments, patternstart, str8lit("\""), 0);
					if (patternend < arguments.len)
						pattern = str8substr(arguments, rng1u64(patternstart, patternend));
				}
			}
		}
		printf("{\"jsonrpc\":\"2.0\",\"id\":");
		if (requestid.len > 0)
			printjsonescaped(requestid);
		else
			printf("null");
		printf(",\"result\":");
		handlesymbolsearch(a, pattern);
		printf("}\n");
		fflush(stdout);
	} else if (str8cmp(toolname, str8lit("symbol_info"), 0)) {
		symbolname = str8zero();
		if (arguments.len > 8 && arguments.str != NULL) {
			namestart = str8index(arguments, 0, str8lit("\"name\":\""), 0);
			if (namestart < arguments.len) {
				namestart += 8;
				if (namestart < arguments.len) {
					nameend = str8index(arguments, namestart, str8lit("\""), 0);
					if (nameend < arguments.len)
						symbolname = str8substr(arguments, rng1u64(namestart, nameend));
				}
			}
		}
		printf("{\"jsonrpc\":\"2.0\",\"id\":");
		if (requestid.len > 0)
			printjsonescaped(requestid);
		else
			printf("null");
		printf(",\"result\":");
		handlesymbolinfo(a, symbolname);
		printf("}\n");
	} else if (str8cmp(toolname, str8lit("file_symbols"), 0)) {
		filepath = str8zero();
		if (arguments.len > 8 && arguments.str != NULL) {
			filestart = str8index(arguments, 0, str8lit("\"file\":\""), 0);
			if (filestart < arguments.len) {
				filestart += 8;
				if (filestart < arguments.len) {
					fileend = str8index(arguments, filestart, str8lit("\""), 0);
					if (fileend < arguments.len)
						filepath = str8substr(arguments, rng1u64(filestart, fileend));
				}
			}
		}
		printf("{\"jsonrpc\":\"2.0\",\"id\":");
		if (requestid.len > 0)
			printjsonescaped(requestid);
		else
			printf("null");
		printf(",\"result\":");
		handlefilesymbols(a, filepath);
		printf("}\n");
	} else {
		printf("{\"jsonrpc\":\"2.0\",\"id\":");
		if (requestid.len > 0)
			printjsonescaped(requestid);
		else
			printf("null");
		printf(",\"error\":{\"code\":-32601,\"message\":\"Unknown tool: %.*s\"}}\n", str8varg(toolname));
	}
	fflush(stdout);
}

static void
handlemcprequest(Arena *a, String8 line)
{
	String8 requestid, toolname, arguments;
	u64 idstart, idend, namestart, nameend, argsstart, bracecount, argsend;

	requestid = str8zero();
	idstart = str8index(line, 0, str8lit("\"id\":"), 0);
	if (idstart < line.len) {
		idstart += 5;
		if (idstart < line.len) {
			if (line.str[idstart] == '"') {
				idstart++;
				idend = str8index(line, idstart, str8lit("\""), 0);
				if (idend < line.len)
					requestid = str8substr(line, rng1u64(idstart, idend));
			} else {
				idend = idstart;
				while (idend < line.len && (line.str[idend] >= '0' && line.str[idend] <= '9'))
					idend++;
				if (idend > idstart)
					requestid = str8substr(line, rng1u64(idstart, idend));
			}
		}
	}
	if (str8index(line, 0, str8lit("\"method\":\"initialize\""), 0) < line.len)
		handleinitialize(requestid);
	else if (str8index(line, 0, str8lit("\"method\":\"tools/list\""), 0) < line.len)
		handletoolslist(requestid);
	else if (str8index(line, 0, str8lit("\"method\":\"tools/call\""), 0) < line.len) {
		namestart = str8index(line, 0, str8lit("\"name\":\""), 0);
		if (namestart < line.len) {
			namestart += 8;
			nameend = str8index(line, namestart, str8lit("\""), 0);
			if (nameend < line.len) {
				toolname = str8substr(line, rng1u64(namestart, nameend));
				argsstart = str8index(line, 0, str8lit("\"arguments\":{"), 0);
				arguments = str8zero();
				if (argsstart < line.len) {
					argsstart += 12;
					bracecount = 1;
					argsend = argsstart;
					while (argsend < line.len && bracecount > 0) {
						if (line.str[argsend] == '{')
							bracecount++;
						else if (line.str[argsend] == '}')
							bracecount--;
						argsend++;
					}
					if (bracecount == 0) {
						if (argsend > argsstart)
							arguments = str8substr(line, rng1u64(argsstart, argsend - 1));
					}
				}
				handletoolcall(a, requestid, toolname, arguments);
			}
		}
	}
}

int
main(void)
{
	Arenaparams ap;
	Arena *arena;
	Temp temp;
	String8 input;
	char line[8192];
	size_t len;

	sysinfo.nprocs = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo.pagesz = sysconf(_SC_PAGESIZE);
	sysinfo.lpagesz = 0x200000;
	ap.flags = arenaflags;
	ap.ressz = arenaressz;
	ap.cmtsz = arenacmtsz;
	arena = arenaalloc(ap);
	while (fgets(line, sizeof(line), stdin)) {
		len = strlen(line);
		if (len > 0 && line[len - 1] == '\n') {
			line[len - 1] = '\0';
			len--;
		}
		input = str8((u8 *)line, len);
		temp = tempbegin(arena);
		handlemcprequest(arena, input);
		tempend(temp);
	}
	arenarelease(arena);
	return 0;
}
