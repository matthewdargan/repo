////////////////////////////////
//~ Globals

global Arena *thread_arena = 0;
global pthread_mutex_t thread_mutex;
global ThreadState *thread_state_free = 0;

////////////////////////////////
//~ Thread Functions

internal void
thread_init(void)
{
  thread_arena = arena_alloc();
  pthread_mutex_init(&thread_mutex, 0);
}

internal ThreadState *
thread_state_alloc(void)
{
  ThreadState *state = 0;
  DeferLoop(pthread_mutex_lock(&thread_mutex), pthread_mutex_unlock(&thread_mutex))
  {
    state = thread_state_free;
    if(state)
    {
      SLLStackPop(thread_state_free);
    }
    else
    {
      state = push_array_no_zero(thread_arena, ThreadState, 1);
    }
  }
  MemoryZeroStruct(state);
  return state;
}

internal void
thread_state_release(ThreadState *state)
{
  DeferLoop(pthread_mutex_lock(&thread_mutex), pthread_mutex_unlock(&thread_mutex))
  {
    SLLStackPush(thread_state_free, state);
  }
}

internal void *
thread_entry_point(void *ptr)
{
  ThreadState *state = (ThreadState *)ptr;
  ThreadEntryPointFunctionType *func = state->func;
  void *thread_ptr = state->ptr;
  supplement_thread_base_entry_point(func, thread_ptr);
  return 0;
}

internal Thread
thread_launch(ThreadEntryPointFunctionType *func, void *ptr)
{
  ThreadState *state = thread_state_alloc();
  state->func = func;
  state->ptr = ptr;
  {
    int pthread_result = pthread_create(&state->handle, 0, thread_entry_point, state);
    if(pthread_result == -1)
    {
      thread_state_release(state);
      state = 0;
    }
  }
  Thread thread = {(u64)state};
  return thread;
}

internal b32
thread_join(Thread thread)
{
  if(MemoryIsZeroStruct(&thread))
  {
    return 0;
  }

  ThreadState *state = (ThreadState *)thread.u64[0];
  int join_result = pthread_join(state->handle, 0);
  b32 result = (join_result == 0);
  thread_state_release(state);

  return result;
}

internal void
thread_detach(Thread thread)
{
  if(MemoryIsZeroStruct(&thread))
  {
    return;
  }

  ThreadState *state = (ThreadState *)thread.u64[0];
  pthread_detach(state->handle);
  thread_state_release(state);
}

////////////////////////////////
//~ Synchronization Primitive Functions

