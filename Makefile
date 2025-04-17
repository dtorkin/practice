# Компилятор и флаги
CC = gcc
# Добавляем пути к заголовочным файлам
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm # Добавили -Isvm -Iuvm

# Флаги линковщика
LDFLAGS_SVM = -lrt # Для таймеров в SVM (setitimer)
LDFLAGS_UVM =

# Исходные файлы (.c)
SVM_C_FILES = svm/svm_main.c svm/svm_handlers.c svm/svm_timers.c # Теперь несколько файлов для SVM
UVM_C_FILES = uvm.c # UVM пока один файл
PROTOCOL_C_FILES = protocol/message_utils.c protocol/message_builder.c
IO_C_FILES = io/io_common.c

# Объектные файлы (.o)
SVM_OBJS = $(SVM_C_FILES:.c=.o) # Объекты для SVM
UVM_OBJS = $(UVM_C_FILES:.c=.o) # Объект для UVM
PROTOCOL_OBJS = $(PROTOCOL_C_FILES:.c=.o)
IO_OBJS = $(IO_C_FILES:.c=.o)

# Все объектные файлы, необходимые для линковки
ALL_SVM_OBJS = $(SVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS)
# UVM все еще зависит от protocol и io
ALL_UVM_OBJS = $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS)

# Исполняемые файлы
SVM_TARGET = svm_app
UVM_TARGET = uvm

# Заголовочные файлы (для автоматического отслеживания зависимостей компилятором)
HEADERS = $(wildcard protocol/*.h) $(wildcard io/*.h) $(wildcard svm/*.h) $(wildcard uvm/*.h) common.h

# Правило по умолчанию: собрать оба
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM
$(SVM_TARGET): $(ALL_SVM_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_SVM)

# Правило для сборки UVM
$(UVM_TARGET): $(ALL_UVM_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS_UVM)

# --- Правила компиляции ---
# Правило для компиляции файлов в корне (для uvm.c)
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в protocol/
protocol/%.o: protocol/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в io/
io/%.o: io/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в svm/
svm/%.o: svm/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в uvm/ (пока не нужно, но добавляем на будущее)
# uvm/%.o: uvm/%.c $(HEADERS)
#	$(CC) $(CFLAGS) -c $< -o $@


# Правило для очистки
clean:
	rm -f $(SVM_TARGET) $(UVM_TARGET) $(SVM_OBJS) $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS)

# Указываем, что all и clean - не файлы
.PHONY: all clean