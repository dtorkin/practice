# Имя компилятора C
CC = gcc
# Имя компилятора C++
CXX = g++
# Флаги для C кода
CFLAGS_BASE = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread
# Флаги для C++ кода (добавляем -fPIC)
CXXFLAGS_BASE = -Wall -Wextra -g -Iprotocol -Iio -Isvm -Iuvm -Iconfig -Iutils -pthread -fPIC
# Флаги линковки
LDFLAGS = -pthread
# Библиотеки
LIBS = -lrt

# --- Qt Настройки (Явное указание путей) ---
# Пути, подтвержденные find
QT_INCLUDE_PATH = /usr/include/x86_64-linux-gnu/qt5
QT_LIBRARY_PATH = /usr/lib/x86_64-linux-gnu

# Флаги для включения заголовков Qt (для CFLAGS и CXXFLAGS)
QT_INCLUDE_FLAGS = -I$(QT_INCLUDE_PATH) \
                   -I$(QT_INCLUDE_PATH)/QtWidgets \
                   -I$(QT_INCLUDE_PATH)/QtGui \
                   -I$(QT_INCLUDE_PATH)/QtCore \
                   -I$(QT_INCLUDE_PATH)/QtNetwork

# Флаги для линковки с библиотеками Qt (для LDFLAGS и LIBS)
QT_LINK_FLAGS = -L$(QT_LIBRARY_PATH)
QT_LINK_LIBS = -lQt5Widgets -lQt5Gui -lQt5Core -lQt5Network

# Определение команды MOC (проверяем стандартные пути)
MOC ?= $(shell which moc-qt5 || which moc || echo "/usr/lib/x86_64-linux-gnu/qt5/bin/moc")


# Добавляем флаги Qt к базовым флагам
CFLAGS = $(CFLAGS_BASE) $(QT_INCLUDE_FLAGS)
CXXFLAGS = $(CXXFLAGS_BASE) $(QT_INCLUDE_FLAGS)
LDFLAGS += $(QT_LINK_FLAGS) # Добавляем путь к библиотекам
LIBS += $(QT_LINK_LIBS)      # Добавляем сами библиотеки

# Имена исполняемых файлов
SVM_TARGET = svm_app
UVM_TARGET = uvm_app

# --- Исходные файлы ---
# C исходники для SVM (кроме main)
SVM_C_SRCS = svm/svm_handlers.c \
             svm/svm_timers.c \
             svm/svm_receiver.c \
             svm/svm_processor.c \
             svm/svm_sender.c
# C++ исходники для SVM GUI
SVM_CXX_SRCS = svm/svm_gui.cpp
# C исходник main для SVM
SVM_MAIN_C_SRC = svm/svm_main.c

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
SVM_CXX_OBJS = $(SVM_CXX_SRCS:.cpp=.o)
SVM_MAIN_C_OBJ = $(SVM_MAIN_C_SRC:.c=.o)
COMMON_C_OBJS = $(COMMON_C_SRCS:.c=.o)
UVM_OBJS = $(UVM_SRCS:.c=.o) $(COMMON_C_OBJS)

# Файлы, генерируемые MOC
# Находим все .h в svm/, которые содержат Q_OBJECT (простой поиск строки)
SVM_HEADERS_WITH_QOBJECT = $(shell grep -l Q_OBJECT svm/*.h)
SVM_MOCS = $(patsubst %.h,moc_%.cpp,$(notdir $(SVM_HEADERS_WITH_QOBJECT)))
SVM_MOC_OBJS = $(SVM_MOCS:.cpp=.o)

# Объектные файлы для SVM = Main(C) + SVM модули(C) + SVM GUI(C++) + MOC(C++) + Общие(C)
SVM_ALL_OBJS = $(SVM_MAIN_C_OBJ) $(SVM_C_OBJS) $(SVM_CXX_OBJS) $(SVM_MOC_OBJS) $(COMMON_C_OBJS)

# --- Правила сборки ---
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM (используем CXX для линковки C и C++)
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
%.o: %.c
	@echo "Compiling (C) $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

# Правило компиляции C++ файлов
%.o: %.cpp
	@echo "Compiling (C++) $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Правило для MOC (Meta-Object Compiler)
moc_%.cpp: svm/%.h # Ищем исходный .h в svm/
	@echo "Running MOC on $<..."
	$(MOC) $< -o $@

# Правило для очистки
clean:
	@echo "Cleaning up build files..."
	rm -f $(SVM_TARGET) $(UVM_TARGET) \
	      $(SVM_SRCS:.c=.o) $(UVM_SRCS:.c=.o) $(COMMON_C_OBJS) \
	      $(SVM_CXX_SRCS:.cpp=.o) moc_*.o moc_*.cpp \
	      core.* *.core *~
	@echo "Cleanup finished."

.PHONY: all clean