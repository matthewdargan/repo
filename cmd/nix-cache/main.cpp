// nix-cache - C++ Unity Build

// clang-format off
#include "base/inc.h"
#include "9p/inc.h"
#include "base/inc.c"
#include "9p/inc.c"
#undef internal
#undef global
// clang-format on

#include <climits>
#include <cstring>
#include <memory>
#include <sstream>
#include <string_view>
#include <unistd.h>
#include <vector>

#include <nix/cmd/command.hh>
#include <nix/cmd/installables.hh>
#include <nix/expr/attr-path.hh>
#include <nix/expr/eval-cache.hh>
#include <nix/expr/eval-settings.hh>
#include <nix/expr/eval.hh>
#include <nix/expr/value.hh>
#include <nix/fetchers/fetch-settings.hh>
#include <nix/fetchers/fetchers.hh>
#include <nix/flake/flake.hh>
#include <nix/flake/flakeref.hh>
#include <nix/flake/settings.hh>
#include <nix/main/shared.hh>
#include <nix/store/local-fs-store.hh>
#include <nix/store/store-api.hh>
#include <nix/store/store-open.hh>
#include <nix/util/archive.hh>
#include <nix/util/serialise.hh>

#include <lzma.h>

////////////////////////////////
//~ Nix C++ API

struct NixStore
{
	nix::ref<nix::Store> store;
	std::unique_ptr<nix::EvalState> eval_state;
	nix::fetchers::Settings fetch_settings;
	nix::flake::Settings flake_settings;
	bool read_only_mode;
	std::shared_ptr<nix::EvalSettings> eval_settings;
};

static NixStore *
nix_store_open(void)
{
	try
	{
		nix::initNix();
		nix::initGC();

		auto store = nix::openStore();

		NixStore *result = new NixStore{
		    .store = store,
		    .eval_state = nullptr,
		    .fetch_settings = {},
		    .flake_settings = {},
		    .read_only_mode = false,
		    .eval_settings = nullptr,
		};
		result->eval_settings = std::make_shared<nix::EvalSettings>(result->read_only_mode);

		nix::LookupPath lookupPath;
		result->eval_state =
		    std::make_unique<nix::EvalState>(lookupPath, store, result->fetch_settings, *result->eval_settings);

		return result;
	}
	catch(...)
	{
		return nullptr;
	}
}

static void
nix_store_close(NixStore *nix_store)
{
	if(nix_store)
	{
		delete nix_store;
	}
}

static std::string
std_from_str8(String8 s)
{
	return std::string((char *)s.str, s.size);
}

static String8
str8_from_std(Arena *arena, const std::string &s)
{
	u8 *buf = (u8 *)arena_push(arena, s.size(), 8, 0);
	MemoryCopy(buf, s.data(), s.size());
	String8 result = {buf, s.size()};
	return result;
}

static String8
nix_build_flake(Arena *arena, NixStore *nix_store, String8 flake_root, String8 output_name)
{
	if(!arena || !nix_store || flake_root.size == 0 || output_name.size == 0)
	{
		return (String8){0, 0};
	}

	try
	{
		auto &store = nix_store->store;
		auto &state = *nix_store->eval_state;

		std::string flake_path = std_from_str8(flake_root);
		std::string output_name_str = std_from_str8(output_name);

		if(flake_path[0] != '/')
		{
			char cwd[PATH_MAX];
			if(getcwd(cwd, sizeof(cwd)) != nullptr)
			{
				flake_path = std::string(cwd) + "/" + flake_path;
			}
		}

		auto flake_ref = nix::parseFlakeRef(nix_store->fetch_settings, flake_path, std::nullopt);

		auto locked_flake = nix::flake::lockFlake(nix_store->flake_settings, state, flake_ref,
		                                          nix::flake::LockFlags{
		                                              .updateLockFile = false,
		                                              .useRegistries = true,
		                                              .allowUnlocked = false,
		                                              .referenceLockFilePath = std::nullopt,
		                                              .outputLockFilePath = std::nullopt,
		                                              .inputOverrides = {},
		                                              .inputUpdates = {},
		                                          });

		nix::Value vFlake;
		nix::flake::callFlake(state, locked_flake, vFlake);

		auto vOutputs = vFlake.attrs()->get(state.symbols.create("outputs"));
		if(!vOutputs)
		{
			return (String8){0, 0};
		}

		auto emptyBindings = state.allocBindings(0);
		auto [vRoot, pos] = nix::findAlongAttrPath(state, output_name_str, *emptyBindings, *vOutputs->value);

		state.forceValue(*vRoot, pos);

		if(vRoot->type() != nix::nAttrs)
		{
			return (String8){0, 0};
		}

		auto drvPathAttr = vRoot->attrs()->get(state.symbols.create("drvPath"));
		if(!drvPathAttr)
		{
			return (String8){0, 0};
		}

		state.forceValue(*drvPathAttr->value, pos);
		if(drvPathAttr->value->type() != nix::nString)
		{
			return (String8){0, 0};
		}

		auto drvPath = store->parseStorePath(drvPathAttr->value->c_str());

		auto drv_path_with_outputs =
		    nix::DerivedPath::Built{.drvPath = nix::makeConstantStorePathRef(drvPath), .outputs = nix::OutputsSpec::All{}};

		nix::DerivedPath derived_path = drv_path_with_outputs;
		store->buildPaths({derived_path});

		auto outputs = store->queryDerivationOutputMap(drvPath);
		if(outputs.empty())
		{
			return (String8){0, 0};
		}

		std::string path = store->printStorePath(outputs.begin()->second);
		return str8_from_std(arena, path);
	}
	catch(const std::exception &e)
	{
		fprintf(stderr, "nix_build_flake error: %s\n", e.what());
		return (String8){0, 0};
	}
	catch(...)
	{
		fprintf(stderr, "nix_build_flake: unknown error\n");
		return (String8){0, 0};
	}
}

