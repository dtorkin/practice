# Имя компилятора C
CC = gcc
# Имя компилятора C++
CXX = g++
# Базовые флаги (общие для C и C++)
CFLAGS_BASE = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread
# Флаги линковки
LDFLAGS = -pthread
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

# Финальные флаги компиляции
CFLAGS = $(CFLAGS_BASE) $(QT_INCLUDE_FLAGS)
CXXFLAGS = $(CFLAGS_BASE) -fPIC $(QT_INCLUDE_FLAGS) # Используем CFLAGS_BASE для C++, добавляем -fPIC
# Финальные флаги линковки
LDFLAGS += $(QT_LINK_FLAGS)
# Финальные библиотеки
LIBS = $(LIBS_BASE) $(QT_LINK_LIBS)

# Имена исполняемых файлов
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# --- Исходные файлы ---
# Все C исходники, КРОМЕ svm_main.c и svm_gui.cpp
SVM_C_SRCS_ONLY = svm/svm_handlers.c \
                  svm/svm_timers.c \
                  svm/svm_receiver.c \
                  svm/svm_processor.c \
                  svm/svm_sender.c
# Все C++ исходники (включая main для SVM)
SVM_CXX_SRCS = svm/svm_main.c \
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
SVM_C_OBJS_ONLY = $(SVM_C_SRCS_ONLY:.c=.o)
SVM_CXX_OBJS = $(SVM_CXX_SRCS:.c=.o) # g++ скомпилирует .c как C++
SVM_GUI_OBJS = $(SVM_CXX_SRCS:.cpp=.o) # Для svm_gui.o из svm_gui.cpp
COMMON_C_OBJS = $(COMMON_C_SRCS:.c=.o)
UVM_OBJS = $(UVM_SRCS:.c=.o) $(COMMON_C_OBJS)

# Файлы, генерируемые MOC
SVM_HEADERS_WITH_QOBJECT = $(shell grep -l Q_OBJECT svm/*.h)
ifneq ($(strip $(SVM_HEADERS_WITH_QOBJECT)),)
    SVM_MOCS = $(patsubst %.h,moc_%.cpp,$(notdir $(SVM_HEADERS_WITH_QOBJECT)))
    SVM_MOC_OBJS = $(SVM_MOCS:.cpp=.o)
else
    SVM_MOCS =
    SVM_MOC_OBJS =
endif

# Объектные файлы для SVM = SVM модули(C) + SVM Main+GUI(C++) + MOC(C++) + Общие(C)
SVM_ALL_OBJS = $(SVM_C_OBJS_ONLY) $(SVM_CXX_OBJS) $(SVM_GUI_OBJS) $(SVM_MOC_OBJS) $(COMMON_C_OBJS)

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

# Правило компиляции C файлов (применяется к SVM_C_SRCS_ONLY, COMMON_C_SRCS, UVM_SRCS)
%.o: %.c
	@echo "Compiling (C) $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

# Правило компиляции C++ файлов (применяется к svm_gui.cpp и moc_*.cpp)
%.o: %.cpp
	@echo "Compiling (C++) $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# !!! УБРАНО явное правило для svm_main.o !!!
# Теперь svm_main.c будет скомпилирован правилом %.o: %.c, НО С ФЛАГАМИ C++,
# так как SVM_ALL_OBJS используется в правиле линковки с $(CXX)

# Правило для MOC
moc_%.cpp: svm/%.h
	@echo "Running MOC on $<..."
	$(MOC) $< -o $@

# Правило для очистки
clean:
	@echo "Cleaning up build files..."
	# Удаляем объектные файлы для всех исходников
	rm -f $(SVM_C_SRCS_ONLY:.c=.o) $(SVM_CXX_SRCS:.c=.o) $(SVM_CXX_SRCS:.cpp=.o) \
	      $(UVM_SRCS:.c=.o) $(COMMON_C_SRCS:.c=.o) $(SVM_MOC_OBJS) \
	      $(SVM_TARGET) $(UVM_TARGET) \
	      moc_*.cpp core.* *.core *~
	@echo "Cleanup finished."

.PHONY: all clean