# Компилятор и флаги
CC = gcc
# Добавляем пути к заголовочным файлам
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig

# Флаги линковщика
LDFLAGS_SVM = -lrt
LDFLAGS_UVM =

# Исходные файлы (.c)
SVM_C_FILES = svm/svm_main.c svm/svm_handlers.c svm/svm_timers.c
UVM_C_FILES = uvm/uvm_main.c uvm/uvm_comm.c
PROTOCOL_C_FILES = protocol/message_utils.c protocol/message_builder.c
# Теперь два файла в IO
IO_C_FILES = io/io_common.c io/io_ethernet.c io/io_serial.c
CONFIG_C_FILES = config/config.c config/ini.c

# Объектные файлы (.o)
SVM_OBJS = $(SVM_C_FILES:.c=.o)
UVM_OBJS = $(UVM_C_FILES:.c=.o)
PROTOCOL_OBJS = $(PROTOCOL_C_FILES:.c=.o)
IO_OBJS = $(IO_C_FILES:.c=.o) # Включает io_common.o, io_ethernet.o, io_serial.o
CONFIG_OBJS = $(CONFIG_C_FILES:.c=.o)

# Все объектные файлы, необходимые для линковки
ALL_SVM_OBJS = $(SVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS) $(CONFIG_OBJS)
ALL_UVM_OBJS = $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS) $(CONFIG_OBJS)

# Исполняемые файлы
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# Заголовочные файлы (для возможного автоматического отслеживания зависимостей)
HEADERS = $(wildcard protocol/*.h) $(wildcard io/*.h) $(wildcard svm/*.h) \
          $(wildcard uvm/*.h) $(wildcard config/*.h) common.h

# Правило по умолчанию: собрать оба
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM
$(SVM_TARGET): $(ALL_SVM_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_SVM)

# Правило для сборки UVM
$(UVM_TARGET): $(ALL_UVM_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_UVM)

# --- Правила компиляции ---
# Общее правило для компиляции .c в .o из любой директории
# (предполагается, что объектные файлы создаются рядом с исходными)
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


# Правило для очистки
clean:
	rm -f $(SVM_TARGET) $(UVM_TARGET)
	# Удаляем .o файлы из всех поддиректорий и корня
	find . -name "*.o" -delete

# Указываем, что all и clean - не файлы
.PHONY: all clean