static String8
nix_query_nar_hash(Arena *arena, NixStore *nix_store, String8 store_path_str)
{
	if(!arena || !nix_store || store_path_str.size == 0)
	{
		return (String8){0, 0};
	}

	try
	{
		auto &store = nix_store->store;
		std::string path_str = std_from_str8(store_path_str);
		auto store_path = store->parseStorePath(path_str);

		auto info = store->queryPathInfo(store_path);

		std::string hash_str = info->narHash.to_string(nix::HashFormat::SRI, true);
		return str8_from_std(arena, hash_str);
	}
	catch(...)
	{
		return (String8){0, 0};
	}
}

static String8
nix_query_references(Arena *arena, NixStore *nix_store, String8 store_path_str)
{
	if(!arena || !nix_store || store_path_str.size == 0)
	{
		return (String8){0, 0};
	}

	try
	{
		auto &store = nix_store->store;
		std::string path_str = std_from_str8(store_path_str);
		auto store_path = store->parseStorePath(path_str);

		auto info = store->queryPathInfo(store_path);

		std::ostringstream oss;
		bool first = true;
		for(const auto &ref : info->references)
		{
			if(!first)
			{
				oss << "\n";
			}
			oss << store->printStorePath(ref);
			first = false;
		}

		std::string result = oss.str();
		return str8_from_std(arena, result);
	}
	catch(...)
	{
		return (String8){0, 0};
	}
}

static String8
nix_generate_nar_xz(Arena *arena, NixStore *nix_store, String8 store_path_str)
{
	if(!arena || !nix_store || store_path_str.size == 0)
	{
		return (String8){0, 0};
	}

	try
	{
		auto &store = nix_store->store;
		std::string path_str = std_from_str8(store_path_str);
		auto store_path = store->parseStorePath(path_str);

		nix::StringSink nar_sink;
		store->narFromPath(store_path, nar_sink);

		std::string nar_data = std::move(nar_sink.s);

		lzma_stream strm = LZMA_STREAM_INIT;
		lzma_ret ret = lzma_easy_encoder(&strm, 9, LZMA_CHECK_CRC64);
		if(ret != LZMA_OK)
		{
			return (String8){0, 0};
		}

		size_t out_buf_size = nar_data.size() * 2;
		std::vector<unsigned char> compressed;
		compressed.reserve(out_buf_size);

		strm.next_in = reinterpret_cast<const uint8_t *>(nar_data.data());
		strm.avail_in = nar_data.size();

		unsigned char out_buf[65536];

		do
		{
			strm.next_out = out_buf;
			strm.avail_out = sizeof(out_buf);

			ret = lzma_code(&strm, LZMA_FINISH);

			if(ret != LZMA_OK && ret != LZMA_STREAM_END)
			{
				lzma_end(&strm);
				return (String8){0, 0};
			}

			size_t produced = sizeof(out_buf) - strm.avail_out;
			compressed.insert(compressed.end(), out_buf, out_buf + produced);

		} while(ret != LZMA_STREAM_END);

		lzma_end(&strm);

		u8 *result_buf = (u8 *)arena_push(arena, compressed.size(), 8, 0);
		MemoryCopy(result_buf, compressed.data(), compressed.size());
		return (String8){result_buf, compressed.size()};
	}
	catch(...)
	{
		return (String8){0, 0};
	}
}

