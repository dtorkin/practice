/*
 * utils/ts_queue_req.c
 *
 * Описание:
 * Реализация потокобезопасной очереди для UvmRequest.
 */

#include "ts_queue_req.h" // <--- Включаем новый .h
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // Для memcpy

// Все имена функций и тип структуры заменены на _req
ThreadSafeReqQueue* queue_req_create(size_t capacity) {
    if (capacity == 0) { /* ... */ return NULL; }

    ThreadSafeReqQueue *queue = (ThreadSafeReqQueue*)malloc(sizeof(ThreadSafeReqQueue));
    if (!queue) { /* ... */ return NULL; }

    // Выделяем память под UvmRequest
    queue->buffer = (UvmRequest*)malloc(capacity * sizeof(UvmRequest));
    if (!queue->buffer) { /* ... */ free(queue); return NULL; }

    queue->capacity = capacity;
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->shutdown = false;

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) { /* ... */ free(queue->buffer); free(queue); return NULL; }
    if (pthread_cond_init(&queue->cond_not_empty, NULL) != 0) { /* ... */ pthread_mutex_destroy(&queue->mutex); free(queue->buffer); free(queue); return NULL; }
    if (pthread_cond_init(&queue->cond_not_full, NULL) != 0) { /* ... */ pthread_cond_destroy(&queue->cond_not_empty); pthread_mutex_destroy(&queue->mutex); free(queue->buffer); free(queue); return NULL; }

    printf("Thread-safe UVM request queue created with capacity %zu\n", capacity);
    return queue;
}

void queue_req_destroy(ThreadSafeReqQueue *queue) {
    if (!queue) return;
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    free(queue->buffer);
    free(queue);
    printf("Thread-safe UVM request queue destroyed\n");
}

// Работаем с UvmRequest
bool queue_req_enqueue(ThreadSafeReqQueue *queue, const UvmRequest *request) {
    if (!queue || !request) return false;

    pthread_mutex_lock(&queue->mutex);
    while (queue->count == queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }

    // Копируем структуру UvmRequest
    memcpy(&queue->buffer[queue->head], request, sizeof(UvmRequest));

    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;
    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

// Работаем с UvmRequest
bool queue_req_dequeue(ThreadSafeReqQueue *queue, UvmRequest *request) {
     if (!queue || !request) return false;

    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->cond_not_empty, &queue->mutex);
    }
    if (queue->shutdown && queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }

    // Копируем структуру UvmRequest
    memcpy(request, &queue->buffer[queue->tail], sizeof(UvmRequest));

    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;
    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

void queue_req_shutdown(ThreadSafeReqQueue *queue) {
    if (!queue) return;
    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = true;
    pthread_cond_broadcast(&queue->cond_not_empty);
    pthread_cond_broadcast(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    printf("Thread-safe UVM request queue shutdown initiated.\n");
}