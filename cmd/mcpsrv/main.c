// clang-format off
#include "base/inc.h"
#include "base/inc.c"
#include <tree_sitter/api.h>
#include "parser_c.c"
// clang-format on

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

typedef struct SymbolNode SymbolNode;
struct SymbolNode
{
	SymbolNode *next;
	Symbol symbol;
};

typedef struct SymbolList SymbolList;
struct SymbolList
{
	SymbolNode *first;
	SymbolNode *last;
	u64 count;
};

read_only static String8 directories[] = {str8_lit_comp("base"), str8_lit_comp("9p"), str8_lit_comp("cmd")};

static String8
cleansignature(Arena *a, String8 signature)
{
	if (signature.size == 0)
	{
		return signature;
	}
	u64 newlinepos = str8_find_needle(signature, 0, str8_lit("\n"), 0);
	String8 cleaned = (newlinepos < signature.size) ? str8_prefix(signature, newlinepos) : signature;
	return str8_copy(a, cleaned);
}

static String8
extractfunctionsignature(Arena *a, String8 source, TSNode node)
{
	u32 startbyte = ts_node_start_byte(node);
	u32 endbyte = ts_node_end_byte(node);
	if (startbyte >= source.size || endbyte > source.size || endbyte <= startbyte)
	{
		return str8_zero();
	}
	String8 signature = str8_substr(source, rng1u64(startbyte, endbyte));
	b32 foundopenbrace = 0;
	u64 i = 0;
	for (; i < signature.size; i++)
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
	if (foundopenbrace && i < signature.size)
	{
		signature = str8_prefix(signature, i);
	}
	return cleansignature(a, signature);
}

static b32
symbolexists(SymbolList *list, Symbol symbol)
{
	for (SymbolNode *node = list->first; node != NULL; node = node->next)
	{
		if (str8_match(node->symbol.name, symbol.name, 0) && str8_match(node->symbol.type, symbol.type, 0) &&
		    str8_match(node->symbol.file, symbol.file, 0) && node->symbol.line == symbol.line)
		{
			return 1;
		}
	}
	return 0;
}

static void
symbollistpush(Arena *a, SymbolList *list, Symbol symbol)
{
	if (symbolexists(list, symbol))
	{
		return;
	}
	SymbolNode *node = push_array(a, SymbolNode, 1);
	*node = (SymbolNode){.symbol = symbol, .next = NULL};
	if (list->last == NULL)
	{
		list->first = node;
		list->last = node;
	}
	else
	{
		list->last->next = node;
		list->last = node;
	}
	list->count++;
}

static String8Array
listcfiles(Arena *a, String8 dirpath)
{
	String8Array result = {0};
	String8List files = {0};
	String8 dirpathcpy = str8_copy(a, dirpath);
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
		String8 name = str8_cstring(entry->d_name);
		String8 ext = str8_skip_last_dot(name);
		if (!str8_match(ext, str8_lit(".c"), 0) || !str8_match(ext, str8_lit(".h"), 0))
		{
			String8 fullpath = str8f(a, "%.*s/%.*s", str8_varg(dirpath), str8_varg(name));
			str8_list_push(a, &files, fullpath);
		}
	}
	closedir(dp);
	if (files.node_count == 0)
	{
		return result;
	}
	result.v = push_array(a, String8, files.node_count);
	u64 i = 0;
	for (String8Node *node = files.first; node != NULL; node = node->next)
	{
		result.v[i++] = node->string;
	}
	result.count = files.node_count;
	return result;
}