//- mutexes
internal Mutex
mutex_alloc(void)
{
  pthread_mutex_t *mutex =
      (pthread_mutex_t *)mmap(0, sizeof(*mutex), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  AssertAlways(mutex != MAP_FAILED);

  int err = pthread_mutex_init(mutex, 0);
  AssertAlways(err == 0);

  Mutex result = {(u64)mutex};
  return result;
}

internal void
mutex_release(Mutex mutex)
{
  if(MemoryIsZeroStruct(&mutex))
  {
    return;
  }
  pthread_mutex_t *m = (pthread_mutex_t *)mutex.u64[0];
  pthread_mutex_destroy(m);
  int err = munmap(m, sizeof(*m));
  AssertAlways(err == 0);
}

internal void
mutex_take(Mutex mutex)
{
  if(MemoryIsZeroStruct(&mutex))
  {
    return;
  }
  pthread_mutex_t *m = (pthread_mutex_t *)mutex.u64[0];
  pthread_mutex_lock(m);
}

internal void
mutex_drop(Mutex mutex)
{
  if(MemoryIsZeroStruct(&mutex))
  {
    return;
  }
  pthread_mutex_t *m = (pthread_mutex_t *)mutex.u64[0];
  pthread_mutex_unlock(m);
}

//- reader/writer mutexes
internal RWMutex
rw_mutex_alloc(void)
{
  pthread_rwlock_t *rwlock =
      (pthread_rwlock_t *)mmap(0, sizeof(*rwlock), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  AssertAlways(rwlock != MAP_FAILED);

  int err = pthread_rwlock_init(rwlock, 0);
  AssertAlways(err == 0);

  RWMutex result = {(u64)rwlock};
  return result;
}

internal void
rw_mutex_release(RWMutex mutex)
{
  if(MemoryIsZeroStruct(&mutex))
  {
    return;
  }
  pthread_rwlock_t *rwlock = (pthread_rwlock_t *)mutex.u64[0];
  pthread_rwlock_destroy(rwlock);
  int err = munmap(rwlock, sizeof(*rwlock));
  AssertAlways(err == 0);
}

internal void
rw_mutex_take(RWMutex mutex, b32 write_mode)
{
  if(MemoryIsZeroStruct(&mutex))
  {
    return;
  }
  pthread_rwlock_t *rwlock = (pthread_rwlock_t *)mutex.u64[0];
  if(write_mode)
  {
    pthread_rwlock_wrlock(rwlock);
  }
  else
  {
    pthread_rwlock_rdlock(rwlock);
  }
}

internal void
rw_mutex_drop(RWMutex mutex)
{
  if(MemoryIsZeroStruct(&mutex))
  {
    return;
  }
  pthread_rwlock_t *rwlock = (pthread_rwlock_t *)mutex.u64[0];
  pthread_rwlock_unlock(rwlock);
}

//- cross-process semaphores
internal Semaphore
semaphore_alloc(u32 initial_count, String8 name)
{
  Semaphore result = {0};
  if(name.size > 0)
  {
    for(u64 attempt_idx = 0; attempt_idx < 64; attempt_idx += 1)
    {
      sem_t *s = sem_open((char *)name.str, O_CREAT | O_EXCL, 0666, initial_count);
      if(s == SEM_FAILED)
      {
        s = sem_open((char *)name.str, 0);
      }
      if(s != SEM_FAILED)
      {
        result.u64[0] = (u64)s;
        break;
      }
    }
  }
  else
  {
    sem_t *s = (sem_t *)mmap(0, sizeof(*s), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    AssertAlways(s != MAP_FAILED);
    int err = sem_init(s, 0, initial_count);
    if(err == 0)
    {
      result.u64[0] = (u64)s;
    }
  }
  return result;
}

internal void
semaphore_release(Semaphore semaphore)
{
  int err = munmap((void *)semaphore.u64[0], sizeof(sem_t));
  AssertAlways(err == 0);
}

internal b32
semaphore_take(Semaphore semaphore, u64 timeout_us)
{
  b32 result = 0;
  if(timeout_us == max_u64)
  {
    for(;;)
    {
      int err = sem_wait((sem_t *)semaphore.u64[0]);
      if(err == 0)
      {
        result = 1;
        break;
      }
      else if(errno == EAGAIN)
      {
        continue;
      }
      break;
    }
  }
  else
  {
    u64 now_us = os_now_microseconds();
    u64 end_us = now_us + timeout_us;

    u64 end_sec = end_us / Million(1);
    u64 end_nsec = (end_us % Million(1)) * Thousand(1);

    struct timespec abs_timeout;
    abs_timeout.tv_sec = end_sec;
    abs_timeout.tv_nsec = end_nsec;

    for(;;)
    {
      int err = sem_clockwait((sem_t *)semaphore.u64[0], CLOCK_MONOTONIC, &abs_timeout);
      if(err == 0)
      {
        result = 1;
        break;
      }
      else if(errno == ETIMEDOUT)
      {
        result = 0;
        break;
      }
      else if(errno == EAGAIN)
      {
        continue;
      }
      break;
    }
  }
  return result;
}

internal void
semaphore_drop(Semaphore semaphore)
{
  for(;;)
  {
    int err = sem_post((sem_t *)semaphore.u64[0]);
    if(err == 0)
    {
      break;
    }
    else
    {
      if(errno == EAGAIN)
      {
        continue;
      }
      break;
    }
  }
}
