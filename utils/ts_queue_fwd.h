/*
 * utils/ts_queue_fwd.h
 *
 * Описание:
 * Предварительное объявление структуры ThreadSafeQueue для использования
 * в заголовочных файлах без включения полного определения ts_queue.h,
 * чтобы избежать циклических зависимостей.
 */
#ifndef TS_QUEUE_FWD_H
#define TS_QUEUE_FWD_H

// Предварительное объявление
typedef struct ThreadSafeQueue ThreadSafeQueue;

#endif // TS_QUEUE_FWD_H