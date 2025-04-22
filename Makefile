# Имя компилятора C
CC = gcc
# Имя компилятора C++
CXX = g++
# Базовые флаги (общие для C и C++) - УБИРАЕМ ОТСЮДА ПУТИ QT
CFLAGS_BASE = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread
CXXFLAGS_BASE = $(CFLAGS_BASE) -fPIC # C++ флаги наследуют базовые + добавляем -fPIC
# Флаги линковки
LDFLAGS_BASE = -pthread
# Базовые библиотеки
LIBS_BASE = -lrt

# --- Qt Настройки (Явное указание путей) ---
QT_INCLUDE_PATH = /usr/include/x86_64-linux-gnu/qt5
QT_LIBRARY_PATH = /usr/lib/x86_64-linux-gnu
QT_MOC_PATH ?= $(shell which moc-qt5 || which moc || echo "/usr/lib/x86_64-linux-gnu/qt5/bin/moc")

# Флаги для включения заголовков Qt (ТОЛЬКО ДЛЯ C++ И MOC)
QT_INCLUDE_FLAGS = -I$(QT_INCLUDE_PATH) \
                   -I$(QT_INCLUDE_PATH)/QtWidgets \
                   -I$(QT_INCLUDE_PATH)/QtGui \
                   -I$(QT_INCLUDE_PATH)/QtCore \
                   -I$(QT_INCLUDE_PATH)/QtNetwork

# Флаги для линковки с библиотеками Qt
QT_LINK_FLAGS = -L$(QT_LIBRARY_PATH)
QT_LINK_LIBS = -lQt5Widgets -lQt5Gui -lQt5Core -lQt5Network
MOC = $(QT_MOC_PATH)

# Финальные флаги компиляции C (БЕЗ Qt)
CFLAGS = $(CFLAGS_BASE)
# Финальные флаги компиляции C++ (БАЗОВЫЕ + Qt)
CXXFLAGS = $(CXXFLAGS_BASE) $(QT_INCLUDE_FLAGS)
# Финальные флаги линковки SVM (БАЗОВЫЕ + Qt)
SVM_LDFLAGS = $(LDFLAGS_BASE) $(QT_LINK_FLAGS)
# Финальные библиотеки SVM (БАЗОВЫЕ + Qt)
SVM_LIBS = $(LIBS_BASE) $(QT_LINK_LIBS)
# Финальные флаги линковки UVM (ТОЛЬКО БАЗОВЫЕ)
UVM_LDFLAGS = $(LDFLAGS_BASE)
# Финальные библиотеки UVM (ТОЛЬКО БАЗОВЫЕ)
UVM_LIBS = $(LIBS_BASE)


# Имена исполняемых файлов
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# --- Исходные файлы ---
SVM_C_SRCS_ONLY = svm/svm_handlers.c svm/svm_timers.c svm/svm_receiver.c svm/svm_processor.c svm/svm_sender.c
SVM_CXX_SRCS = svm/svm_main.c svm/svm_gui.cpp # main теперь C++
ALL_COMMON_C_SRCS = protocol/message_utils.c protocol/message_builder.c \
                    io/io_common.c io/io_ethernet.c io/io_serial.c \
                    config/config.c config/ini.c \
                    utils/ts_queue.c utils/ts_queue_req.c \
                    utils/ts_queued_msg_queue.c utils/ts_uvm_resp_queue.c
UVM_SRCS = uvm/uvm_main.c uvm/uvm_sender.c uvm/uvm_receiver.c uvm/uvm_utils.c

# --- Объектные файлы ---
SVM_C_OBJS_ONLY = $(SVM_C_SRCS_ONLY:.c=.o)
SVM_CXX_OBJS = $(SVM_CXX_SRCS:.c=.o) # Компилируем .c как C++
SVM_CXX_OBJS += $(SVM_CXX_SRCS:.cpp=.o) # Добавляем .cpp
ALL_COMMON_C_OBJS = $(ALL_COMMON_C_SRCS:.c=.o)
UVM_OBJS = $(UVM_SRCS:.c=.o) $(ALL_COMMON_C_OBJS)

# MOC файлы
SVM_HEADERS_WITH_QOBJECT = $(shell grep -l Q_OBJECT svm/svm_gui.h) # Ищем только в svm_gui.h
ifneq ($(strip $(SVM_HEADERS_WITH_QOBJECT)),)
    SVM_MOCS = $(patsubst %.h,moc_%.cpp,$(notdir $(SVM_HEADERS_WITH_QOBJECT)))
    SVM_MOC_OBJS = $(SVM_MOCS:.cpp=.o)
else
    SVM_MOCS =
    SVM_MOC_OBJS =
endif

SVM_ALL_OBJS = $(SVM_C_OBJS_ONLY) $(SVM_CXX_OBJS) $(SVM_MOC_OBJS) $(ALL_COMMON_C_OBJS)

# --- Правила сборки ---
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM (линкуем с CXX и библиотеками Qt)
$(SVM_TARGET): $(SVM_ALL_OBJS)
	@echo "Linking $(SVM_TARGET)..."
	$(CXX) $(SVM_ALL_OBJS) -o $(SVM_TARGET) $(SVM_LDFLAGS) $(SVM_LIBS)
	@echo "SVM application ($(SVM_TARGET)) built successfully."

# Правило для сборки UVM (линкуем с CC и базовыми библиотеками)
$(UVM_TARGET): $(UVM_OBJS)
	@echo "Linking $(UVM_TARGET)..."
	$(CC) $(UVM_OBJS) -o $(UVM_TARGET) $(UVM_LDFLAGS) $(UVM_LIBS) # Используем CC
	@echo "UVM application ($(UVM_TARGET)) built successfully."

# Правило компиляции C файлов (Используем CC и CFLAGS)
%.o: %.c
	@echo "Compiling (C) $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

# Правило компиляции C++ файлов (Используем CXX и CXXFLAGS)
%.o: %.cpp
	@echo "Compiling (C++) $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Явное правило компиляции для svm_main.c КАК C++
svm/svm_main.o: svm/svm_main.c
	@echo "Compiling (C as C++) $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $< # Используем CXX и CXXFLAGS

# Правило для MOC
moc_%.cpp: svm/%.h
	@echo "Running MOC on $<..."
	$(MOC) $< -o $@

# Правило для очистки (ИСПОЛЬЗУЕМ ПРАВИЛЬНЫЕ ПЕРЕМЕННЫЕ С ОБЪЕКТАМИ)
clean:
	@echo "Cleaning up build files..."
	rm -f $(SVM_TARGET) $(UVM_TARGET) \
	      $(SVM_C_OBJS_ONLY) $(SVM_CXX_OBJS) $(SVM_MOC_OBJS) \
	      $(UVM_SRCS:.c=.o) $(ALL_COMMON_C_OBJS) \
	      moc_*.cpp core.* *.core *~
	@echo "Cleanup finished."

.PHONY: all clean