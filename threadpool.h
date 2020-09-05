#pragma once

#include <stdint.h>
#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct threadpool threadpool_t;

typedef int (*threadpool_shutdown_t)(void*);

typedef struct threadpool_item {
    struct list_entry link;
    void (*action)(struct threadpool_item *);
    void (*release)(struct threadpool_item *);
} threadpool_item_t;

typedef enum threadpool_priority {
    THREAD_POOL_PRIORITY_NONE = 0x0,
    THREAD_POOL_PRIORITY_LOW = 0x1,
    THREAD_POOL_PRIORITY_NORMAL = 0x5,
    THREAD_POOL_PRIORITY_HIGH = 0x9
} threadpool_priority_t;


int threadpool_create(threadpool_t **ppool,
                      unsigned int threads,
                      threadpool_shutdown_t shutdown_handler,
                      void *shutdown_handler_context);

int threadpool_submit(threadpool_t *pool, 
                      threadpool_item_t *item, 
                      threadpool_priority_t priority);

void threadpool_keepAlive(threadpool_t *pool);

bool threadpool_cancel(threadpool_t *pool, threadpool_item_t *item);

void threadpool_shutdown(threadpool_t *pool, bool abortive);

void threadpool_close(threadpool_t *pool);

uint32_t threadpool_size(threadpool_t *pool);

uint32_t threadpool_thread_count(threadpool_t *pool);

uint32_t threadpool_active_thread_count(threadpool_t *pool);

#ifdef __cplusplus
}
#endif