////////////////////////////////
//~ Global State

static FsContext9P *fs_context = 0;
static Arena *cache_arena = 0;
static NixStore *nix_store = 0;

////////////////////////////////
//~ Nix Cache

static String8
nix_hash_from_store_path(Arena *arena, String8 store_path)
{
	Temp scratch = scratch_begin(&arena, 1);

	u64 last_slash = 0;
	for(u64 i = 0; i < store_path.size; i++)
	{
		if(store_path.str[i] == '/')
		{
			last_slash = i;
		}
	}

	String8 component = str8_skip(store_path, last_slash + 1);

	u64 first_dash = 0;
	for(u64 i = 0; i < component.size; i++)
	{
		if(component.str[i] == '-')
		{
			first_dash = i;
			break;
		}
	}

	String8 hash = str8_prefix(component, first_dash);
	String8 result = str8_copy(arena, hash);

	scratch_end(scratch);
	return result;
}

typedef struct NixStorePath NixStorePath;
struct NixStorePath
{
	String8 path;
	String8 hash;
	NixStorePath *next;
};

typedef struct NixCacheBuilder NixCacheBuilder;
struct NixCacheBuilder
{
	Arena *arena;
	FsContext9P *fs_ctx;
	NixStorePath *store_paths_first;
	NixStorePath *store_paths_last;
	String8 flake_root;
};

static String8
nix_generate_narinfo(Arena *arena, String8 store_path, String8 hash, String8 nar_hash, u64 nar_size, String8 references)
{
	String8 url = str8f(arena, "nar/%S.nar.xz", hash);

	String8 narinfo = str8f(arena,
	                        "StorePath: %S\n"
	                        "URL: %S\n"
	                        "Compression: xz\n"
	                        "NarHash: %S\n"
	                        "NarSize: %llu\n"
	                        "References: %S\n",
	                        store_path, url, nar_hash, nar_size, references);

	return narinfo;
}

