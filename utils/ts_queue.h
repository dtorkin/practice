/*
 * utils/ts_queue.h
 *
 * Описание:
 * Потокобезопасная очередь для передачи Message между потоками.
 * Реализована как кольцевой буфер.
 * (Версия для работы с Message)
 */

#ifndef TS_QUEUE_H
#define TS_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include "../protocol/protocol_defs.h" // Для структуры Message
#include "ts_queue_fwd.h"     // Для struct ThreadSafeQueue

// Определение структуры очереди (работает с Message)
struct ThreadSafeQueue {
    Message *buffer;            // Буфер для хранения сообщений (копий)
    size_t capacity;            // Максимальная вместимость очереди
    size_t count;               // Текущее количество элементов в очереди
    size_t head;                // Индекс для добавления следующего элемента
    size_t tail;                // Индекс для извлечения следующего элемента
    pthread_mutex_t mutex;      // Мьютекс для защиты доступа к очереди
    pthread_cond_t cond_not_empty; // Условная переменная: очередь не пуста
    pthread_cond_t cond_not_full;  // Условная переменная: очередь не полна
    bool shutdown;              // Флаг для сигнализации о завершении работы
};

// Функции с префиксом queue_
ThreadSafeQueue* queue_create(size_t capacity);
void queue_destroy(ThreadSafeQueue *queue);
bool queue_enqueue(ThreadSafeQueue *queue, const Message *message);
bool queue_dequeue(ThreadSafeQueue *queue, Message *message);
void queue_shutdown(ThreadSafeQueue *queue);

#endif // TS_QUEUE_H