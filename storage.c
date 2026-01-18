#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 初始化存储引擎
int storage_init(StorageEngine *engine, const char *db_file) {
    if (!engine || !db_file) {
        return -1;
    }
    
    memset(engine, 0, sizeof(StorageEngine));
    
    // 初始化页面管理器
    if (page_manager_init(&engine->pm, db_file) < 0) {
        return -1;
    }
    
    // 初始化 B+ 树
    if (btree_init(&engine->btree, &engine->pm) < 0) {
        page_manager_close(&engine->pm);
        return -1;
    }
    
    engine->initialized = true;
    return 0;
}

// 关闭存储引擎
int storage_close(StorageEngine *engine) {
    if (!engine || !engine->initialized) {
        return -1;
    }
    
    // 刷新所有页面
    page_flush(&engine->pm);
    
    // 关闭 B+ 树
    btree_destroy(&engine->btree);
    
    // 关闭页面管理器
    page_manager_close(&engine->pm);
    
    engine->initialized = false;
    return 0;
}

// 插入键值对
int storage_put(StorageEngine *engine, const char *key, const char *value) {
    if (!engine || !engine->initialized || !key || !value) {
        return -1;
    }
    
    if (strlen(key) > MAX_KEY_SIZE || strlen(value) > MAX_VAL_SIZE) {
        return -1;
    }
    
    return btree_insert(&engine->btree, key, value);
}

// 获取值
int storage_get(StorageEngine *engine, const char *key, char *value, size_t value_size) {
    if (!engine || !engine->initialized || !key || !value) {
        return -1;
    }
    
    return btree_get(&engine->btree, key, value, value_size);
}

// 删除键值对
int storage_delete(StorageEngine *engine, const char *key) {
    if (!engine || !engine->initialized || !key) {
        return -1;
    }
    
    return btree_delete(&engine->btree, key);
}

