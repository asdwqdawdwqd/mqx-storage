#ifndef STORAGE_H
#define STORAGE_H

#include "btree.h"
#include "page.h"
#include <stdint.h>

// 存储引擎结构
typedef struct {
    PageManager pm;
    BTree btree;
    bool initialized;
} StorageEngine;

// 初始化存储引擎
int storage_init(StorageEngine *engine, const char *db_file);

// 关闭存储引擎
int storage_close(StorageEngine *engine);

// 插入键值对
int storage_put(StorageEngine *engine, const char *key, const char *value);

// 获取值
int storage_get(StorageEngine *engine, const char *key, char *value, size_t value_size);

// 删除键值对
int storage_delete(StorageEngine *engine, const char *key);

#endif // STORAGE_H

