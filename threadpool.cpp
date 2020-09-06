#include "threadpool.h"
#include "threadpool_provider.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#define THREAD_POOL_KEEP_ALIVE_TIMEOUT    (150000)

typedef enum shutdown_state {
    SHUTDOWN_STATE_NONE = 0x0,
    SHUTDOWN_STATE_WAIT,
    SHUTDOWN_STATE_ABORT,
    SHUTDOWN_STATE_COMPLETE
} shutdown_state_t;

typedef struct threadpool_queue {
    struct list_entry queue_normal;
    struct list_entry queue_low;
    struct list_entry queue_high;
    unsigned int size;
} threadpool_queue_t;

struct threadpool {
    pthread_mutex_t mutex;
    threadpool_queue_t queue;
    threadpool_provider_t *provider;
    uint32_t threads;
    uint32_t active_threads;
    uint32_t max_threads;
    shutdown_state_t shutdown_state;
    bool shutdown_handler_called;
    uint64_t last_tick_count;
    threadpool_shutdown_t shutdown_handler;
    void *shutdown_handler_context;
};

bool threadpool_queue_isvalid(threadpool_queue_t *queue)
{
    if (!list_empty(&queue->queue_normal) ||
        !list_empty(&queue->queue_low) ||
        !list_empty(&queue->queue_high)) {

        return (queue->size != 0);

    } else {

        return (queue->size == 0);
    }
}

bool threadpool_queue_isempty(threadpool_queue_t *queue)
{
    assert(threadpool_queue_isvalid(queue));

    return (queue->size == 0);
}

void threadpool_queue_push(threadpool_queue_t *queue,
                           threadpool_item_t *item,
                           threadpool_priority_t priority)
{
    if (!list_empty(&item->link)) {
        assert(false);
    } else {
        if (priority == THREAD_POOL_PRIORITY_LOW) {
            list_insert_tail(&queue->queue_low, &item->link);
        } else if (priority == THREAD_POOL_PRIORITY_HIGH) {
            list_insert_tail(&queue->queue_high, &item->link);
        } else {
            list_insert_tail(&queue->queue_normal, &item->link);
        }
        ++queue->size;
        assert(threadpool_queue_isvalid(queue));
    }
}

threadpool_item_t *threadpool_queue_pop(threadpool_queue_t *queue)
{
    struct list_entry *entry = nullptr;;
    threadpool_item_t *item = nullptr;

    assert(threadpool_queue_isvalid(queue));

    if (!queue->size) {
        return nullptr;
    }

    if (!list_empty(&queue->queue_high)) {
        entry = list_remove_head(&queue->queue_high);
    } else if (!list_empty(&queue->queue_normal)) {
        entry = list_remove_head(&queue->queue_normal);
    } else if (!list_empty(&queue->queue_low)) {
        entry = list_remove_head(&queue->queue_low);
    }

    if (!entry) {
        assert(false);
    } else {
        --queue->size;
        item = container_of(entry, threadpool_item_t, link);
        list_init(&item->link);
        assert(threadpool_queue_isvalid(queue));
    }
    return item;
}

bool threadpool_queue_remove(threadpool_queue_t *queue,
                             threadpool_item_t *item)
{
    if (!list_empty(&item->link)) {
        return false;
    }

    if (threadpool_queue_isempty(queue)) {
        assert(false);
        return false;
    } else {
        list_remove_entry(&item->link);
        list_init(&item->link);
        if (queue->size > 0) {
            --queue->size;
        }
        assert(threadpool_queue_isvalid(queue));
        return true;
    }
}

uint64_t threadpool_get_tickcount(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}


void threadpool_keepalive_locked(threadpool_t *pool)
{
    if (pool->active_threads < pool->threads) {

        uint64_t tick_count = threadpool_get_tickcount();

        if (tick_count - pool->last_tick_count >= THREAD_POOL_KEEP_ALIVE_TIMEOUT) {

            pool->last_tick_count = tick_count;

            pool->provider->submit_lost_work(pool->active_threads,
                                             pool->threads);
        }
    }
}

void thread_pool_async_dequeue(threadpool_t *pool)
{
    pthread_mutex_lock(&pool->mutex);

    pool->last_tick_count = threadpool_get_tickcount();

    if (pool->active_threads >= pool->threads) {
        pthread_mutex_unlock(&pool->mutex);
        return;
    }

    pool->active_threads++;

    if (pool->active_threads > pool->threads) {
        pool->threads--;
        pool->active_threads--;
        pthread_mutex_unlock(&pool->mutex);
        return;
    }

    while (true) {

        threadpool_item_t *item;
        shutdown_state_t shutdown_state;

        assert(pool->threads >= 1);
        assert(pool->threads >= pool->active_threads);
        assert(pool->shutdown_state != SHUTDOWN_STATE_COMPLETE);

        shutdown_state = pool->shutdown_state;

        item = threadpool_queue_pop(&pool->queue);
        if (!item) {
            break;
        }

        pthread_mutex_unlock(&pool->mutex);

        if (shutdown_state != SHUTDOWN_STATE_ABORT) {
            item->action(item);
        }

        item->release(item);

        pthread_mutex_lock(&pool->mutex);
    }

    --pool->threads;
    --pool->active_threads;

    if ((pool->shutdown_state == SHUTDOWN_STATE_NONE) ||
        pool->active_threads || pool->shutdown_handler_called) {

        pthread_mutex_unlock(&pool->mutex);

    } else {
        assert(threadpool_queue_isempty(&pool->queue));

        pool->shutdown_handler_called = true;

        pthread_mutex_unlock(&pool->mutex);

        if (pool->shutdown_handler) {
            pool->shutdown_handler(pool->shutdown_handler_context);
        }

    }
}