static void
cache_add_store_path(NixCacheBuilder *builder, String8 store_path)
{
	Temp scratch = scratch_begin(&builder->arena, 1);

	fprintf(stdout, "nix-cache: processing %.*s\n", (int)store_path.size, store_path.str);
	fflush(stdout);

	String8 hash = nix_hash_from_store_path(builder->arena, store_path);
	if(hash.size == 0)
	{
		fprintf(stderr, "nix-cache: failed to extract hash from %.*s\n", (int)store_path.size, store_path.str);
		scratch_end(scratch);
		return;
	}

	String8 nar_data = nix_generate_nar_xz(cache_arena, nix_store, store_path);
	if(nar_data.size == 0)
	{
		fprintf(stderr, "nix-cache: failed to generate NAR for %.*s\n", (int)store_path.size, store_path.str);
		scratch_end(scratch);
		return;
	}

	String8 nar_hash = nix_query_nar_hash(scratch.arena, nix_store, store_path);
	if(nar_hash.size == 0)
	{
		fprintf(stderr, "nix-cache: failed to query NAR hash for %.*s\n", (int)store_path.size, store_path.str);
		scratch_end(scratch);
		return;
	}

	String8 output = nix_query_references(scratch.arena, nix_store, store_path);
	if(output.size == 0)
	{
		fprintf(stderr, "nix-cache: failed to query references for %.*s\n", (int)store_path.size, store_path.str);
		scratch_end(scratch);
		return;
	}

	String8List ref_hashes = {0};
	String8 remaining = output;

	for(;;)
	{
		u64 newline_pos = 0;
		b32 found = 0;
		for(u64 i = 0; i < remaining.size; i++)
		{
			if(remaining.str[i] == '\n')
			{
				newline_pos = i;
				found = 1;
				break;
			}
		}

		if(!found && remaining.size == 0)
		{
			break;
		}

		String8 ref_path = str8_prefix(remaining, newline_pos);
		if(ref_path.size > 0)
		{
			String8 ref_hash = nix_hash_from_store_path(scratch.arena, ref_path);
			str8_list_push(scratch.arena, &ref_hashes, ref_hash);
		}

		if(!found)
		{
			break;
		}

		remaining = str8_skip(remaining, newline_pos + 1);
	}

	StringJoin join = {.sep = str8_lit(" ")};
	String8 references = str8_list_join(scratch.arena, &ref_hashes, &join);

	String8 narinfo = nix_generate_narinfo(scratch.arena, store_path, hash, nar_hash, nar_data.size, references);

	String8 nar_filename = str8f(scratch.arena, "%S.nar.xz", hash);
	String8 nar_path = str8f(scratch.arena, "nar/%S", nar_filename);

	if(!temp9p_create(builder->arena, builder->fs_ctx, nar_path, 0644))
	{
		fprintf(stderr, "nix-cache: failed to create NAR file %.*s\n", (int)nar_path.size, nar_path.str);
		scratch_end(scratch);
		return;
	}

	TempNode9P *nar_node = temp9p_open(builder->fs_ctx, nar_path);
	if(nar_node == 0)
	{
		fprintf(stderr, "nix-cache: failed to open NAR file %.*s\n", (int)nar_path.size, nar_path.str);
		scratch_end(scratch);
		return;
	}

	temp9p_write(cache_arena, nar_node, 0, nar_data);

	String8 narinfo_filename = str8f(scratch.arena, "%S.narinfo", hash);

	if(!temp9p_create(builder->arena, builder->fs_ctx, narinfo_filename, 0644))
	{
		fprintf(stderr, "nix-cache: failed to create narinfo file %.*s\n", (int)narinfo_filename.size,
		        narinfo_filename.str);
		scratch_end(scratch);
		return;
	}

	TempNode9P *narinfo_node = temp9p_open(builder->fs_ctx, narinfo_filename);
	if(narinfo_node == 0)
	{
		fprintf(stderr, "nix-cache: failed to open narinfo file %.*s\n", (int)narinfo_filename.size, narinfo_filename.str);
		scratch_end(scratch);
		return;
	}

	String8 narinfo_data = str8_copy(cache_arena, narinfo);
	temp9p_write(cache_arena, narinfo_node, 0, narinfo_data);

	fprintf(stdout, "nix-cache: cached %.*s (%llu bytes)\n", (int)hash.size, hash.str, nar_data.size);
	fflush(stdout);

	scratch_end(scratch);
}

static void
cache_build_config(NixCacheBuilder *builder, String8 config_name)
{
	Temp scratch = scratch_begin(&builder->arena, 1);

	fprintf(stdout, "nix-cache: building .#%.*s\n", (int)config_name.size, config_name.str);
	fflush(stdout);

	String8 store_path = nix_build_flake(scratch.arena, nix_store, builder->flake_root, config_name);
	if(store_path.size == 0)
	{
		fprintf(stderr, "nix-cache: failed to build .#%.*s\n", (int)config_name.size, config_name.str);
		scratch_end(scratch);
		return;
	}

	cache_add_store_path(builder, store_path);

	scratch_end(scratch);
}

static void
cache_init(NixCacheBuilder *builder)
{
	Temp scratch = scratch_begin(&builder->arena, 1);

	if(!temp9p_create(builder->arena, builder->fs_ctx, str8_lit("nar"), P9_ModeFlag_Directory | 0755))
	{
		fprintf(stderr, "nix-cache: failed to create nar directory\n");
		scratch_end(scratch);
		return;
	}

	String8 cache_info_content = str8_lit("StoreDir: /nix/store\n"
	                                      "WantMassQuery: 1\n"
	                                      "Priority: 40\n");

	if(!temp9p_create(builder->arena, builder->fs_ctx, str8_lit("nix-cache-info"), 0644))
	{
		fprintf(stderr, "nix-cache: failed to create nix-cache-info\n");
		scratch_end(scratch);
		return;
	}

	TempNode9P *info_node = temp9p_open(builder->fs_ctx, str8_lit("nix-cache-info"));
	if(info_node == 0)
	{
		fprintf(stderr, "nix-cache: failed to open nix-cache-info\n");
		scratch_end(scratch);
		return;
	}

	String8 info_data = str8_copy(cache_arena, cache_info_content);
	temp9p_write(cache_arena, info_node, 0, info_data);

	scratch_end(scratch);
}

////////////////////////////////
//~ Worker Thread Pool

typedef struct Worker Worker;
struct Worker
{
	u64 id;
	Thread handle;
};

