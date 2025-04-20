/*
 * utils/ts_queued_msg_queue.c
 *
 * Описание:
 * Реализация потокобезопасной очереди для QueuedMessage.
 */

#include "ts_queued_msg_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

ThreadSafeQueuedMsgQueue* qmq_create(size_t capacity) {
    if (capacity == 0) {
        fprintf(stderr, "qmq_create: Capacity must be greater than 0\n");
        return NULL;
    }
    ThreadSafeQueuedMsgQueue *queue = (ThreadSafeQueuedMsgQueue*)malloc(sizeof(ThreadSafeQueuedMsgQueue));
    if (!queue) {
        perror("qmq_create: Failed to allocate queue structure");
        return NULL;
    }
    queue->buffer = (QueuedMessage*)malloc(capacity * sizeof(QueuedMessage));
    if (!queue->buffer) {
        perror("qmq_create: Failed to allocate queue buffer");
        free(queue);
        return NULL;
    }
    queue->capacity = capacity;
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->shutdown = false;
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) { /* ... error handling ... */ free(queue->buffer); free(queue); return NULL; }
    if (pthread_cond_init(&queue->cond_not_empty, NULL) != 0) { /* ... error handling ... */ pthread_mutex_destroy(&queue->mutex); free(queue->buffer); free(queue); return NULL; }
    if (pthread_cond_init(&queue->cond_not_full, NULL) != 0) { /* ... error handling ... */ pthread_cond_destroy(&queue->cond_not_empty); pthread_mutex_destroy(&queue->mutex); free(queue->buffer); free(queue); return NULL; }
    printf("Thread-safe QueuedMessage queue created with capacity %zu\n", capacity);
    return queue;
}

void qmq_destroy(ThreadSafeQueuedMsgQueue *queue) {
    if (!queue) return;
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    free(queue->buffer);
    free(queue);
    printf("Thread-safe QueuedMessage queue destroyed\n");
}

bool qmq_enqueue(ThreadSafeQueuedMsgQueue *queue, const QueuedMessage *queued_message) {
    if (!queue || !queued_message) return false;
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    memcpy(&queue->buffer[queue->head], queued_message, sizeof(QueuedMessage));
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;
    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

bool qmq_dequeue(ThreadSafeQueuedMsgQueue *queue, QueuedMessage *queued_message) {
     if (!queue || !queued_message) return false;
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->cond_not_empty, &queue->mutex);
    }
    if (queue->shutdown && queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    memcpy(queued_message, &queue->buffer[queue->tail], sizeof(QueuedMessage));
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;
    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

void qmq_shutdown(ThreadSafeQueuedMsgQueue *queue) {
    if (!queue) return;
    pthread_mutex_lock(&queue->mutex);
    if (!queue->shutdown) {
       queue->shutdown = true;
       printf("Thread-safe QueuedMessage queue shutdown initiated.\n");
       pthread_cond_broadcast(&queue->cond_not_empty);
       pthread_cond_broadcast(&queue->cond_not_full);
    }
    pthread_mutex_unlock(&queue->mutex);
}