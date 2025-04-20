/*
 * utils/ts_queue.c
 *
 * Описание:
 * Реализация потокобезопасной очереди для Message.
 * (Версия для работы с Message)
 */

#include "ts_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // Для memcpy
#include <arpa/inet.h> // Для ntohs/htons
#include <pthread.h>
#include <stdbool.h>
#include "../protocol/protocol_defs.h" // Для Message

ThreadSafeQueue* queue_create(size_t capacity) {
    if (capacity == 0) { /* ... */ return NULL; }
    ThreadSafeQueue *queue = (ThreadSafeQueue*)malloc(sizeof(ThreadSafeQueue));
    if (!queue) { /* ... */ return NULL; }
    queue->buffer = (Message*)malloc(capacity * sizeof(Message)); // Память под Message
    if (!queue->buffer) { /* ... */ free(queue); return NULL; }
    queue->capacity = capacity;
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->shutdown = false;
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) { /* ... */ free(queue->buffer); free(queue); return NULL; }
    if (pthread_cond_init(&queue->cond_not_empty, NULL) != 0) { /* ... */ pthread_mutex_destroy(&queue->mutex); free(queue->buffer); free(queue); return NULL; }
    if (pthread_cond_init(&queue->cond_not_full, NULL) != 0) { /* ... */ pthread_cond_destroy(&queue->cond_not_empty); pthread_mutex_destroy(&queue->mutex); free(queue->buffer); free(queue); return NULL; }
    printf("Thread-safe Message queue created with capacity %zu\n", capacity);
    return queue;
}

void queue_destroy(ThreadSafeQueue *queue) {
    if (!queue) return;
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    free(queue->buffer);
    free(queue);
    printf("Thread-safe Message queue destroyed\n");
}

bool queue_enqueue(ThreadSafeQueue *queue, const Message *message) {
    if (!queue || !message) return false;
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }
    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    // Копируем Message с учетом body_length
    uint16_t body_len_host = ntohs(message->header.body_length);
    size_t message_size = sizeof(MessageHeader) + body_len_host;
    if (message_size > sizeof(Message)) {
         fprintf(stderr,"Error: Message size %zu exceeds buffer element size %zu in enqueue\n", message_size, sizeof(Message));
         message_size = sizeof(Message); // Ограничиваем копирование
    }
    memcpy(&queue->buffer[queue->head], message, message_size);
    queue->buffer[queue->head].header.body_length = message->header.body_length; // Сохраняем оригинальную длину
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;
    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

bool queue_dequeue(ThreadSafeQueue *queue, Message *message) {
     if (!queue || !message) return false;
    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->cond_not_empty, &queue->mutex);
    }
    if (queue->shutdown && queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    // Копируем Message с учетом body_length
    uint16_t body_len_host = ntohs(queue->buffer[queue->tail].header.body_length);
    size_t message_size = sizeof(MessageHeader) + body_len_host;
    if (message_size > sizeof(Message)) {
         fprintf(stderr,"Error: Invalid body length %u (%zu bytes total) in queue during dequeue. Clamping size.\n", body_len_host, message_size);
         message_size = sizeof(Message);
    }
    memcpy(message, &queue->buffer[queue->tail], message_size);
    message->header = queue->buffer[queue->tail].header; // Копируем заголовок
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;
    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

void queue_shutdown(ThreadSafeQueue *queue) {
    if (!queue) return;
    pthread_mutex_lock(&queue->mutex);
    if (!queue->shutdown) {
       queue->shutdown = true;
       printf("Thread-safe Message queue shutdown initiated.\n");
       pthread_cond_broadcast(&queue->cond_not_empty);
       pthread_cond_broadcast(&queue->cond_not_full);
    }
    pthread_mutex_unlock(&queue->mutex);
}