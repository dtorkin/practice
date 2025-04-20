/*
 * utils/ts_queue.h
 *
 * Описание:
 * Потокобезопасная очередь для передачи сообщений между потоками.
 * Реализована как кольцевой буфер.
 * МОДИФИЦИРОВАНА для хранения QueuedMessage вместо Message.
 */

#ifndef TS_QUEUE_H
#define TS_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include "../svm/svm_types.h" // <-- ВКЛЮЧЕНО для QueuedMessage
#include "ts_queue_fwd.h"     // <-- ВКЛЮЧЕНО для struct ThreadSafeQueue

// Структура очереди (определение теперь здесь, а не в ts_queue_fwd.h)
struct ThreadSafeQueue {
    QueuedMessage *buffer;      // Буфер для хранения сообщений (копий) + ID экземпляра
    size_t capacity;            // Максимальная вместимость очереди
    size_t count;               // Текущее количество элементов в очереди
    size_t head;                // Индекс для добавления следующего элемента
    size_t tail;                // Индекс для извлечения следующего элемента
    pthread_mutex_t mutex;      // Мьютекс для защиты доступа к очереди
    pthread_cond_t cond_not_empty; // Условная переменная: очередь не пуста
    pthread_cond_t cond_not_full;  // Условная переменная: очередь не полна
    bool shutdown;              // Флаг для сигнализации о завершении работы
};

/**
 * @brief Создает и инициализирует потокобезопасную очередь.
 * @param capacity Максимальное количество сообщений в очереди.
 * @return Указатель на созданную очередь или NULL в случае ошибки.
 */
ThreadSafeQueue* queue_create(size_t capacity);

/**
 * @brief Уничтожает очередь и освобождает все ресурсы.
 * @param queue Указатель на очередь для уничтожения.
 */
void queue_destroy(ThreadSafeQueue *queue);

/**
 * @brief Добавляет копию сообщения с ID экземпляра в очередь.
 * Блокируется, если очередь полна, до появления свободного места или сигнала shutdown.
 * @param queue Указатель на очередь.
 * @param queued_message Указатель на сообщение с ID для добавления (будет скопировано).
 * @return true в случае успеха, false если очередь была закрыта (shutdown).
 */
bool queue_enqueue(ThreadSafeQueue *queue, const QueuedMessage *queued_message);

/**
 * @brief Извлекает сообщение с ID экземпляра из очереди.
 * Блокируется, если очередь пуста, до появления сообщения или сигнала shutdown.
 * @param queue Указатель на очередь.
 * @param queued_message Указатель на структуру, куда будет скопировано извлеченное сообщение с ID.
 * @return true в случае успеха, false если очередь пуста и была закрыта (shutdown).
 */
bool queue_dequeue(ThreadSafeQueue *queue, QueuedMessage *queued_message);

/**
 * @brief Сигнализирует всем потокам, ожидающим на очереди, о завершении работы.
 * Устанавливает флаг shutdown и пробуждает все ждущие потоки.
 * @param queue Указатель на очередь.
 */
void queue_shutdown(ThreadSafeQueue *queue);

#endif // TS_QUEUE_H