static void
symbolresult(Arena *a, Jsonbuilder *b, SymbolList *symbols)
{
	jsonbobjstart(b);
	jsonbobjkey(b, str8_lit("content"));
	jsonbarrstart(b);

	jsonbobjstart(b);
	jsonbobjkey(b, str8_lit("type"));
	jsonbwritestr(b, str8_lit("text"));
	jsonbobjcomma(b);
	jsonbobjkey(b, str8_lit("text"));

	u64 estsize = 1024 + symbols->count * 200;
	Jsonbuilder textbuilder = jsonbuilder(a, estsize);
	jsonbobjstart(&textbuilder);
	jsonbobjkey(&textbuilder, str8_lit("symbols"));
	jsonbarrstart(&textbuilder);

	for (SymbolNode *node = symbols->first; node != NULL; node = node->next)
	{
		Symbol *sym = &node->symbol;
		if (node != symbols->first)
		{
			jsonbarrcomma(&textbuilder);
		}
		jsonbobjstart(&textbuilder);
		jsonbobjkey(&textbuilder, str8_lit("name"));
		jsonbwritestr(&textbuilder, sym->name);
		jsonbobjcomma(&textbuilder);
		jsonbobjkey(&textbuilder, str8_lit("type"));
		jsonbwritestr(&textbuilder, sym->type);
		jsonbobjcomma(&textbuilder);
		jsonbobjkey(&textbuilder, str8_lit("file"));
		jsonbwritestr(&textbuilder, sym->file);
		jsonbobjcomma(&textbuilder);
		jsonbobjkey(&textbuilder, str8_lit("line"));
		jsonbwritenum(&textbuilder, sym->line);
		jsonbobjcomma(&textbuilder);
		jsonbobjkey(&textbuilder, str8_lit("signature"));
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
	u64 estsize = 100 + requestid.size + result.size;
	Jsonbuilder b = jsonbuilder(a, estsize);
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("jsonrpc"));
	jsonbwritestr(&b, str8_lit("2.0"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("id"));
	if (requestid.size > 0)
	{
		jsonbwritestr(&b, requestid);
	}
	else
	{
		jsonbwritenull(&b);
	}
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("result"));
	jsonbwrite(&b, result);
	jsonbobjend(&b);

	String8 output = jsonbfinish(&b);
	printf("%.*s\n", str8_varg(output));
	fflush(stdout);
}

static void
printmcperror(Arena *a, String8 requestid, s32 code, String8 message)
{
	u64 estsize = 150 + requestid.size + message.size;
	Jsonbuilder b = jsonbuilder(a, estsize);
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("jsonrpc"));
	jsonbwritestr(&b, str8_lit("2.0"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("id"));
	if (requestid.size > 0)
	{
		jsonbwritestr(&b, requestid);
	}
	else
	{
		jsonbwritenull(&b);
	}
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("error"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("code"));
	jsonbwritenum(&b, code);
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8_lit("message"));
	jsonbwritestr(&b, message);
	jsonbobjend(&b);
	jsonbobjend(&b);

	String8 output = jsonbfinish(&b);
	printf("%.*s\n", str8_varg(output));
	fflush(stdout);
}

