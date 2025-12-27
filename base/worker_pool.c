////////////////////////////////
//~ Task Node Freelist

internal WP_Task *
wp_task_alloc(WP_Pool *pool)
{
	WP_Task *task = 0;
	MutexScope(pool->mutex)
	{
		task = pool->free_list;
		if(task != 0)
		{
			SLLStackPop(pool->free_list);
		}
		else
		{
			task = push_array_no_zero(pool->arena, WP_Task, 1);
		}
	}
	MemoryZeroStruct(task);
	return task;
}

internal void
wp_task_release(WP_Pool *pool, WP_Task *task)
{
	MutexScope(pool->mutex) { SLLStackPush(pool->free_list, task); }
}

////////////////////////////////
//~ Task Queue Operations

internal WP_Task *
wp_task_pop(WP_Pool *pool)
{
	if(!semaphore_take(pool->semaphore, max_u64))
	{
		return 0;
	}

	WP_Task *task = 0;
	MutexScope(pool->mutex)
	{
		if(pool->queue_first != 0)
		{
			task = pool->queue_first;
			SLLQueuePop(pool->queue_first, pool->queue_last);
		}
	}

	return task;
}

internal void
wp_submit(WP_Pool *pool, WP_TaskFunc *func, void *params, u64 params_size)
{
	AssertAlways(params_size <= sizeof(((WP_Task *)0)->params));

	log_infof("worker_pool: wp_submit called pool=%p func=%p params_size=%llu\n", pool, func, params_size);

	WP_Task *task = wp_task_alloc(pool);
	task->func = func;
	MemoryCopy(task->params, params, params_size);

	log_infof("worker_pool: wp_submit task allocated task=%p\n", task);

	MutexScope(pool->mutex) { SLLQueuePush(pool->queue_first, pool->queue_last, task); }

	log_infof("worker_pool: wp_submit about to drop semaphore\n");
	semaphore_drop(pool->semaphore);
	log_infof("worker_pool: wp_submit semaphore dropped\n");
}

////////////////////////////////
//~ Worker Thread

internal void
wp_worker_entry_point(void *ptr)
{
	WP_Worker *worker = (WP_Worker *)ptr;
	WP_Pool *pool = worker->pool;

	// Set up logging for this worker thread
	Log *log = log_alloc();
	log_select(log);
	log_scope_begin();

	log_infof("worker_pool: thread %llu started\n", worker->id);
	for(; pool->is_live;)
	{
		log_infof("worker_pool: thread %llu waiting for task\n", worker->id);
		WP_Task *task = wp_task_pop(pool);
		log_infof("worker_pool: thread %llu got task=%p\n", worker->id, task);
		if(task != 0)
		{
			log_infof("worker_pool: thread %llu executing task func=%p\n", worker->id, task->func);
			if(task->func != 0)
			{
				task->func(task->params);
			}
			log_infof("worker_pool: thread %llu task complete\n", worker->id);
			wp_task_release(pool, task);
		}
	}
	log_infof("worker_pool: thread %llu exiting\n", worker->id);

	// Flush and release the log
	Temp scratch = scratch_begin(0, 0);
	log_scope_flush(scratch.arena);
	scratch_end(scratch);
	log_release(log);
}

////////////////////////////////
//~ Pool Lifecycle

internal WP_Pool *
wp_pool_alloc(Arena *arena, u64 worker_count)
{
	log_infof("worker_pool: wp_pool_alloc called with worker_count=%llu\n", worker_count);

	WP_Pool *pool = push_array(arena, WP_Pool, 1);
	pool->arena = arena_alloc();
	pool->mutex = mutex_alloc();
	pool->semaphore = semaphore_alloc(0, 1024, str8_zero());
	pool->workers = push_array(arena, WP_Worker, worker_count);
	pool->worker_count = worker_count;
	pool->is_live = 1;

	AssertAlways(pool->mutex.u64[0] != 0);
	AssertAlways(pool->semaphore.u64[0] != 0);

	log_infof("worker_pool: launching %llu worker threads\n", worker_count);

	for(u64 i = 0; i < worker_count; i += 1)
	{
		WP_Worker *worker = &pool->workers[i];
		worker->id = i;
		worker->pool = pool;
		log_infof("worker_pool: launching thread %llu\n", i);
		worker->handle = thread_launch(wp_worker_entry_point, worker);
		AssertAlways(worker->handle.u64[0] != 0);
		log_infof("worker_pool: thread %llu launched handle=%llu\n", i, worker->handle.u64[0]);
	}

	log_infof("worker_pool: all threads launched\n");

	return pool;
}

internal void
wp_pool_release(WP_Pool *pool)
{
	pool->is_live = 0;

	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		semaphore_drop(pool->semaphore);
	}

	for(u64 i = 0; i < pool->worker_count; i += 1)
	{
		WP_Worker *worker = &pool->workers[i];
		if(worker->handle.u64[0] != 0)
		{
			thread_join(worker->handle);
		}
	}

	semaphore_release(pool->semaphore);
	mutex_release(pool->mutex);

	MemoryZeroStruct(pool);
}