typedef struct WorkQueueNode WorkQueueNode;
struct WorkQueueNode
{
	WorkQueueNode *next;
	OS_Handle connection;
};

typedef struct WorkerPool WorkerPool;
struct WorkerPool
{
	b32 is_live;
	Semaphore semaphore;
	pthread_mutex_t mutex;
	Arena *arena;
	WorkQueueNode *queue_first;
	WorkQueueNode *queue_last;
	WorkQueueNode *node_free_list;
	Worker *workers;
	u64 worker_count;
};

static WorkerPool *worker_pool = 0;

static WorkQueueNode *
work_queue_node_alloc(WorkerPool *pool)
{
	WorkQueueNode *node = 0;
	DeferLoop(pthread_mutex_lock(&pool->mutex), pthread_mutex_unlock(&pool->mutex))
	{
		node = pool->node_free_list;
		if(node != 0)
		{
			SLLStackPop(pool->node_free_list);
		}
		else
		{
			node = (WorkQueueNode *)push_array_no_zero(pool->arena, WorkQueueNode, 1);
		}
	}
	MemoryZeroStruct(node);
	return node;
}

static void
work_queue_node_release(WorkerPool *pool, WorkQueueNode *node)
{
	DeferLoop(pthread_mutex_lock(&pool->mutex), pthread_mutex_unlock(&pool->mutex))
	{
		SLLStackPush(pool->node_free_list, node);
	}
}

static void
work_queue_push(WorkerPool *pool, OS_Handle connection)
{
	WorkQueueNode *node = work_queue_node_alloc(pool);
	node->connection = connection;
	DeferLoop(pthread_mutex_lock(&pool->mutex), pthread_mutex_unlock(&pool->mutex))
	{
		SLLQueuePush(pool->queue_first, pool->queue_last, node);
	}
	semaphore_drop(pool->semaphore);
}

static OS_Handle
work_queue_pop(WorkerPool *pool)
{
	if(!semaphore_take(pool->semaphore, max_u64))
	{
		return os_handle_zero();
	}

	OS_Handle result = os_handle_zero();
	WorkQueueNode *node = 0;
	DeferLoop(pthread_mutex_lock(&pool->mutex), pthread_mutex_unlock(&pool->mutex))
	{
		if(pool->queue_first != 0)
		{
			node = pool->queue_first;
			result = node->connection;
			SLLQueuePop(pool->queue_first, pool->queue_last);
		}
	}

	if(node != 0)
	{
		work_queue_node_release(pool, node);
	}

	return result;
}

static FidAuxiliary9P *
get_fid_aux(Arena *arena, ServerFid9P *fid)
{
	if(fid->auxiliary == 0)
	{
		FidAuxiliary9P *aux = push_array(arena, FidAuxiliary9P, 1);
		fid->auxiliary = aux;
	}
	return (FidAuxiliary9P *)fid->auxiliary;
}

////////////////////////////////
//~ 9P Operation Handlers

static void
srv_version(ServerRequest9P *request)
{
	request->out_msg.max_message_size = request->in_msg.max_message_size;
	request->out_msg.protocol_version = request->in_msg.protocol_version;
	server9p_respond(request, str8_zero());
}

static void
srv_auth(ServerRequest9P *request)
{
	server9p_respond(request, str8_lit("authentication not required"));
}

static void
srv_attach(ServerRequest9P *request)
{
	Dir9P root_stat = fs9p_stat(request->scratch.arena, fs_context, str8_zero());
	if(root_stat.name.size == 0)
	{
		request->fid->qid.path = 0;
		request->fid->qid.version = 0;
		request->fid->qid.type = QidTypeFlag_Directory;
	}
	else
	{
		request->fid->qid = root_stat.qid;
	}

	request->out_msg.qid = request->fid->qid;
	server9p_respond(request, str8_zero());
}

