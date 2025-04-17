# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm

# Флаги линковщика
LDFLAGS_SVM = -lrt
LDFLAGS_UVM =

# Исходные файлы (.c)
SVM_C_FILES = svm/svm_main.c svm/svm_handlers.c svm/svm_timers.c
UVM_C_FILES = uvm/uvm_main.c uvm/uvm_comm.c # Теперь два файла для UVM
PROTOCOL_C_FILES = protocol/message_utils.c protocol/message_builder.c
IO_C_FILES = io/io_common.c

# Объектные файлы (.o)
SVM_OBJS = $(SVM_C_FILES:.c=.o)
UVM_OBJS = $(UVM_C_FILES:.c=.o) # Теперь включает uvm_main.o и uvm_comm.o
PROTOCOL_OBJS = $(PROTOCOL_C_FILES:.c=.o)
IO_OBJS = $(IO_C_FILES:.c=.o)

# Все объектные файлы, необходимые для линковки
ALL_SVM_OBJS = $(SVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS)
ALL_UVM_OBJS = $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS) # Обновлено

# Исполняемые файлы
SVM_TARGET = svm_app # Оставляем переименованный
UVM_TARGET = uvm_app # Переименовываем UVM тоже

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
# Правило для компиляции файлов в корне (теперь не используется)
# %.o: %.c $(HEADERS)
#	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в protocol/
protocol/%.o: protocol/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в io/
io/%.o: io/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в svm/
svm/%.o: svm/%.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для компиляции файлов в uvm/
uvm/%.o: uvm/%.c $(HEADERS) # Добавлено правило для uvm
	$(CC) $(CFLAGS) -c $< -o $@


# Правило для очистки
clean:
	rm -f $(SVM_TARGET) $(UVM_TARGET) $(SVM_OBJS) $(UVM_OBJS) $(PROTOCOL_OBJS) $(IO_OBJS)

# Указываем, что all и clean - не файлы
.PHONY: all clean