# Компилятор
CC = gcc

# Флаги компиляции
CFLAGS = -Wall -Wextra -g

# Флаги для компоновщика
LDFLAGS = -lrt  # Для работы с таймерами (timer_create)

# Имена исполняемых файлов
SVM_EXEC = svm
UVM_EXEC = uvm

# Список исходных файлов
SOURCES = common.c svm.c uvm.c

# Список объектных файлов
OBJECTS = $(SOURCES:.c=.o)

# Цели по умолчанию
all: $(SVM_EXEC) $(UVM_EXEC)

# Правило для сборки SVM
$(SVM_EXEC): common.o svm.o
	$(CC) common.o svm.o -o $(SVM_EXEC) $(LDFLAGS)

# Правило для сборки UVM
$(UVM_EXEC): common.o uvm.o
	$(CC) common.o uvm.o -o $(UVM_EXEC) $(LDFLAGS)

# Правило для компиляции common.c в common.o
common.o: common.c common.h
	$(CC) $(CFLAGS) -c common.c -o common.o

# Правило для компиляции svm.c в svm.o
svm.o: svm.c common.h
	$(CC) $(CFLAGS) -c svm.c -o svm.o

# Правило для компиляции uvm.c в uvm.o
uvm.o: uvm.c common.h
	$(CC) $(CFLAGS) -c uvm.c -o uvm.o

# Правило для очистки
clean:
	rm -f $(OBJECTS) $(SVM_EXEC) $(UVM_EXEC)

# Правило для пересборки всего
rebuild: clean all

# Указываем, что clean и rebuild — это не файлы
.PHONY: all clean rebuild
