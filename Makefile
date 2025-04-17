# Компилятор и флаги
CC = gcc
# Добавляем пути к заголовочным файлам
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig # Добавили -Iconfig

# Флаги линковщика
LDFLAGS_SVM = -lrt
LDFLAGS_UVM =

# Исходные файлы (.c)
SVM_C_FILES = svm/svm_main.c svm/svm_handlers.c svm/svm_timers.c
UVM_C_FILES = uvm/uvm_main.c uvm/uvm_comm.c
PROTOCOL_C_FILES = protocol/message_utils.c protocol/message_builder.c
IO_C_FILES = io/io_common.c
CONFIG_C_FILES = config/config.c config/ini.c # Добавили config.c и ini.c

# Объектные файлы (.o)
SVM_OBJS = $(SVM_C_FILES:.c=.o)
UVM_OBJS = $(UVM_C_FILES:.c=.o)
PROTOCOL_OBJS = $(PROTOCOL_C_FILES:.c=.o)
IO_OBJS = $(IO_C_FILES:.c=.o)
CONFIG_OBJS = $(CONFIG_C_FILES:.c=.o) # Добавили объекты для config

# Все объектные файлы, необходимые для линковки
# Теперь включаем CONFIG_OBJS
ALL_SVM_OBJS = $(SVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS) $(CONFIG_OBJS)
ALL_UVM_OBJS = $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS) $(CONFIG_OBJS)

# Исполняемые файлы
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# Заголовочные файлы
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
# Общее правило будет работать и для config/ т.к. мы указали -Iconfig
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Отдельные правила для поддиректорий больше не нужны при использовании общего правила с -I

# Правило для очистки
clean:
	# Используем find для удаления .o файлов во всех поддиректориях
	rm -f $(SVM_TARGET) $(UVM_TARGET)
	find . -name "*.o" -delete

# Указываем, что all и clean - не файлы
.PHONY: all clean