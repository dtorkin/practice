/*
 * utils/ts_queue.c
 *
 * Описание:
 * Реализация потокобезопасной очереди.
 * МОДИФИЦИРОВАНА для хранения QueuedMessage вместо Message.
 */

#include "ts_queue.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // Для memcpy
#include <arpa/inet.h> // Для ntohs/htons
#include <pthread.h>
#include <stdbool.h>
#include "../svm/svm_types.h" // Для QueuedMessage и Message

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

    // Выделяем память под QueuedMessage
    queue->buffer = (QueuedMessage*)malloc(capacity * sizeof(QueuedMessage));
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

    printf("Thread-safe queue created with capacity %zu (for QueuedMessage)\n", capacity);
    return queue;
}

void queue_destroy(ThreadSafeQueue *queue) {
    if (!queue) return;

    // Перед уничтожением мьютексов/condvars убедиться, что потоки завершены
    // Это должно управляться извне (вызов queue_shutdown() и pthread_join())

    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    free(queue->buffer); // Освобождаем буфер QueuedMessage
    free(queue);
    printf("Thread-safe queue destroyed\n");
}

bool queue_enqueue(ThreadSafeQueue *queue, const QueuedMessage *queued_message) {
    if (!queue || !queued_message) return false;

    pthread_mutex_lock(&queue->mutex);

    // Ждем, пока в очереди есть место ИЛИ пока не пришел сигнал shutdown
    while (queue->count == queue->capacity && !queue->shutdown) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }

    if (queue->shutdown) {
        pthread_mutex_unlock(&queue->mutex);
        return false; // Очередь закрыта
    }

    // Копируем всю структуру QueuedMessage
    // Внутренняя логика проверки размера тела Message должна быть ВЫШЕ
    // по стеку вызовов, перед вызовом queue_enqueue, если это нужно.
    // Здесь мы доверяем, что queued_message содержит корректные данные.
    memcpy(&queue->buffer[queue->head], queued_message, sizeof(QueuedMessage));

    // Обработка сетевого порядка байт для длины тела ВНУТРИ сообщения
    // происходит при СОЗДАНИИ сообщения перед вызовом enqueue
    // и при ИСПОЛЬЗОВАНИИ сообщения после dequeue. Очередь просто хранит байты.
    // Однако, для отладки или если структура QueuedMessage как-то сериализуется,
    // может потребоваться конвертация здесь. Пока оставляем без изменений.

    queue->head = (queue->head + 1) % queue->capacity;
    queue->count++;

    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);
    // printf("DEBUG: Enqueued message for instance %d\n", queued_message->instance_id);
    return true;
}

bool queue_dequeue(ThreadSafeQueue *queue, QueuedMessage *queued_message) {
     if (!queue || !queued_message) return false;

    pthread_mutex_lock(&queue->mutex);

    // Ждем, пока очередь не пуста ИЛИ пока не пришел сигнал shutdown
    while (queue->count == 0 && !queue->shutdown) {
        pthread_cond_wait(&queue->cond_not_empty, &queue->mutex);
    }

    // Если очередь закрыта И пуста, возвращаем false
    if (queue->shutdown && queue->count == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }

    // Копируем всю структуру QueuedMessage из буфера
    memcpy(queued_message, &queue->buffer[queue->tail], sizeof(QueuedMessage));

    // Логика проверки размера тела и преобразования порядка байт (ntohs/htons)
    // должна быть ВЫШЕ по стеку вызовов, ПОСЛЕ вызова queue_dequeue,
    // когда получатель будет реально использовать данные из queued_message->message.
    // Очередь просто передает байты.

    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->count--;

    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    // printf("DEBUG: Dequeued message for instance %d\n", queued_message->instance_id);
    return true;
}

void queue_shutdown(ThreadSafeQueue *queue) {
    if (!queue) return;

    pthread_mutex_lock(&queue->mutex);
    if (!queue->shutdown) { // Предотвращаем повторный вывод сообщения
       queue->shutdown = true;
       printf("Thread-safe queue shutdown initiated.\n");
       pthread_cond_broadcast(&queue->cond_not_empty);
       pthread_cond_broadcast(&queue->cond_not_full);
    }
    pthread_mutex_unlock(&queue->mutex);
}