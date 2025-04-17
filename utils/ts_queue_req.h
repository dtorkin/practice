/*
 * utils/ts_queue_req.h
 *
 * Описание:
 * Потокобезопасная очередь для передачи UvmRequest между потоками UVM.
 * (Адаптированная копия ts_queue.h)
 */

#ifndef TS_QUEUE_REQ_H
#define TS_QUEUE_REQ_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include "../uvm/uvm_types.h" // <--- Включаем типы UVM

typedef struct {
    UvmRequest *buffer; // <--- Тип буфера изменен на UvmRequest*
    size_t capacity;
    size_t count;
    size_t head;
    size_t tail;
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
    bool shutdown;
} ThreadSafeReqQueue; // <--- Тип структуры изменен

/**
 * @brief Создает и инициализирует очередь запросов UVM.
 */
ThreadSafeReqQueue* queue_req_create(size_t capacity); // <--- Имя функции изменено

/**
 * @brief Уничтожает очередь запросов UVM.
 */
void queue_req_destroy(ThreadSafeReqQueue *queue); // <--- Имя функции изменено

/**
 * @brief Добавляет копию запроса в очередь.
 */
bool queue_req_enqueue(ThreadSafeReqQueue *queue, const UvmRequest *request); // <--- Имя и тип аргумента изменены

/**
 * @brief Извлекает запрос из очереди.
 */
bool queue_req_dequeue(ThreadSafeReqQueue *queue, UvmRequest *request); // <--- Имя и тип аргумента изменены

/**
 * @brief Сигнализирует о завершении работы очереди запросов.
 */
void queue_req_shutdown(ThreadSafeReqQueue *queue); // <--- Имя функции изменено

#endif // TS_QUEUE_REQ_H