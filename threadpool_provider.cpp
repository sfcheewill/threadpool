#include "threadpool_provider.h"

typedef struct threadpool_provider_impl {
    bool(*submit)(void);
    void(*submit_lost_work)(uint32_t active_threads, uint32_t threads);
    void(*close)(void);

    threadpool_worker_t worker;
    void *context;





} threadpool_provider_impl_t;