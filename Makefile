# Компилятор и флаги
CC = gcc
# Добавляем пути к заголовочным файлам protocol/ и io/
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio

# Флаги линковщика
LDFLAGS_SVM = -lrt # Для таймеров в SVM (setitimer)
LDFLAGS_UVM =      # Для UVM пока ничего не нужно

# Исходные файлы (.c)
# common.c пуст, не включаем его
SVM_C_FILES = svm.c
UVM_C_FILES = uvm.c
PROTOCOL_C_FILES = protocol/message_utils.c protocol/message_builder.c
IO_C_FILES = io/io_common.c

# Объектные файлы (.o)
# Создаем список объектных файлов, заменяя .c на .o
SVM_OBJS = $(SVM_C_FILES:.c=.o)
UVM_OBJS = $(UVM_C_FILES:.c=.o)
PROTOCOL_OBJS = $(PROTOCOL_C_FILES:.c=.o)
IO_OBJS = $(IO_C_FILES:.c=.o)

# Все объектные файлы, необходимые для линковки
ALL_SVM_OBJS = $(SVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS)
ALL_UVM_OBJS = $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS)

# Исполняемые файлы
SVM_TARGET = svm
UVM_TARGET = uvm

# Заголовочные файлы (для автоматического отслеживания зависимостей компилятором)
# Хотя мы не используем автоматическое отслеживание в этом простом Makefile,
# перечисление их здесь может быть полезно для понимания.
HEADERS = protocol/protocol_defs.h protocol/message_utils.h \
          protocol/message_builder.h io/io_common.h common.h

# Правило по умолчанию: собрать оба
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM
$(SVM_TARGET): $(ALL_SVM_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_SVM)

# Правило для сборки UVM
$(UVM_TARGET): $(ALL_UVM_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_UVM)

# --- Правила компиляции ---
# Правило для компиляции файлов в корне
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в protocol/
protocol/%.o: protocol/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в io/
io/%.o: io/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для очистки
clean:
	rm -f $(SVM_TARGET) $(UVM_TARGET) $(SVM_OBJS) $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS)

# Указываем, что all и clean - не файлы
.PHONY: all clean