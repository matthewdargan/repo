#ifndef THREAD_H
#define THREAD_H

////////////////////////////////
//~ Includes

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <time.h>

////////////////////////////////
//~ Thread Types

typedef struct Thread Thread;
struct Thread
{
	u64 u64[1];
};

typedef void ThreadEntryPointFunctionType(void *p);

typedef struct ThreadState ThreadState;
struct ThreadState
{
	ThreadState *next;
	pthread_t handle;
	ThreadEntryPointFunctionType *func;
	void *ptr;
};

////////////////////////////////
//~ Synchronization Primitive Types

typedef struct Semaphore Semaphore;
struct Semaphore
{
	u64 u64[1];
};

////////////////////////////////
//~ Thread Functions

internal void thread_init(void);
internal ThreadState *thread_state_alloc(void);
internal void thread_state_release(ThreadState *state);
internal void *thread_entry_point(void *ptr);
internal void thread_init(void);
internal Thread thread_launch(ThreadEntryPointFunctionType *func, void *ptr);
internal b32 thread_join(Thread thread);
internal void thread_detach(Thread thread);

////////////////////////////////
//~ Synchronization Primitive Functions

internal Semaphore semaphore_alloc(u32 initial_count, u32 max_count, String8 name);
internal void semaphore_release(Semaphore semaphore);
internal b32 semaphore_take(Semaphore semaphore, u64 timeout_us);
internal void semaphore_drop(Semaphore semaphore);

#endif // THREAD_H
