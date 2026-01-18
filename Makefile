CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -O2
LDFLAGS = 

# 源文件
SOURCES = page.c btree.c storage.c
OBJECTS = $(SOURCES:.c=.o)
HEADERS = page.h btree.h storage.h

# 目标
TARGET = libstorage.a
TEST_TARGET = test_storage
TEST_FULL_TARGET = test_full

.PHONY: all clean test test-full

all: $(TARGET)

# 静态库
$(TARGET): $(OBJECTS)
	ar rcs $@ $^

# 测试程序
test: $(TEST_TARGET)

$(TEST_TARGET): test.c $(TARGET)
	$(CC) $(CFLAGS) -o $@ $< -L. -lstorage $(LDFLAGS)

# 完整功能测试
test-full: $(TEST_FULL_TARGET)

$(TEST_FULL_TARGET): test_full.c $(TARGET)
	$(CC) $(CFLAGS) -o $@ $< -L. -lstorage $(LDFLAGS)

# 编译目标文件
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET) $(TEST_TARGET) test.db

