/*
 * utils/ts_queued_msg_queue.h
 *
 * Описание:
 * Потокобезопасная очередь для передачи QueuedMessage (сообщение + ID экземпляра).
 * Реализована как кольцевой буфер.
 */

#ifndef TS_QUEUED_MSG_QUEUE_H
#define TS_QUEUED_MSG_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
// Включаем svm_types.h напрямую, т.к. он определяет QueuedMessage
#include "../svm/svm_types.h" // <-- Зависимость от SVM здесь
#include "ts_queued_msg_queue_fwd.h" // Для struct ThreadSafeQueuedMsgQueue

// Определение структуры новой очереди
struct ThreadSafeQueuedMsgQueue {
    QueuedMessage *buffer;      // Буфер для хранения сообщений + ID экземпляра
    size_t capacity;            // Максимальная вместимость очереди
    size_t count;               // Текущее количество элементов в очереди
    size_t head;                // Индекс для добавления следующего элемента
    size_t tail;                // Индекс для извлечения следующего элемента
    pthread_mutex_t mutex;      // Мьютекс для защиты доступа к очереди
    pthread_cond_t cond_not_empty; // Условная переменная: очередь не пуста
    pthread_cond_t cond_not_full;  // Условная переменная: очередь не полна
    bool shutdown;              // Флаг для сигнализации о завершении работы
};

// Функции с префиксом qmq_
ThreadSafeQueuedMsgQueue* qmq_create(size_t capacity);
void qmq_destroy(ThreadSafeQueuedMsgQueue *queue);
bool qmq_enqueue(ThreadSafeQueuedMsgQueue *queue, const QueuedMessage *queued_message);
bool qmq_dequeue(ThreadSafeQueuedMsgQueue *queue, QueuedMessage *queued_message);
void qmq_shutdown(ThreadSafeQueuedMsgQueue *queue);

#endif // TS_QUEUED_MSG_QUEUE_H