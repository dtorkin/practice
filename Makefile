# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread
LIBS_SVM = -lrt -pthread
LIBS_UVM = -pthread

# Исходные файлы (.c)
SVM_C_FILES = svm/svm_main.c svm/svm_handlers.c svm/svm_timers.c \
              svm/svm_receiver.c svm/svm_processor.c svm/svm_sender.c
# Добавляем uvm_utils.c
UVM_C_FILES = uvm/uvm_main.c \
              uvm/uvm_sender.c uvm/uvm_receiver.c uvm/uvm_utils.c # <-- Добавлен
PROTOCOL_C_FILES = protocol/message_utils.c protocol/message_builder.c
IO_C_FILES = io/io_common.c io/io_ethernet.c io/io_serial.c
CONFIG_C_FILES = config/config.c config/ini.c
UTILS_QUEUE_MSG_SRC = utils/ts_queue.c
UTILS_QUEUE_REQ_SRC = utils/ts_queue_req.c

# Объектные файлы (.o)
SVM_OBJS = $(SVM_C_FILES:.c=.o)
UVM_OBJS = $(UVM_C_FILES:.c=.o) # <-- Теперь включает uvm_utils.o
PROTOCOL_OBJS = $(PROTOCOL_C_FILES:.c=.o)
IO_OBJS = $(IO_C_FILES:.c=.o)
CONFIG_OBJS = $(CONFIG_C_FILES:.c=.o)
UTILS_QUEUE_MSG_OBJ = $(UTILS_QUEUE_MSG_SRC:.c=.o)
UTILS_QUEUE_REQ_OBJ = $(UTILS_QUEUE_REQ_SRC:.c=.o)

# Все объектные файлы, необходимые для линковки
ALL_SVM_OBJS = $(SVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS) $(CONFIG_OBJS) $(UTILS_QUEUE_MSG_OBJ)
# Добавляем uvm_utils.o в зависимости UVM
ALL_UVM_OBJS = $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS) $(CONFIG_OBJS) $(UTILS_QUEUE_MSG_OBJ) $(UTILS_QUEUE_REQ_OBJ)

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
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS_SVM)

# Правило для сборки UVM
$(UVM_TARGET): $(ALL_UVM_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS_UVM)

# --- Правила компиляции ---
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@
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
utils/%.o: utils/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для очистки
clean:
	rm -f $(SVM_TARGET) $(UVM_TARGET)
	find . -name "*.o" -delete

.PHONY: all clean