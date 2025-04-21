/* utils/ts_uvm_resp_queue.c */
#include "ts_uvm_resp_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

ThreadSafeUvmRespQueue* uvq_create(size_t capacity) {
    if (capacity == 0) { /*...*/ return NULL; }
    ThreadSafeUvmRespQueue *queue = malloc(sizeof(ThreadSafeUvmRespQueue));
    if (!queue) { /*...*/ return NULL; }
    queue->buffer = malloc(capacity * sizeof(UvmResponseMessage));
    if (!queue->buffer) { /*...*/ free(queue); return NULL; }
    queue->capacity = capacity; queue->count = 0; queue->head = 0; queue->tail = 0; queue->shutdown = false;
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) { /*...*/ free(queue->buffer); free(queue); return NULL; }
    if (pthread_cond_init(&queue->cond_not_empty, NULL) != 0) { /*...*/ pthread_mutex_destroy(&queue->mutex); free(queue->buffer); free(queue); return NULL; }
    if (pthread_cond_init(&queue->cond_not_full, NULL) != 0) { /*...*/ pthread_cond_destroy(&queue->cond_not_empty); pthread_mutex_destroy(&queue->mutex); free(queue->buffer); free(queue); return NULL; }
    printf("Thread-safe UVM Response queue created with capacity %zu\n", capacity);
    return queue;
}

void uvq_destroy(ThreadSafeUvmRespQueue *queue) {
    if (!queue) return;
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    free(queue->buffer); free(queue);
    printf("Thread-safe UVM Response queue destroyed\n");
}

bool uvq_enqueue(ThreadSafeUvmRespQueue *queue, const UvmResponseMessage *resp_message) {
    if (!queue || !resp_message) return false;
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == queue->capacity && !queue->shutdown) { pthread_cond_wait(&queue->cond_not_full, &queue->mutex); }
    if (queue->shutdown) { pthread_mutex_unlock(&queue->mutex); return false; }
    memcpy(&queue->buffer[queue->head], resp_message, sizeof(UvmResponseMessage));
    queue->head = (queue->head + 1) % queue->capacity; queue->count++;
    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex); return true;
}

bool uvq_dequeue(ThreadSafeUvmRespQueue *queue, UvmResponseMessage *resp_message) {
     if (!queue || !resp_message) return false;
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->shutdown) { pthread_cond_wait(&queue->cond_not_empty, &queue->mutex); }
    if (queue->shutdown && queue->count == 0) { pthread_mutex_unlock(&queue->mutex); return false; }
    memcpy(resp_message, &queue->buffer[queue->tail], sizeof(UvmResponseMessage));
    queue->tail = (queue->tail + 1) % queue->capacity; queue->count--;
    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex); return true;
}

void uvq_shutdown(ThreadSafeUvmRespQueue *queue) {
    if (!queue) return;
    pthread_mutex_lock(&queue->mutex);
    if (!queue->shutdown) {
       queue->shutdown = true;
       printf("Thread-safe UVM Response queue shutdown initiated.\n");
       pthread_cond_broadcast(&queue->cond_not_empty);
       pthread_cond_broadcast(&queue->cond_not_full);
    }
    pthread_mutex_unlock(&queue->mutex);
}