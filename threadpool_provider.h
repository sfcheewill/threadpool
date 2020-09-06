#pragma once
#include <stdint.h>

typedef void (*threadpool_worker_t)(void *context);

typedef struct threadpool_provider {
    bool(*submit)(void);
    void(*submit_lost_work)(uint32_t active_threads, uint32_t threads);
    void(*close)(void);
} threadpool_provider_t;

