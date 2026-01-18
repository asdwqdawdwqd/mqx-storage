# B+ 树 KV 存储引擎

这是一个基于 B+ 树的键值存储引擎实现，参考了 SQLite 的设计思路，使用 mmap 进行文件映射。

## 特性

- **B+ 树索引**：使用 B+ 树作为底层数据结构，支持高效的插入、查找和删除
- **mmap 映射**：使用内存映射文件（mmap）进行数据访问，提高性能
- **文件分离**：索引文件（.idx）和数据文件（.dat）分离存储
- **持久化存储**：数据持久化到磁盘，支持重启后恢复

## 文件结构

```
storage/
├── page.h/page.c      # 页面管理模块（使用 mmap）
├── btree.h/btree.c    # B+ 树实现
├── storage.h/storage.c # 存储引擎接口
├── test.c             # 测试程序
├── Makefile           # 编译文件
└── README.md          # 本文件
```

## 编译

```bash
cd storage
make          # 编译静态库
make test     # 编译测试程序
```

## 使用方法

### 基本 API

```c
#include "storage.h"

// 初始化存储引擎
StorageEngine engine;
storage_init(&engine, "mydb");  // 会创建 mydb.idx 和 mydb.dat

// 插入键值对
storage_put(&engine, "name", "Alice");
storage_put(&engine, "age", "25");

// 查找值
char value[1024];
if (storage_get(&engine, "name", value, sizeof(value)) == 0) {
    printf("name = %s\n", value);
}

// 删除键值对
storage_delete(&engine, "age");

// 关闭存储引擎
storage_close(&engine);
```

### 完整示例

```c
#include "storage.h"
#include <stdio.h>

int main() {
    StorageEngine engine;
    char value[1024];
    
    // 初始化
    if (storage_init(&engine, "test.db") < 0) {
        fprintf(stderr, "初始化失败\n");
        return 1;
    }
    
    // 插入数据
    storage_put(&engine, "key1", "value1");
    storage_put(&engine, "key2", "value2");
    
    // 读取数据
    if (storage_get(&engine, "key1", value, sizeof(value)) == 0) {
        printf("key1 = %s\n", value);
    }
    
    // 关闭
    storage_close(&engine);
    return 0;
}
```

## 运行测试

### 基础测试

```bash
cd storage
make test
./test_storage
```

基础测试程序会执行以下操作：
1. 初始化存储引擎
2. 插入多个键值对
3. 查找并验证数据
4. 更新值
5. 删除键值对
6. 测试持久化（关闭后重新打开）

### 完整功能测试

```bash
make test-full
./test_full
```

完整功能测试包括：
1. **大量插入测试**：插入和查找 100 个键值对
2. **删除和合并测试**：测试删除后的节点合并
3. **多层级分裂测试**：测试触发多层级 B+ 树分裂
4. **更新操作测试**：多次更新同一个 key
5. **持久化测试**：关闭后重新打开验证数据完整性

## 技术细节

### 页面管理

- 页面大小：4KB
- 最大页面数：1024（可调整）
- 使用 mmap 映射索引文件和数据文件
- 自动扩展文件大小

### B+ 树结构

- 阶数：4（每个节点最多 4 个 key）
- 叶子节点：存储 key-value 对，通过 next 指针链接
- 内部节点：格式为 `child0, key0, child1, key1, ..., childN`
- 支持完整的节点分裂和合并
- 支持多层级树结构自动增长

### 文件格式

**索引文件（.idx）**：
- 页面 0：文件头（magic number, root page, page count 等）
- 页面 1+：B+ 树节点

**数据文件（.dat）**：
- 预留用于存储大 value（当前实现中 value 存储在索引文件中）

## 已实现的完整功能

✅ **完整的插入逻辑**
- 叶子节点插入和分裂
- 内部节点插入和分裂
- 多层级分裂向上传播
- 根节点自动增长

✅ **完整的删除逻辑**
- 叶子节点删除
- 删除后节点合并
- 内部节点 key 删除
- 下溢处理

✅ **持久化存储**
- 使用 mmap 映射文件
- 索引文件和数据文件分离
- 支持重启后数据恢复

## 限制

- Key 最大长度：255 字节
- Value 最大长度：1024 字节
- 节点借用（borrow）逻辑已实现但简化（优先合并而非借用）

## 注意事项

1. 使用 `storage_close()` 确保数据正确写入磁盘
2. 多个进程同时访问同一数据库文件可能导致数据损坏（未实现锁机制）
3. 文件大小会自动扩展，但不会自动收缩