static void
srv_walk(ServerRequest9P *request)
{
	FidAuxiliary9P *from_aux = get_fid_aux(request->server->arena, request->fid);

	if(request->in_msg.walk_name_count == 0)
	{
		FidAuxiliary9P *new_aux = get_fid_aux(request->server->arena, request->new_fid);
		new_aux->path = str8_copy(request->server->arena, from_aux->path);
		request->new_fid->qid = request->fid->qid;
		request->out_msg.walk_qid_count = 0;
		server9p_respond(request, str8_zero());
		return;
	}

	String8 current_path = from_aux->path;

	for(u64 i = 0; i < request->in_msg.walk_name_count; i += 1)
	{
		String8 name = request->in_msg.walk_names[i];

		if(str8_match(name, str8_lit("."), 0))
		{
			if(i == 0)
			{
				request->out_msg.walk_qids[i] = request->fid->qid;
			}
			else
			{
				request->out_msg.walk_qids[i] = request->out_msg.walk_qids[i - 1];
			}
			continue;
		}

		PathResolution9P res = fs9p_resolve_path(request->scratch.arena, fs_context, current_path, name);

		if(!res.valid)
		{
			if(i == 0)
			{
				if(request->new_fid != request->fid)
				{
					server9p_fid_remove(request->server, request->in_msg.new_fid);
				}
				server9p_respond(request, res.error);
				return;
			}
			else
			{
				request->out_msg.walk_qid_count = i;
				server9p_respond(request, str8_zero());
				return;
			}
		}

		Dir9P stat = fs9p_stat(request->scratch.arena, fs_context, res.absolute_path);
		if(stat.name.size == 0)
		{
			if(i == 0)
			{
				if(request->new_fid != request->fid)
				{
					server9p_fid_remove(request->server, request->in_msg.new_fid);
				}
				server9p_respond(request, str8_lit("file not found"));
				return;
			}
			else
			{
				request->out_msg.walk_qid_count = i;
				server9p_respond(request, str8_zero());
				return;
			}
		}

		request->out_msg.walk_qids[i] = stat.qid;
		current_path = res.absolute_path;
	}

	FidAuxiliary9P *new_aux = get_fid_aux(request->server->arena, request->new_fid);
	new_aux->path = str8_copy(request->server->arena, current_path);
	request->new_fid->qid = request->out_msg.walk_qids[request->in_msg.walk_name_count - 1];
	request->out_msg.walk_qid_count = request->in_msg.walk_name_count;
	server9p_respond(request, str8_zero());
}

static void
srv_open(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	FsHandle9P *handle = fs9p_open(request->server->arena, fs_context, aux->path, request->in_msg.open_mode);
	if(handle == 0 || (handle->fd < 0 && !handle->is_directory && handle->tmp_node == 0))
	{
		server9p_respond(request, str8_lit("cannot open file"));
		return;
	}

	aux->handle = handle;
	aux->open_mode = request->in_msg.open_mode;

	if(handle->is_directory)
	{
		aux->dir_iter = fs9p_opendir(request->server->arena, fs_context, aux->path);
	}

	request->out_msg.qid = request->fid->qid;
	request->out_msg.io_unit_size = P9_IOUNIT_DEFAULT;
	server9p_respond(request, str8_zero());
}

static void
srv_read(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	if(request->fid->qid.type & QidTypeFlag_Directory)
	{
		if(aux->dir_iter == 0)
		{
			aux->dir_iter = fs9p_opendir(request->server->arena, fs_context, aux->path);
		}

		if(aux->dir_iter == 0)
		{
			server9p_respond(request, str8_lit("cannot read directory"));
			return;
		}

		String8 dir_data = fs9p_readdir(request->server->arena, fs_context, aux->dir_iter, request->in_msg.file_offset,
		                                request->in_msg.byte_count);

		request->out_msg.payload_data = dir_data;
		request->out_msg.byte_count = dir_data.size;
		server9p_respond(request, str8_zero());
		return;
	}

	if(aux->handle == 0 || (aux->handle->fd < 0 && aux->handle->tmp_node == 0))
	{
		server9p_respond(request, str8_lit("file not open"));
		return;
	}

	String8 data =
	    fs9p_read(request->scratch.arena, aux->handle, request->in_msg.file_offset, request->in_msg.byte_count);

	request->out_msg.payload_data = data;
	request->out_msg.byte_count = data.size;
	server9p_respond(request, str8_zero());
}

static void
srv_stat(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	Dir9P stat = fs9p_stat(request->scratch.arena, fs_context, aux->path);
	if(stat.name.size == 0)
	{
		server9p_respond(request, str8_lit("cannot stat file"));
		return;
	}

	request->out_msg.stat_data = str8_from_dir9p(request->scratch.arena, stat);
	server9p_respond(request, str8_zero());
}

