/* utils/ts_uvm_resp_queue.h */
#ifndef TS_UVM_RESP_QUEUE_H
#define TS_UVM_RESP_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include "../uvm/uvm_types.h" // Для UvmResponseMessage
#include "ts_uvm_resp_queue_fwd.h"

struct ThreadSafeUvmRespQueue {
    UvmResponseMessage *buffer;
    size_t capacity;
    size_t count;
    size_t head;
    size_t tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
    bool shutdown;
};

// Функции с префиксом uvq_
ThreadSafeUvmRespQueue* uvq_create(size_t capacity);
void uvq_destroy(ThreadSafeUvmRespQueue *queue);
bool uvq_enqueue(ThreadSafeUvmRespQueue *queue, const UvmResponseMessage *resp_message);
bool uvq_dequeue(ThreadSafeUvmRespQueue *queue, UvmResponseMessage *resp_message);
void uvq_shutdown(ThreadSafeUvmRespQueue *queue);

#endif // TS_UVM_RESP_QUEUE_H