# Имя компилятора C
CC = gcc
# Имя компилятора C++
CXX = g++
# Базовые флаги (общие для C и C++)
CFLAGS_BASE = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread
# Флаги линковки
LDFLAGS_BASE = -pthread
# Базовые библиотеки
LIBS_BASE = -lrt

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

# Финальные флаги компиляции C
CFLAGS = $(CFLAGS_BASE) $(QT_INCLUDE_FLAGS)
# Финальные флаги компиляции C++ (добавляем -fPIC для возможных библиотек)
CXXFLAGS = $(CFLAGS_BASE) -fPIC $(QT_INCLUDE_FLAGS)
# Финальные флаги линковки
LDFLAGS = $(LDFLAGS_BASE) $(QT_LINK_FLAGS)
# Финальные библиотеки (Общие + Qt)
LIBS = $(LIBS_BASE) $(QT_LINK_LIBS)

# Имена исполняемых файлов
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# --- Исходные файлы ---
# C исходники для SVM (БЕЗ main)
SVM_C_SRCS_ONLY = svm/svm_handlers.c \
                  svm/svm_timers.c \
                  svm/svm_receiver.c \
                  svm/svm_processor.c \
                  svm/svm_sender.c
# C++ исходники для SVM (ВКЛЮЧАЯ main и GUI)
SVM_CXX_SRCS = svm/svm_main.c \ # <-- ПЕРЕНЕСЕН СЮДА
               svm/svm_gui.cpp
# C исходник main для SVM (УДАЛЕНА ПЕРЕМЕННАЯ)
# SVM_MAIN_C_SRC = svm/svm_main.c

# C исходники для UVM
UVM_SRCS = uvm/uvm_main.c uvm/uvm_sender.c uvm/uvm_receiver.c uvm/uvm_utils.c

# Общие C исходники
ALL_COMMON_C_SRCS = protocol/message_utils.c \
                    protocol/message_builder.c \
                    io/io_common.c io/io_ethernet.c io/io_serial.c \
                    config/config.c config/ini.c \
                    utils/ts_queue.c utils/ts_queue_req.c \
                    utils/ts_queued_msg_queue.c utils/ts_uvm_resp_queue.c

# --- Объектные файлы ---
SVM_C_OBJS_ONLY = $(SVM_C_SRCS_ONLY:.c=.o)
SVM_CXX_OBJS = $(SVM_CXX_SRCS:.cpp=.o) $(SVM_CXX_SRCS:.c=.o) # <-- Компилируем и .c, и .cpp с CXX
# SVM_MAIN_C_OBJ = $(SVM_MAIN_C_SRC:.c=.o) # <-- УДАЛЕНА ПЕРЕМЕННАЯ
ALL_COMMON_C_OBJS = $(ALL_COMMON_C_SRCS:.c=.o)
UVM_OBJS = $(UVM_SRCS:.c=.o) $(ALL_COMMON_C_OBJS)

SVM_HEADERS_WITH_QOBJECT = $(shell grep -l Q_OBJECT svm/*.h)
ifneq ($(strip $(SVM_HEADERS_WITH_QOBJECT)),)
    SVM_MOCS = $(patsubst %.h,moc_%.cpp,$(notdir $(SVM_HEADERS_WITH_QOBJECT)))
    SVM_MOC_OBJS = $(SVM_MOCS:.cpp=.o)
else
    SVM_MOCS =
    SVM_MOC_OBJS =
endif

# Объектные файлы для SVM = SVM модули(C) + SVM GUI(C++) + MOC(C++) + Общие(C)
# SVM_MAIN_C_OBJ УДАЛЕН ИЗ СПИСКА
SVM_ALL_OBJS = $(SVM_C_OBJS_ONLY) $(SVM_CXX_OBJS) $(SVM_MOC_OBJS) $(ALL_COMMON_C_OBJS)

# --- Правила сборки ---
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM (используем CXX для линковки)
$(SVM_TARGET): $(SVM_ALL_OBJS)
	@echo "Linking $(SVM_TARGET)..."
	$(CXX) $(SVM_ALL_OBJS) -o $(SVM_TARGET) $(LDFLAGS) $(LIBS)
	@echo "SVM application ($(SVM_TARGET)) built successfully."

# Правило для сборки UVM (используем CXX для линковки, но без Qt LIBS)
$(UVM_TARGET): $(UVM_OBJS)
	@echo "Linking $(UVM_TARGET)..."
	$(CXX) $(UVM_OBJS) -o $(UVM_TARGET) $(LDFLAGS_BASE) $(LIBS_BASE)
	@echo "UVM application ($(UVM_TARGET)) built successfully."

# Правило компиляции C файлов (Используем CC и CFLAGS)
%.o: %.c
	@echo "Compiling (C) $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

# Правило компиляции C++ файлов (Используем CXX и CXXFLAGS)
# Это правило теперь будет компилировать и svm_gui.cpp, и svm_main.c
%.o: %.cpp
	@echo "Compiling (C++) $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# УДАЛИТЬ ЯВНОЕ ПРАВИЛО ДЛЯ SVM_MAIN_C_OBJ
# $(SVM_MAIN_C_OBJ): $(SVM_MAIN_C_SRC) $(wildcard svm/*.h) $(wildcard config/*.h)
#	@echo "Compiling (C++-Linkable C) $<..."
#	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Правило для MOC
moc_%.cpp: svm/%.h
	@echo "Running MOC on $<..."
	$(MOC) $< -o $@

# Правило для очистки (Обновляем для CXX_SRCS)
clean:
	@echo "Cleaning up build files..."
	rm -f $(SVM_TARGET) $(UVM_TARGET) \
	      $(SVM_C_OBJS_ONLY) $(SVM_CXX_OBJS) \
	      $(UVM_SRCS:.c=.o) $(ALL_COMMON_C_OBJS) $(SVM_MOC_OBJS) \
	      moc_*.cpp core.* *.core *~
	@echo "Cleanup finished."

.PHONY: all clean