static void
srv_clunk(ServerRequest9P *request)
{
	FidAuxiliary9P *aux = get_fid_aux(request->server->arena, request->fid);

	if(aux->handle)
	{
		fs9p_close(aux->handle);
		aux->handle = 0;
	}
	if(aux->dir_iter)
	{
		fs9p_closedir(aux->dir_iter);
		aux->dir_iter = 0;
	}

	server9p_fid_remove(request->server, request->in_msg.fid);
	server9p_respond(request, str8_zero());
}

////////////////////////////////
//~ Server Loop

static void
handle_connection(OS_Handle connection_socket)
{
	Temp scratch = scratch_begin(0, 0);
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	DateTime now = os_now_universal_time();
	String8 timestamp = str8_from_datetime(scratch.arena, now);
	u64 connection_fd = connection_socket.u64[0];
	log_infof("[%S] nix-cache: connection established\n", timestamp);

	Server9P *server = server9p_alloc(scratch.arena, connection_fd, connection_fd);
	if(server == 0)
	{
		log_error(str8_lit("nix-cache: failed to allocate server\n"));
		os_file_close(connection_socket);
		return;
	}

	for(;;)
	{
		ServerRequest9P *request = server9p_get_request(server);
		if(request == 0)
		{
			break;
		}
		if(request->error.size > 0)
		{
			server9p_respond(request, request->error);
			continue;
		}

		switch(request->in_msg.type)
		{
			case Msg9P_Tversion:
				srv_version(request);
				break;
			case Msg9P_Tauth:
				srv_auth(request);
				break;
			case Msg9P_Tattach:
				srv_attach(request);
				break;
			case Msg9P_Twalk:
				srv_walk(request);
				break;
			case Msg9P_Topen:
				srv_open(request);
				break;
			case Msg9P_Tread:
				srv_read(request);
				break;
			case Msg9P_Tstat:
				srv_stat(request);
				break;
			case Msg9P_Tclunk:
				srv_clunk(request);
				break;
			default:
				server9p_respond(request, str8_lit("unsupported operation"));
				break;
		}
	}

	os_file_close(connection_socket);

	DateTime end_time = os_now_universal_time();
	String8 end_timestamp = str8_from_datetime(scratch.arena, end_time);
	log_infof("[%S] nix-cache: connection closed\n", end_timestamp);

	LogScopeResult result = log_scope_end(scratch.arena);
	if(result.strings[LogMsgKind_Info].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Info].str, 1, result.strings[LogMsgKind_Info].size, stdout);
		fflush(stdout);
	}
	if(result.strings[LogMsgKind_Error].size > 0)
	{
		fwrite(result.strings[LogMsgKind_Error].str, 1, result.strings[LogMsgKind_Error].size, stderr);
		fflush(stderr);
	}

	log_release(log);
	scratch_end(scratch);
}

////////////////////////////////
//~ Worker Thread Entry Point

static void
worker_thread_entry_point(void *ptr)
{
	WorkerPool *pool = (WorkerPool *)ptr;
	for(; pool->is_live;)
	{
		OS_Handle connection = work_queue_pop(pool);
		if(!os_handle_match(connection, os_handle_zero()))
		{
			handle_connection(connection);
		}
	}
}

////////////////////////////////
//~ Worker Pool Lifecycle

static WorkerPool *
worker_pool_alloc(Arena *arena, u64 worker_count)
{
	WorkerPool *pool = push_array(arena, WorkerPool, 1);
	pool->arena = arena_alloc();

	int mutex_result = pthread_mutex_init(&pool->mutex, 0);
	AssertAlways(mutex_result == 0);

	pool->semaphore = semaphore_alloc(0, 1024, str8_zero());
	AssertAlways(pool->semaphore.u64[0] != 0);

	pool->worker_count = worker_count;
	pool->workers = push_array(arena, Worker, worker_count);

	return pool;
}

static void
worker_pool_start(WorkerPool *pool)
{
	pool->is_live = 1;
	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		Worker *worker = &pool->workers[i];
		worker->id = i;
		worker->handle = thread_launch(worker_thread_entry_point, pool);
		AssertAlways(worker->handle.u64[0] != 0);
	}
}

static void
worker_pool_shutdown(WorkerPool *pool)
{
	pool->is_live = 0;

	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		semaphore_drop(pool->semaphore);
	}

	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		Worker *worker = &pool->workers[i];
		if(worker->handle.u64[0] != 0)
		{
			thread_join(worker->handle);
		}
	}

	semaphore_release(pool->semaphore);
	pthread_mutex_destroy(&pool->mutex);
}

