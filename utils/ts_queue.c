/*
 * utils/ts_queue.c
 *
 * Описание:
 * Реализация потокобезопасной очереди.
 */

#include "ts_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // Для memcpy
#include <arpa/inet.h> // <-- ДОБАВЛЕНО для ntohs/htons
#include <pthread.h>   // Убедитесь, что pthread.h включен
#include <stdbool.h> // Убедитесь, что stdbool.h включен

ThreadSafeQueue* queue_create(size_t capacity) {
    if (capacity == 0) {
        fprintf(stderr, "queue_create: Capacity must be greater than 0\n");
        return NULL;
    }

    ThreadSafeQueue *queue = (ThreadSafeQueue*)malloc(sizeof(ThreadSafeQueue));
    if (!queue) {
        perror("queue_create: Failed to allocate queue structure");
        return NULL;
    }

    queue->buffer = (Message*)malloc(capacity * sizeof(Message));
    if (!queue->buffer) {
        perror("queue_create: Failed to allocate queue buffer");
        free(queue);
        return NULL;
    }

    queue->capacity = capacity;
    queue->count = 0;
    queue->head = 0;
    queue->tail = 0;
    queue->shutdown = false;

    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        perror("queue_create: Mutex initialization failed");
        free(queue->buffer);
        free(queue);
        return NULL;
    }
    if (pthread_cond_init(&queue->cond_not_empty, NULL) != 0) {
        perror("queue_create: NotEmpty Condition variable initialization failed");
        pthread_mutex_destroy(&queue->mutex);
        free(queue->buffer);
        free(queue);
        return NULL;
    }
    if (pthread_cond_init(&queue->cond_not_full, NULL) != 0) {
        perror("queue_create: NotFull Condition variable initialization failed");
        pthread_cond_destroy(&queue->cond_not_empty);
        pthread_mutex_destroy(&queue->mutex);
        free(queue->buffer);
        free(queue);
        return NULL;
    }

    printf("Thread-safe queue created with capacity %zu\n", capacity);
    return queue;
}

void queue_destroy(ThreadSafeQueue *queue) {
    if (!queue) return;

    // Перед уничтожением мьютексов/condvars убедиться, что потоки завершены
    // Это должно управляться извне (вызов queue_shutdown() и pthread_join())

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    free(queue->buffer);
    free(queue);
    printf("Thread-safe queue destroyed\n");
}

bool queue_enqueue(ThreadSafeQueue *queue, const Message *message) {
    if (!queue || !message) return false;

    pthread_mutex_lock(&queue->mutex);

    // Ждем, пока в очереди есть место ИЛИ пока не пришел сигнал shutdown
    while (queue->count == queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }

    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }

    // Получаем длину тела в хост-порядке перед memcpy
    // Заголовок в message уже должен быть в сетевом порядке, кроме body_length
    uint16_t body_len_host = ntohs(message->header.body_length); // Используем ntohs
    size_t message_size = sizeof(MessageHeader) + body_len_host;

    if (message_size > sizeof(Message)) {
         fprintf(stderr,"Error: Message size %zu exceeds buffer size %zu in enqueue\n", message_size, sizeof(Message));
         message_size = sizeof(Message); // Ограничиваем копирование
    }

    memcpy(&queue->buffer[queue->head], message, message_size);
    // Длина тела в буфере должна остаться в сетевом порядке
    queue->buffer[queue->head].header.body_length = message->header.body_length;


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

    uint16_t body_len_host = ntohs(queue->buffer[queue->tail].header.body_length); // Используем ntohs
    size_t message_size = sizeof(MessageHeader) + body_len_host;

    if (message_size > sizeof(Message)) {
         fprintf(stderr,"Error: Invalid body length %u in queue during dequeue\n", body_len_host);
         message_size = sizeof(Message);
         // Устанавливаем "безопасную" длину в сообщение, которое вернем
         message->header.body_length = htons(MAX_MESSAGE_BODY_SIZE); // Используем htons
    } else {
         // Копируем правильный заголовок (включая длину)
         message->header.body_length = queue->buffer[queue->tail].header.body_length;
    }

    // Копируем заголовок и тело
    memcpy(message, &queue->buffer[queue->tail], message_size);


    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;

    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

void queue_shutdown(ThreadSafeQueue *queue) {
    if (!queue) return;

    pthread_mutex_lock(&queue->mutex);
    queue->shutdown = true;
    pthread_cond_broadcast(&queue->cond_not_empty);
    pthread_cond_broadcast(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    printf("Thread-safe queue shutdown initiated.\n");
}