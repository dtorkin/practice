#!/bin/bash

# Директория, где лежит svm_app
APP_DIR="." # Текущая директория, измените если нужно
SVM_APP="./svm_app" # Имя исполняемого файла

# Количество запускаемых экземпляров (можно читать из config.ini, но проще здесь)
NUM_INSTANCES=4

echo "Starting ${NUM_INSTANCES} SVM instances..."

# Запускаем экземпляры в фоновом режиме
for (( i=0; i<${NUM_INSTANCES}; i++ ))
do
   echo "Starting SVM instance ${i}..."
   # Передаем ID как аргумент командной строки
   "${APP_DIR}/${SVM_APP}" "${i}" &
   # Сохраняем PID, чтобы потом можно было остановить
   pids[${i}]=$!
   sleep 0.1 # Небольшая пауза между запусками
done

echo "All SVM instances started."
echo "PIDs: ${pids[@]}"
echo "Press Ctrl+C to stop all instances."

# Функция для остановки всех запущенных SVM
cleanup() {
    echo "\nStopping SVM instances..."
    for pid in "${pids[@]}"; do
        if kill -0 $pid 2>/dev/null; then # Проверяем, жив ли процесс
            echo "Sending SIGTERM to PID ${pid}..."
            kill -SIGTERM $pid
        fi
    done
    # Ждем немного, чтобы процессы завершились
    sleep 1
    echo "Cleanup done."
    exit 0
}

# Перехватываем SIGINT (Ctrl+C) и SIGTERM для вызова cleanup
trap cleanup SIGINT SIGTERM

# Бесконечный цикл, чтобы скрипт не завершался сразу
# Можно заменить на ожидание завершения дочерних процессов (wait),
# но trap + sleep проще для примера
while true; do
    sleep 1
done