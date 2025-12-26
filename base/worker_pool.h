#ifndef WORKER_POOL_H
#define WORKER_POOL_H

////////////////////////////////
//~ Worker Pool Types

typedef void WP_TaskFunc(void *params);

typedef struct WP_Worker WP_Worker;
struct WP_Worker
{
	u64 id;
	struct WP_Pool *pool;
	Thread handle;
};

typedef struct WP_Task WP_Task;
struct WP_Task
{
	WP_Task *next;
	WP_TaskFunc *func;
	u64 params[2];
};

typedef struct WP_Pool WP_Pool;
struct WP_Pool
{
	b32 is_live;
	Semaphore semaphore;
	Mutex mutex;
	Arena *arena;
	WP_Task *queue_first;
	WP_Task *queue_last;
	WP_Task *free_list;
	WP_Worker *workers;
	u64 worker_count;
};

////////////////////////////////
//~ Worker Pool Functions

internal WP_Pool *wp_pool_alloc(Arena *arena, u64 worker_count);
internal void wp_pool_release(WP_Pool *pool);
internal void wp_submit(WP_Pool *pool, WP_TaskFunc *func, void *params, u64 params_size);

#endif // WORKER_POOL_H
