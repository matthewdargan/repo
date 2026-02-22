////////////////////////////////
//~ Task Node Freelist

internal WP_Task *
wp_task_alloc(WP_Pool *pool)
{
  WP_Task *task = 0;
  MutexScope(pool->mutex)
  {
    task = pool->free_list;
    if(task != 0) { SLLStackPop(pool->free_list); }
    else          { task = push_array_no_zero(pool->arena, WP_Task, 1); }
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
  if(!semaphore_take(pool->semaphore, max_u64)) { return 0; }

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

  WP_Task *task = wp_task_alloc(pool);
  task->func = func;
  MemoryCopy(task->params, params, params_size);

  MutexScope(pool->mutex) { SLLQueuePush(pool->queue_first, pool->queue_last, task); }
  semaphore_drop(pool->semaphore);
}

////////////////////////////////
//~ Worker Thread

internal void
wp_worker_entry_point(void *ptr)
{
  WP_Worker *worker = (WP_Worker *)ptr;
  WP_Pool *pool     = worker->pool;

  for(; pool->is_live;)
  {
    WP_Task *task = wp_task_pop(pool);
    if(task != 0)
    {
      if(task->func != 0) { task->func(task->params); }
      wp_task_release(pool, task);
    }
  }
}

////////////////////////////////
//~ Pool Lifecycle

internal WP_Pool *
wp_pool_alloc(Arena *arena, u64 worker_count)
{
  WP_Pool *pool      = push_array(arena, WP_Pool, 1);
  pool->arena        = arena_alloc();
  pool->mutex        = mutex_alloc();
  pool->semaphore    = semaphore_alloc(0, str8_zero());
  pool->workers      = push_array(arena, WP_Worker, worker_count);
  pool->worker_count = worker_count;
  pool->is_live      = 1;

  AssertAlways(pool->mutex.u64[0] != 0);
  AssertAlways(pool->semaphore.u64[0] != 0);

  for(u64 i = 0; i < worker_count; i += 1)
  {
    WP_Worker *worker = &pool->workers[i];
    worker->id        = i;
    worker->pool      = pool;
    worker->handle    = thread_launch(wp_worker_entry_point, worker);
    AssertAlways(worker->handle.u64[0] != 0);
  }

  return pool;
}

internal void
wp_pool_release(WP_Pool *pool)
{
  pool->is_live = 0;

  for(u64 i = 0; i < pool->worker_count; i += 1) { semaphore_drop(pool->semaphore); }
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