int threadpool_create(threadpool_t **ppool,
                      uint32_t max_threads, 
                      threadpool_shutdown_t shutdown_handler,
                      void *shutdown_handler_context)
{
    int ret;
    threadpool_t *pool;

    if (!ppool || !max_threads) {
        return EINVAL;
    }

    *ppool = nullptr;

    pool = (threadpool_t*)malloc(sizeof(threadpool_t));
    if (!pool) {
        return ENOMEM;
    }

    memset(pool, 0, sizeof(threadpool_t));
    ret = pthread_mutex_init(&pool->mutex, nullptr);
    if (ret != 0) {
        free(pool);
        return ret;
    }

    pool->max_threads = max_threads;
    pool->shutdown_handler = shutdown_handler;
    pool->shutdown_handler_context = shutdown_handler_context;

    *ppool = pool;

    return ret;
}


int threadpool_submit(threadpool_t *pool, 
                      threadpool_item_t *item, 
                      threadpool_priority_t priority)
{
    int ret;

    assert(item->action);
    assert(item->release);

    pthread_mutex_lock(&pool->mutex);

    if (pool->shutdown_state != SHUTDOWN_STATE_NONE) {

        item->release(item);

        ret = ECANCELED;

    } else {

        threadpool_keepalive_locked(pool);

        threadpool_queue_push(&pool->queue, item, priority);

        if (pool->threads >= pool->max_threads) {

            ret = 0;

        } else {

            ++pool->threads;

            pool->last_tick_count = threadpool_get_tickcount();

            pthread_mutex_unlock(&pool->mutex);

            if (!pool->provider->submit()) {
                ret = ECANCELED;
            } else {
                ret = 0;
            }

            return ret;
        }
    }

    pthread_mutex_unlock(&pool->mutex);
    
    return ret;
}

int threadpool_submitex(threadpool_t *pool, threadpool_item_t *item)
{
    return threadpool_submit(pool, item, THREAD_POOL_PRIORITY_NORMAL);
}

void threadpool_keepalive(threadpool_t *pool)
{
    pthread_mutex_lock(&pool->mutex);
    threadpool_keepalive_locked(pool);
    pthread_mutex_unlock(&pool->mutex);
}

bool threadpool_cancel(threadpool_t *pool, threadpool_item_t *item)
{
    bool ret;
    pthread_mutex_lock(&pool->mutex);
    ret = threadpool_queue_remove(&pool->queue, item);
    pthread_mutex_unlock(&pool->mutex);
    if (!ret) {
        return false;
    } else {
        item->release(item);
        return true;
    }
}

void threadpool_shutdown(threadpool_t *pool, bool abortive)
{
    pthread_mutex_lock(&pool->mutex);

    if (pool->shutdown_state != SHUTDOWN_STATE_NONE) {
        pthread_mutex_unlock(&pool->mutex);
        assert(false);
        return;
    }

    pool->shutdown_state =
        (abortive != false) ? SHUTDOWN_STATE_ABORT : SHUTDOWN_STATE_WAIT;

    if ((pool->threads > 0) || (pool->active_threads) > 0) {

        threadpool_keepalive(pool);

        pthread_mutex_unlock(&pool->mutex);

    } else {

        assert(threadpool_queue_isempty(&pool->queue));

        assert(!pool->shutdown_handler_called);

        pool->shutdown_handler_called = true;

        pthread_mutex_unlock(&pool->mutex);

        if (pool->shutdown_handler != nullptr) {
            pool->shutdown_handler(pool->shutdown_handler_context);
        }
    }
}

void threadpool_close(threadpool_t *pool)
{
    pthread_mutex_lock(&pool->mutex);

    if (pool->shutdown_state == SHUTDOWN_STATE_NONE) {
        pool->shutdown_state = SHUTDOWN_STATE_ABORT;
    }

    assert(pool->shutdown_state != SHUTDOWN_STATE_COMPLETE);

    ++pool->threads;

    pthread_mutex_unlock(&pool->mutex);

    thread_pool_async_dequeue(pool);

    pool->provider->close();

    assert(threadpool_queue_isempty(&pool->queue));
    assert(pool->active_threads == 0);

    pool->shutdown_state = SHUTDOWN_STATE_COMPLETE;
}

uint32_t threadpool_size(threadpool_t *pool)
{
    return pool->max_threads;
}

uint32_t threadpool_thread_count(threadpool_t *pool)
{
    return pool->threads;
}

uint32_t threadpool_active_thread_count(threadpool_t *pool)
{
    return pool->active_threads;
}
