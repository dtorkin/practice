# Имя компилятора C
CC = gcc
# Имя компилятора C++
CXX = g++
# Флаги для C кода
CFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread $(QT_INCLUDE_FLAGS) # Добавляем Qt includes и для C
# Флаги для C++ кода
CXXFLAGS = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread -fPIC $(QT_INCLUDE_FLAGS)
# Флаги линковки
LDFLAGS = -pthread $(QT_LINK_FLAGS) # Добавляем путь к библиотекам Qt
# Библиотеки
LIBS = -lrt $(QT_LINK_LIBS)      # Добавляем библиотеки Qt

# --- Qt Настройки (Явное указание путей) ---
QT_INCLUDE_PATH = /usr/include/x86_64-linux-gnu/qt5
QT_LIBRARY_PATH = /usr/lib/x86_64-linux-gnu
QT_MOC_PATH ?= $(shell which moc-qt5 || which moc || echo "/usr/lib/x86_64-linux-gnu/qt5/bin/moc")

QT_INCLUDE_FLAGS = -I$(QT_INCLUDE_PATH) \
                   -I$(QT_INCLUDE_PATH)/QtWidgets \
                   -I$(QT_INCLUDE_PATH)/QtGui \
                   -I$(QT_INCLUDE_PATH)/QtCore \
                   -I$(QT_INCLUDE_PATH)/QtNetwork
QT_LINK_FLAGS = -L$(QT_LIBRARY_PATH)
QT_LINK_LIBS = -lQt5Widgets -lQt5Gui -lQt5Core -lQt5Network
MOC = $(QT_MOC_PATH)

# Имена исполняемых файлов
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# --- Исходные файлы ---
# C исходники для SVM (БЕЗ main)
SVM_C_SRCS = svm/svm_handlers.c \
             svm/svm_timers.c \
             svm/svm_receiver.c \
             svm/svm_processor.c \
             svm/svm_sender.c
# C++ исходники для SVM (main + GUI)
SVM_CXX_SRCS = svm/svm_main.c \  # <-- svm_main.c ПЕРЕНЕСЕН СЮДА
               svm/svm_gui.cpp

# C исходники для UVM
UVM_SRCS = uvm/uvm_main.c uvm/uvm_sender.c uvm/uvm_receiver.c uvm/uvm_utils.c

# Общие C исходники
COMMON_C_SRCS = protocol/message_utils.c \
                protocol/message_builder.c \
                io/io_common.c io/io_ethernet.c io/io_serial.c \
                config/config.c config/ini.c \
                utils/ts_queue.c utils/ts_queue_req.c \
                utils/ts_queued_msg_queue.c utils/ts_uvm_resp_queue.c

# --- Объектные файлы ---
SVM_C_OBJS = $(SVM_C_SRCS:.c=.o)
SVM_CXX_OBJS = $(SVM_CXX_SRCS:.cpp=.o) $(SVM_CXX_SRCS:.c=.o) # Включаем и .cpp и .c из CXX_SRCS
# SVM_MAIN_C_OBJ = $(SVM_MAIN_C_SRC:.c=.o) # <-- УДАЛЕНО
COMMON_C_OBJS = $(COMMON_C_SRCS:.c=.o)
UVM_OBJS = $(UVM_SRCS:.c=.o) $(COMMON_C_OBJS)

# Файлы, генерируемые MOC
SVM_HEADERS_WITH_QOBJECT = $(shell grep -l Q_OBJECT svm/*.h)
SVM_MOCS = $(patsubst %.h,moc_%.cpp,$(notdir $(SVM_HEADERS_WITH_QOBJECT)))
SVM_MOC_OBJS = $(SVM_MOCS:.cpp=.o)

# Объектные файлы для SVM = SVM модули(C) + SVM Main+GUI(C++) + MOC(C++) + Общие(C)
# SVM_MAIN_C_OBJ УДАЛЕН из списка
SVM_ALL_OBJS = $(SVM_C_OBJS) $(SVM_CXX_OBJS) $(SVM_MOC_OBJS) $(COMMON_C_OBJS)

# --- Правила сборки ---
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM (используем CXX для линковки)
$(SVM_TARGET): $(SVM_ALL_OBJS)
	@echo "Linking $(SVM_TARGET)..."
	$(CXX) $(CXXFLAGS) $(SVM_ALL_OBJS) -o $(SVM_TARGET) $(LDFLAGS) $(LIBS)
	@echo "SVM application ($(SVM_TARGET)) built successfully."

# Правило для сборки UVM (используем CC)
$(UVM_TARGET): $(UVM_OBJS)
	@echo "Linking $(UVM_TARGET)..."
	$(CC) $(CFLAGS) $(UVM_OBJS) -o $(UVM_TARGET) $(LDFLAGS) $(LIBS)
	@echo "UVM application ($(UVM_TARGET)) built successfully."

# Правило компиляции C файлов
# Теперь оно НЕ обрабатывает svm_main.c
%.o: %.c
	@echo "Compiling (C) $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

# Правило компиляции C++ файлов
# Теперь оно обрабатывает и .cpp, и svm_main.c
%.o: %.cpp
	@echo "Compiling (C++) $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $<
# Добавляем правило для компиляции svm_main.c с помощью CXX
svm/svm_main.o: svm/svm_main.c
	@echo "Compiling (C++-Linkable C) $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $< # Компилируем svm_main.c как C++

# Правило для MOC
moc_%.cpp: svm/%.h
	@echo "Running MOC on $<..."
	$(MOC) $< -o $@

# Правило для очистки
clean:
	@echo "Cleaning up build files..."
	rm -f $(SVM_TARGET) $(UVM_TARGET) \
	      $(SVM_C_SRCS:.c=.o) $(UVM_SRCS:.c=.o) $(COMMON_C_OBJS) \
	      $(SVM_CXX_SRCS:.cpp=.o) $(SVM_CXX_SRCS:.c=.o) \
	      moc_*.o moc_*.cpp \
	      core.* *.core *~
	@echo "Cleanup finished."

.PHONY: all clean