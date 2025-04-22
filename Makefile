# --- Объектные файлы ---
SVM_C_OBJS = $(SVM_C_SRCS:.c=.o)
# Эта строка должна правильно преобразовать .c и .cpp в .o
SVM_CXX_OBJS = $(SVM_CXX_SRCS:.cpp=.o) $(filter %.c, $(SVM_CXX_SRCS):.c=.o) # <-- Явно фильтруем .c

# SVM_MAIN_C_OBJ больше не нужен

COMMON_C_OBJS = $(COMMON_C_SRCS:.c=.o)
UVM_OBJS = $(UVM_SRCS:.c=.o) $(COMMON_C_OBJS)

# Файлы, генерируемые MOC
SVM_HEADERS_WITH_QOBJECT = $(shell grep -l Q_OBJECT svm/*.h)
# Проверяем, что список не пустой перед генерацией moc файлов
ifneq ($(strip $(SVM_HEADERS_WITH_QOBJECT)),)
    SVM_MOCS = $(patsubst %.h,moc_%.cpp,$(notdir $(SVM_HEADERS_WITH_QOBJECT)))
    SVM_MOC_OBJS = $(SVM_MOCS:.cpp=.o)
else
    SVM_MOCS =
    SVM_MOC_OBJS =
endif

# Объектные файлы для SVM = SVM модули(C) + SVM Main+GUI(C++) + MOC(C++) + Общие(C)
SVM_ALL_OBJS = $(SVM_C_OBJS) $(SVM_CXX_OBJS) $(SVM_MOC_OBJS) $(COMMON_C_OBJS)

# --- Правила сборки ---
all: $(SVM_TARGET) $(UVM_TARGET)

# Правило для сборки SVM (используем CXX для линковки)
$(SVM_TARGET): $(SVM_ALL_OBJS) # Зависит от объектных файлов
	@echo "Linking $(SVM_TARGET)..."
	$(CXX) $(CXXFLAGS) $(SVM_ALL_OBJS) -o $(SVM_TARGET) $(LDFLAGS) $(LIBS)
	@echo "SVM application ($(SVM_TARGET)) built successfully."

# Правило для сборки UVM (используем CC)
$(UVM_TARGET): $(UVM_OBJS)
	@echo "Linking $(UVM_TARGET)..."
	$(CC) $(CFLAGS) $(UVM_OBJS) -o $(UVM_TARGET) $(LDFLAGS) $(LIBS)
	@echo "UVM application ($(UVM_TARGET)) built successfully."

# Правило компиляции C файлов (для всего, КРОМЕ svm_main.c)
%.o: %.c
	@echo "Compiling (C) $<..."
	$(CC) $(CFLAGS) -c -o $@ $<

# Правило компиляции C++ файлов (включая .cpp MOC'а)
%.o: %.cpp
	@echo "Compiling (C++) $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Явное правило для компиляции svm_main.c с помощью CXX (оставляем)
svm/svm_main.o: svm/svm_main.c $(wildcard svm/*.h) $(wildcard config/*.h) # Добавляем зависимости от .h
	@echo "Compiling (C++-Linkable C) $<..."
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Правило для MOC
moc_%.cpp: svm/%.h
	@echo "Running MOC on $<..."
	$(MOC) $< -o $@

# Правило для очистки (добавляем очистку MOC объектов)
clean:
	@echo "Cleaning up build files..."
	rm -f $(SVM_TARGET) $(UVM_TARGET) \
	      $(SVM_C_SRCS:.c=.o) $(UVM_SRCS:.c=.o) $(COMMON_C_OBJS) \
	      $(SVM_CXX_SRCS:.cpp=.o) $(filter %.c, $(SVM_CXX_SRCS):.c=.o) \
          $(SVM_MOC_OBJS) \
	      moc_*.cpp core.* *.core *~
	@echo "Cleanup finished."

.PHONY: all clean