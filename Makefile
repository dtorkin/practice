# Имя компилятора
CC = gcc
# Флаги компиляции
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread
# Флаги линковки
LDFLAGS = -pthread
# Библиотеки
LIBS = -lrt

# Исполняемые файлы
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# --- Исходные файлы ---
SVM_SRCS = svm/svm_main.c svm/svm_handlers.c svm/svm_timers.c svm/svm_receiver.c svm/svm_processor.c svm/svm_sender.c
UVM_SRCS = uvm/uvm_main.c uvm/uvm_sender.c uvm/uvm_receiver.c uvm/uvm_utils.c
PROTOCOL_SRCS = protocol/message_utils.c protocol/message_builder.c
IO_SRCS = io/io_common.c io/io_ethernet.c io/io_serial.c
CONFIG_SRCS = config/config.c config/ini.c
# Добавляем все три очереди в UTILS_SRCS
UTILS_SRCS = utils/ts_queue.c utils/ts_queue_req.c utils/ts_queued_msg_queue.c utils/ts_uvm_resp_queue.c

# --- Объектные файлы ---
COMMON_OBJS = $(PROTOCOL_SRCS:.c=.o) $(IO_SRCS:.c=.o) $(CONFIG_SRCS:.c=.o) $(UTILS_SRCS:.c=.o)
SVM_OBJS = $(SVM_SRCS:.c=.o) $(COMMON_OBJS)
UVM_OBJS = $(UVM_SRCS:.c=.o) $(COMMON_OBJS)

# --- Правила сборки ---
all: $(SVM_TARGET) $(UVM_TARGET)

$(SVM_TARGET): $(SVM_OBJS)
	@echo "Linking $(SVM_TARGET)..."
	$(CC) $(CFLAGS) $(SVM_OBJS) -o $(SVM_TARGET) $(LDFLAGS) $(LIBS)
	@echo "SVM application ($(SVM_TARGET)) built successfully."

$(UVM_TARGET): $(UVM_OBJS)
	@echo "Linking $(UVM_TARGET)..."
	$(CC) $(CFLAGS) $(UVM_OBJS) -o $(UVM_TARGET) $(LDFLAGS) $(LIBS)
	@echo "UVM application ($(UVM_TARGET)) built successfully."

%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@echo "Cleaning up build files..."
	rm -f $(SVM_TARGET) $(UVM_TARGET) \
	      $(SVM_SRCS:.c=.o) $(UVM_SRCS:.c=.o) $(COMMON_OBJS) \
	      core.* *.core *~
	@echo "Cleanup finished."

.PHONY: all clean