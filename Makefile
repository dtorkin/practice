# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -pthread # Добавили -pthread
LIBS_SVM = -lrt -pthread # Добавили -pthread
LIBS_UVM = -pthread      # Добавили -pthread

# Исходные файлы (.c)
SVM_C_FILES = svm/svm_main.c svm/svm_handlers.c svm/svm_timers.c # Добавим новые потоки позже
# --- Создаем заглушки для потоков SVM ---
SVM_RECEIVER_SRC = svm/svm_receiver.c
SVM_PROCESSOR_SRC = svm/svm_processor.c
SVM_SENDER_SRC = svm/svm_sender.c
# ------------------------------------------
UVM_C_FILES = uvm/uvm_main.c uvm/uvm_comm.c
PROTOCOL_C_FILES = protocol/message_utils.c protocol/message_builder.c
IO_C_FILES = io/io_common.c io/io_ethernet.c io/io_serial.c
CONFIG_C_FILES = config/config.c config/ini.c
UTILS_C_FILES = utils/ts_queue.c # Добавили очередь

# Объектные файлы (.o)
SVM_OBJS = $(SVM_C_FILES:.c=.o)
# --- Объекты для заглушек ---
SVM_RECEIVER_OBJ = $(SVM_RECEIVER_SRC:.c=.o)
SVM_PROCESSOR_OBJ = $(SVM_PROCESSOR_SRC:.c=.o)
SVM_SENDER_OBJ = $(SVM_SENDER_SRC:.c=.o)
# ---------------------------
UVM_OBJS = $(UVM_C_FILES:.c=.o)
PROTOCOL_OBJS = $(PROTOCOL_C_FILES:.c=.o)
IO_OBJS = $(IO_C_FILES:.c=.o)
CONFIG_OBJS = $(CONFIG_C_FILES:.c=.o)
UTILS_OBJS = $(UTILS_C_FILES:.c=.o) # Добавили объект для очереди

# Все объектные файлы, необходимые для линковки
# Добавляем все новые объекты
ALL_SVM_OBJS = $(SVM_OBJS) $(SVM_RECEIVER_OBJ) $(SVM_PROCESSOR_OBJ) $(SVM_SENDER_OBJ) \
               $(PROTOCOL_OBJS) $(IO_OBJS) $(CONFIG_OBJS) $(UTILS_OBJS)
# Для UVM пока добавляем только утилиты (очередь может понадобиться)
ALL_UVM_OBJS = $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS) $(CONFIG_OBJS) $(UTILS_OBJS)

# Исполняемые файлы
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# Заголовочные файлы
HEADERS = $(wildcard protocol/*.h) $(wildcard io/*.h) $(wildcard svm/*.h) \
          $(wildcard uvm/*.h) $(wildcard config/*.h) $(wildcard utils/*.h) common.h

# Правило по умолчанию: собрать оба
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM
$(SVM_TARGET): $(ALL_SVM_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS_SVM) # Используем LIBS_SVM

# Правило для сборки UVM
$(UVM_TARGET): $(ALL_UVM_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS_UVM) # Используем LIBS_UVM

# --- Правила компиляции ---
# Общее правило
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правила для поддиректорий (нужны, т.к. объектные файлы создаются рядом с исходными)
protocol/%.o: protocol/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

io/%.o: io/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

svm/%.o: svm/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

uvm/%.o: uvm/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

config/%.o: config/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

utils/%.o: utils/%.c $(HEADERS) # Добавлено правило для utils
	$(CC) $(CFLAGS) -c $< -o $@


# Правило для очистки
clean:
	rm -f $(SVM_TARGET) $(UVM_TARGET)
	find . -name "*.o" -delete # Удаляем все .o

.PHONY: all clean