# Имя компилятора
CC = gcc
# Флаги компиляции: -Wall (все предупреждения), -Wextra (доп. предупреждения), -g (отладочная информация)
# -pthread нужен и для компиляции, и для линковки потокобезопасного кода
# Добавляем пути ко всем директориям с заголовочными файлами через -I
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread
# Флаги линковки: -pthread нужен для библиотеки pthreads
LDFLAGS = -pthread
# Необходимые библиотеки: -lrt для clock_gettime (в svm_timers.c)
LIBS = -lrt

# Имена исполняемых файлов
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# --- Исходные файлы ---

# Модуль SVM
SVM_SRCS = svm/svm_main.c \
           svm/svm_handlers.c \
           svm/svm_timers.c \
           svm/svm_receiver.c \
           svm/svm_processor.c \
           svm/svm_sender.c

# Модуль UVM
UVM_SRCS = uvm/uvm_main.c \
           uvm/uvm_sender.c \
           uvm/uvm_receiver.c \
           uvm/uvm_utils.c

# Модуль Protocol
PROTOCOL_SRCS = protocol/message_utils.c \
                protocol/message_builder.c

# Модуль IO
IO_SRCS = io/io_common.c \
          io/io_ethernet.c \
          io/io_serial.c

# Модуль Config
CONFIG_SRCS = config/config.c \
              config/ini.c

# Модуль Utils
# УБРАН ts_queued_msg_queue.c, т.к. мы откатили SVM к использованию ts_queue
UTILS_SRCS = utils/ts_queue.c \
             utils/ts_queue_req.c \
             utils/ts_uvm_resp_queue.c

# --- Объектные файлы ---

# Собираем все общие объектные файлы в одну переменную для удобства
COMMON_OBJS = $(PROTOCOL_SRCS:.c=.o) \
              $(IO_SRCS:.c=.o) \
              $(CONFIG_SRCS:.c=.o) \
              $(UTILS_SRCS:.c=.o) # ts_queued_msg_queue.o автоматически убран

# Объектные файлы, специфичные для SVM + общие
SVM_OBJS = $(SVM_SRCS:.c=.o) $(COMMON_OBJS)

# Объектные файлы, специфичные для UVM + общие
UVM_OBJS = $(UVM_SRCS:.c=.o) $(COMMON_OBJS)

# --- Правила сборки ---

# Правило по умолчанию: собрать оба приложения
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM приложения
$(SVM_TARGET): $(SVM_OBJS)
	@echo "Linking $(SVM_TARGET)..."
	$(CC) $(CFLAGS) $(SVM_OBJS) -o $(SVM_TARGET) $(LDFLAGS) $(LIBS)
	@echo "SVM application ($(SVM_TARGET)) built successfully."

# Правило для сборки UVM приложения
$(UVM_TARGET): $(UVM_OBJS)
	@echo "Linking $(UVM_TARGET)..."
	$(CC) $(CFLAGS) $(UVM_OBJS) -o $(UVM_TARGET) $(LDFLAGS) $(LIBS)
	@echo "UVM application ($(UVM_TARGET)) built successfully."

# Правило для компиляции .c файлов в .o (шаблон)
# Компилируем каждый .c файл в соответствующий .o файл в той же директории
%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

# Правило для очистки (удаляет объектные файлы и исполняемые файлы)
clean:
	@echo "Cleaning up build files..."
	rm -f $(SVM_TARGET) $(UVM_TARGET) \
	      $(SVM_SRCS:.c=.o) $(UVM_SRCS:.c=.o) $(COMMON_OBJS) \
	      core.* *.core *~
	@echo "Cleanup finished."

# Фиктивные цели (не создают файлы с такими именами)
.PHONY: all clean