////////////////////////////////
//~ Entry Point

static void
entry_point(CmdLine *cmd_line)
{
	Temp scratch = scratch_begin(0, 0);
	Arena *arena = arena_alloc();

	String8 address = str8_zero();
	String8 flake_root = str8_lit(".");
	u64 worker_count = 0;
	String8List configs = {0};

	for(CmdLineOpt *opt = cmd_line->options.first; opt != 0; opt = opt->next)
	{
		String8 option = opt->string;
		if(str8_match(option, str8_lit("flake"), 0))
		{
			if(opt->value_string.size > 0)
			{
				flake_root = opt->value_string;
			}
		}
		else if(str8_match(option, str8_lit("threads"), 0))
		{
			if(opt->value_string.size > 0)
			{
				worker_count = u64_from_str8(opt->value_string, 10);
			}
		}
		else if(str8_match(option, str8_lit("config"), 0) || str8_match(option, str8_lit("c"), 0))
		{
			if(opt->value_string.size > 0)
			{
				str8_list_push(arena, &configs, opt->value_string);
			}
		}
	}

	if(cmd_line->inputs.node_count > 0)
	{
		address = cmd_line->inputs.first->string;
	}

	if(address.size == 0)
	{
		fprintf(stderr, "usage: nix-cache [options] <address>\n"
		                "options:\n"
		                "  --flake=<path>     Flake root directory (default: current directory)\n"
		                "  --config=<name>    Configuration to build (can be specified multiple times)\n"
		                "  --threads=<n>      Number of worker threads (default: max(4, cores/4))\n"
		                "arguments:\n"
		                "  <address>          Dial string (e.g., tcp!host!port)\n");
		fflush(stderr);
		arena_release(arena);
		scratch_end(scratch);
		return;
	}

	cache_arena = arena_alloc();

	nix_store = nix_store_open();
	if(nix_store == 0)
	{
		fprintf(stderr, "nix-cache: failed to initialize Nix store\n");
		fflush(stderr);
		arena_release(arena);
		scratch_end(scratch);
		return;
	}

	fs_context = fs9p_context_alloc(arena, str8_zero(), str8_zero(), 0, StorageBackend9P_ArenaTemp);

	NixCacheBuilder builder = {0};
	builder.arena = arena;
	builder.fs_ctx = fs_context;
	builder.flake_root = flake_root;

	cache_init(&builder);

	if(configs.node_count == 0)
	{
		fprintf(stdout, "nix-cache: no configurations specified, serving empty cache\n");
		fflush(stdout);
	}
	else
	{
		for(String8Node *node = configs.first; node != 0; node = node->next)
		{
			cache_build_config(&builder, node->string);
		}
	}

	OS_Handle listen_socket = dial9p_listen(address, str8_lit("tcp"), str8_lit("nix-cache"));
	if(os_handle_match(listen_socket, os_handle_zero()))
	{
		fprintf(stderr, "nix-cache: failed to listen on address '%.*s'\n", (int)address.size, address.str);
		fflush(stderr);
		arena_release(arena);
		scratch_end(scratch);
		return;
	}

	fprintf(stdout, "nix-cache: serving Nix binary cache on %.*s\n", (int)address.size, address.str);
	fflush(stdout);

	if(worker_count == 0)
	{
		u64 logical_cores = os_get_system_info()->logical_processor_count;
		worker_count = Max(4, logical_cores / 4);
	}

	worker_pool = worker_pool_alloc(arena, worker_count);
	worker_pool_start(worker_pool);

	fprintf(stdout, "nix-cache: launched %lu worker threads\n", (unsigned long)worker_count);
	fflush(stdout);

	for(;;)
	{
		OS_Handle connection_socket = os_socket_accept(listen_socket);
		if(os_handle_match(connection_socket, os_handle_zero()))
		{
			fprintf(stderr, "nix-cache: failed to accept connection\n");
			fflush(stderr);
			continue;
		}

		DateTime accept_time = os_now_universal_time();
		String8 accept_timestamp = str8_from_datetime(scratch.arena, accept_time);
		fprintf(stdout, "[%.*s] nix-cache: accepted connection\n", (int)accept_timestamp.size, accept_timestamp.str);
		fflush(stdout);

		work_queue_push(worker_pool, connection_socket);
	}

	if(nix_store)
	{
		nix_store_close(nix_store);
	}

	arena_release(arena);
	scratch_end(scratch);
}
