# Компилятор и флаги
CC = gcc
CFLAGS = -Wall -Wextra -g # -Iprotocol можно добавить, если include не находит
LDFLAGS = -lrt # Для таймеров в SVM

# Исходные файлы
COMMON_SRC = common.c
SVM_SRC = svm.c
UVM_SRC = uvm.c
PROTOCOL_UTILS_SRC = protocol/message_utils.c
PROTOCOL_BUILDER_SRC = protocol/message_builder.c
# Добавьте сюда другие .c файлы по мере их создания

# Объектные файлы
COMMON_OBJ = $(COMMON_SRC:.c=.o)
SVM_OBJ = $(SVM_SRC:.c=.o)
UVM_OBJ = $(UVM_SRC:.c=.o)
PROTOCOL_UTILS_OBJ = $(PROTOCOL_UTILS_SRC:.c=.o)
PROTOCOL_BUILDER_OBJ = $(PROTOCOL_BUILDER_SRC:.c=.o)
# Добавьте сюда другие .o файлы

# Исполняемые файлы
SVM_TARGET = svm
UVM_TARGET = uvm

# Правило по умолчанию
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM
$(SVM_TARGET): $(COMMON_OBJ) $(SVM_OBJ) $(PROTOCOL_UTILS_OBJ) $(PROTOCOL_BUILDER_OBJ) # Добавили зависимости
	$(CC) $(COMMON_OBJ) $(SVM_OBJ) $(PROTOCOL_UTILS_OBJ) $(PROTOCOL_BUILDER_OBJ) -o $(SVM_TARGET) $(LDFLAGS) # Передаем все .o линковщику

# Правило для сборки UVM
$(UVM_TARGET): $(COMMON_OBJ) $(UVM_OBJ) $(PROTOCOL_UTILS_OBJ) $(PROTOCOL_BUILDER_OBJ) # Добавили зависимости
	$(CC) $(COMMON_OBJ) $(UVM_OBJ) $(PROTOCOL_UTILS_OBJ) $(PROTOCOL_BUILDER_OBJ) -o $(UVM_TARGET) # LDFLAGS не нужен?

# Правило для компиляции .c в .o (общее правило)
# Оно скомпилирует protocol/message_utils.c в protocol/message_utils.o
# и protocol/message_builder.c в protocol/message_builder.o
%.o: %.c protocol/protocol_defs.h protocol/message_utils.h protocol/message_builder.h common.h # Добавьте другие .h по мере необходимости
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для очистки
clean:
	rm -f $(SVM_TARGET) $(UVM_TARGET) $(COMMON_OBJ) $(SVM_OBJ) $(UVM_OBJ) $(PROTOCOL_UTILS_OBJ) $(PROTOCOL_BUILDER_OBJ) # Добавили новые .o

.PHONY: all clean