static void
extractsymbolsfromnode(Arena *a, SymbolList *symbols, TSNode node, String8 filepath, String8 source)
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
				if (startline < source.size && endline < source.size)
				{
					u32 startbyte = ts_node_start_byte(child);
					u32 endbyte = ts_node_end_byte(child);
					if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
					{
						String8 identifiertext = str8_substr(source, rng1u64(startbyte, endbyte));
						if (identifiertext.size > 0)
						{
							Symbol symbol = {.name = str8_copy(a, identifiertext),
							                 .type = str8_lit("function"),
							                 .file = str8_copy(a, filepath),
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
				if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
				{
					String8 identifiertext = str8_substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.size > 0)
					{
						Symbol symbol = {0};
						symbol.name = str8_copy(a, identifiertext);
						symbol.type = str8_lit("struct");
						symbol.file = str8_copy(a, filepath);
						symbol.line = startpoint.row + 1;
						u32 startbyte = ts_node_start_byte(node);
						u32 endbyte = ts_node_end_byte(node);
						if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
						{
							String8 nodetext = str8_substr(source, rng1u64(startbyte, endbyte));
							String8List lines = str8_split(a, nodetext, (u8 *)"\n", 1, 0);
							if (lines.first != NULL)
							{
								symbol.signature = str8_copy(a, lines.first->string);
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
				if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
				{
					String8 identifiertext = str8_substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.size > 0)
					{
						Symbol symbol = {0};
						symbol.name = str8_copy(a, identifiertext);
						symbol.type = str8_lit("enum");
						symbol.file = str8_copy(a, filepath);
						symbol.line = startpoint.row + 1;
						u32 startbyte = ts_node_start_byte(node);
						u32 endbyte = ts_node_end_byte(node);
						if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
						{
							String8 nodetext = str8_substr(source, rng1u64(startbyte, endbyte));
							String8List lines = str8_split(a, nodetext, (u8 *)"\n", 1, 0);
							if (lines.first != NULL)
							{
								symbol.signature = str8_copy(a, lines.first->string);
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
						if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
						{
							String8 identifiertext = str8_substr(source, rng1u64(startbyte, endbyte));
							if (identifiertext.size > 0)
							{
								Symbol symbol = {0};
								symbol.name = str8_copy(a, identifiertext);
								symbol.type = str8_lit("enum");
								symbol.file = str8_copy(a, filepath);
								symbol.line = startpoint.row + 1;
								u32 startbyte = ts_node_start_byte(enumlistchild);
								u32 endbyte = ts_node_end_byte(enumlistchild);
								if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
								{
									String8 nodetext = str8_substr(source, rng1u64(startbyte, endbyte));
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
				if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
				{
					String8 identifiertext = str8_substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.size > 0)
					{
						Symbol symbol = {0};
						symbol.name = str8_copy(a, identifiertext);
						symbol.type = str8_lit("typedef");
						symbol.file = str8_copy(a, filepath);
						symbol.line = startpoint.row + 1;
						u32 startbyte = ts_node_start_byte(node);
						u32 endbyte = ts_node_end_byte(node);
						if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
						{
							String8 nodetext = str8_substr(source, rng1u64(startbyte, endbyte));
							String8List lines = str8_split(a, nodetext, (u8 *)"\n", 1, 0);
							if (lines.first != NULL)
							{
								symbol.signature = str8_copy(a, lines.first->string);
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
				if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
				{
					String8 identifiertext = str8_substr(source, rng1u64(startbyte, endbyte));
					if (identifiertext.size > 0)
					{
						Symbol symbol = {0};
						symbol.name = str8_copy(a, identifiertext);
						symbol.type = str8_lit("macro");
						symbol.file = str8_copy(a, filepath);
						symbol.line = startpoint.row + 1;
						u32 startbyte = ts_node_start_byte(node);
						u32 endbyte = ts_node_end_byte(node);
						if (startbyte < source.size && endbyte <= source.size && endbyte > startbyte)
						{
							String8 nodetext = str8_substr(source, rng1u64(startbyte, endbyte));
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

static SymbolList
parsecfiletreesitter(Arena *a, String8 filepath, String8 source)
{
	SymbolList symbols = {0};
	TSParser *parser = ts_parser_new();
	if (!ts_parser_set_language(parser, tree_sitter_c()))
	{
		fprintf(stderr, "mcpsrv: failed to set tree-sitter language for C\n");
		ts_parser_delete(parser);
		return symbols;
	}
	char *sourcecstr = (char *)push_array(a, u8, source.size + 1);
	memcpy(sourcecstr, source.str, source.size);
	sourcecstr[source.size] = '\0';
	TSTree *tree = ts_parser_parse_string(parser, NULL, sourcecstr, source.size);
	if (tree == NULL)
	{
		fprintf(stderr, "mcpsrv: failed to parse source file '%.*s'\n", str8_varg(filepath));
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
	SymbolList allsymbols = {0};
	SymbolList filtered = {0};
	for (u64 d = 0; d < ArrayCount(directories); d++)
	{
		String8Array files = listcfiles(a, directories[d]);
		for (u64 i = 0; i < files.count; i++)
		{
			String8 source = os_data_from_file_path(a, files.v[i]);
			if (source.size > 0 && source.str != NULL)
			{
				SymbolList filesymbols = parsecfiletreesitter(a, files.v[i], source);
				if (filesymbols.count == 0)
				{
					fprintf(stderr, "mcpsrv: failed to parse symbols from '%.*s' using tree-sitter\n", str8_varg(files.v[i]));
				}
				else
				{
					for (SymbolNode *symnode = filesymbols.first; symnode != NULL; symnode = symnode->next)
					{
						symbollistpush(a, &allsymbols, symnode->symbol);
					}
				}
			}
		}
	}
	for (SymbolNode *node = allsymbols.first; node != NULL; node = node->next)
	{
		Symbol *sym = &node->symbol;
		if (pattern.size == 0 ||
		    (sym->name.size >= pattern.size &&
		     str8_find_needle(sym->name, 0, pattern, StringMatchFlag_CaseInsensitive) < sym->name.size))
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
	jsonbobjkey(&b, str8_lit("protocolVersion"));
	jsonbwritestr(&b, str8_lit("2024-11-05"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("capabilities"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("tools"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("listChanged"));
	jsonbwritebool(&b, 0);
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("serverInfo"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("name"));
	jsonbwritestr(&b, str8_lit("mcpsrv"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8_lit("version"));
	jsonbwritestr(&b, str8_lit("0.1.0"));
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
	jsonbobjkey(&b, str8_lit("tools"));
	jsonbarrstart(&b);

	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("name"));
	jsonbwritestr(&b, str8_lit("symbol_search"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8_lit("description"));
	jsonbwritestr(&b, str8_lit("Search for symbols in C codebase"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("inputSchema"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("type"));
	jsonbwritestr(&b, str8_lit("object"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("properties"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("pattern"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("type"));
	jsonbwritestr(&b, str8_lit("string"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8_lit("description"));
	jsonbwritestr(&b, str8_lit("Search pattern for symbol names"));
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("required"));
	jsonbarrstart(&b);
	jsonbarrend(&b);
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbarrcomma(&b);

	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("name"));
	jsonbwritestr(&b, str8_lit("symbol_info"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8_lit("description"));
	jsonbwritestr(&b, str8_lit("Get detailed information about a specific symbol"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("inputSchema"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("type"));
	jsonbwritestr(&b, str8_lit("object"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("properties"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("name"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("type"));
	jsonbwritestr(&b, str8_lit("string"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8_lit("description"));
	jsonbwritestr(&b, str8_lit("Symbol name to look up"));
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("required"));
	jsonbarrstart(&b);
	jsonbwritestr(&b, str8_lit("name"));
	jsonbarrend(&b);
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbarrcomma(&b);

	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("name"));
	jsonbwritestr(&b, str8_lit("file_symbols"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8_lit("description"));
	jsonbwritestr(&b, str8_lit("List all symbols in a specific file"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("inputSchema"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("type"));
	jsonbwritestr(&b, str8_lit("object"));
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("properties"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("file"));
	jsonbobjstart(&b);
	jsonbobjkey(&b, str8_lit("type"));
	jsonbwritestr(&b, str8_lit("string"));
	jsonbobjcomma(&b);
	jsonbobjkey(&b, str8_lit("description"));
	jsonbwritestr(&b, str8_lit("File path to analyze"));
	jsonbobjend(&b);
	jsonbobjend(&b);
	jsonbobjcomma(&b);

	jsonbobjkey(&b, str8_lit("required"));
	jsonbarrstart(&b);
	jsonbwritestr(&b, str8_lit("file"));
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
	SymbolList allsymbols = {0};
	SymbolList filtered = {0};
	for (u64 d = 0; d < ArrayCount(directories); d++)
	{
		String8Array files = listcfiles(a, directories[d]);
		for (u64 i = 0; i < files.count; i++)
		{
			String8 source = os_data_from_file_path(a, files.v[i]);
			if (source.size > 0 && source.str != NULL)
			{
				SymbolList filesymbols = parsecfiletreesitter(a, files.v[i], source);
				if (filesymbols.count > 0)
				{
					for (SymbolNode *symnode = filesymbols.first; symnode != NULL; symnode = symnode->next)
					{
						symbollistpush(a, &allsymbols, symnode->symbol);
					}
				}
			}
		}
	}
	for (SymbolNode *node = allsymbols.first; node != NULL; node = node->next)
	{
		Symbol *sym = &node->symbol;
		if (str8_match(sym->name, symbolname, 0))
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
	SymbolList symbols = {0};
	String8 source = os_data_from_file_path(a, filepath);
	if (source.size == 0 || source.str == NULL)
	{
		Jsonbuilder b = jsonbuilder(a, 512);
		jsonbobjstart(&b);
		jsonbobjkey(&b, str8_lit("content"));
		jsonbarrstart(&b);

		jsonbobjstart(&b);
		jsonbobjkey(&b, str8_lit("type"));
		jsonbwritestr(&b, str8_lit("text"));
		jsonbobjcomma(&b);
		jsonbobjkey(&b, str8_lit("text"));
		jsonbwritec(&b, '"');
		jsonbwrite(&b, str8_lit("{\\\"error\\\":\\\"Failed to read file '"));
		jsonbwrite(&b, filepath);
		jsonbwrite(&b, str8_lit("'\\\"}"));
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
	if (str8_match(toolname, str8_lit("symbol_search"), 0))
	{
		String8 pattern = str8_zero();
		if (argsobj.type == JSON_OBJECT)
		{
			Jsonvalue patternval = jsonget(argsobj, str8_lit("pattern"));
			if (patternval.type == JSON_STRING)
			{
				pattern = patternval.string;
			}
		}
		String8 result = symbolsearch(a, pattern);
		printmcpresponse(a, requestid, result);
	}
	else if (str8_match(toolname, str8_lit("symbol_info"), 0))
	{
		String8 symbolname = str8_zero();
		if (argsobj.type == JSON_OBJECT)
		{
			Jsonvalue nameval = jsonget(argsobj, str8_lit("name"));
			if (nameval.type == JSON_STRING)
			{
				symbolname = nameval.string;
			}
		}
		String8 result = symbolinfo(a, symbolname);
		printmcpresponse(a, requestid, result);
	}
	else if (str8_match(toolname, str8_lit("file_symbols"), 0))
	{
		String8 filepath = str8_zero();
		if (argsobj.type == JSON_OBJECT)
		{
			Jsonvalue fileval = jsonget(argsobj, str8_lit("file"));
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
		printmcperror(a, requestid, -32601, str8f(a, "Unknown tool: %.*s", str8_varg(toolname)));
	}
}

static void
mcprequest(Arena *a, String8 line)
{
	String8 requestid = str8_zero();
	u64 idstart = str8_find_needle(line, 0, str8_lit("\"id\":"), 0);
	if (idstart < line.size)
	{
		idstart += 5;
		if (idstart < line.size)
		{
			if (line.str[idstart] == '"')
			{
				idstart++;
				u64 idend = str8_find_needle(line, idstart, str8_lit("\""), 0);
				if (idend < line.size)
				{
					requestid = str8_substr(line, rng1u64(idstart, idend));
				}
			}
			else
			{
				u64 idend = idstart;
				while (idend < line.size && (line.str[idend] >= '0' && line.str[idend] <= '9'))
				{
					idend++;
				}
				if (idend > idstart)
				{
					requestid = str8_substr(line, rng1u64(idstart, idend));
				}
			}
		}
	}
	if (str8_find_needle(line, 0, str8_lit("\"method\":\"initialize\""), 0) < line.size)
	{
		initialize(a, requestid);
	}
	else if (str8_find_needle(line, 0, str8_lit("\"method\":\"tools/list\""), 0) < line.size)
	{
		toolslist(a, requestid);
	}
	else if (str8_find_needle(line, 0, str8_lit("\"method\":\"tools/call\""), 0) < line.size)
	{
		u64 namestart = str8_find_needle(line, 0, str8_lit("\"name\":\""), 0);
		if (namestart < line.size)
		{
			namestart += 8;
			u64 nameend = str8_find_needle(line, namestart, str8_lit("\""), 0);
			if (nameend < line.size)
			{
				String8 toolname = str8_substr(line, rng1u64(namestart, nameend));
				u64 argsstart = str8_find_needle(line, 0, str8_lit("\"arguments\":{"), 0);
				String8 arguments = str8_zero();
				if (argsstart < line.size)
				{
					argsstart += 12;
					u64 bracecount = 1;
					u64 argsend = argsstart;
					while (argsend < line.size && bracecount > 0)
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
							arguments = str8_substr(line, rng1u64(argsstart, argsend - 1));
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
	OS_SystemInfo *sysinfo = os_get_system_info();
	sysinfo->logical_processor_count = sysconf(_SC_NPROCESSORS_ONLN);
	sysinfo->page_size = sysconf(_SC_PAGESIZE);
	sysinfo->large_page_size = 0x200000;
	Arena *arena = arena_alloc();
	char line[8192] = {0};
	while (fgets(line, sizeof(line), stdin))
	{
		size_t size = strlen(line);
		if (size > 0 && line[size - 1] == '\n')
		{
			line[size - 1] = '\0';
			size--;
		}
		String8 input = str8((u8 *)line, size);
		Temp temp = temp_begin(arena);
		mcprequest(arena, input);
		temp_end(temp);
	}
	arena_release(arena);
	return 0;
}
