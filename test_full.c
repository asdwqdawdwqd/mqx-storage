#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

// 测试大量插入和查找
void test_large_insert() {
    printf("\n=== 测试大量插入 ===\n");
    StorageEngine engine;
    char value[1024];
    char key[64];
    
    assert(storage_init(&engine, "test_large.db") == 0);
    
    // 插入 100 个键值对
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        assert(storage_put(&engine, key, value) == 0);
    }
    
    // 验证所有数据
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        char result[1024];
        assert(storage_get(&engine, key, result, sizeof(result)) == 0);
        assert(strcmp(result, value) == 0);
    }
    
    printf("  插入和查找 100 个键值对：通过\n");
    
    storage_close(&engine);
}

// 测试删除和合并
void test_delete_merge() {
    printf("\n=== 测试删除和节点合并 ===\n");
    StorageEngine engine;
    char value[1024];
    char key[64];
    
    assert(storage_init(&engine, "test_delete.db") == 0);
    
    // 插入数据
    for (int i = 0; i < 20; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        assert(storage_put(&engine, key, value) == 0);
    }
    
    // 删除部分数据
    for (int i = 5; i < 15; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        assert(storage_delete(&engine, key) == 0);
    }
    
    // 验证删除的数据不存在
    for (int i = 5; i < 15; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        assert(storage_get(&engine, key, value, sizeof(value)) != 0);
    }
    
    // 验证未删除的数据仍然存在
    for (int i = 0; i < 5; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        assert(storage_get(&engine, key, value, sizeof(value)) == 0);
    }
    
    for (int i = 15; i < 20; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        assert(storage_get(&engine, key, value, sizeof(value)) == 0);
    }
    
    printf("  删除和合并测试：通过\n");
    
    storage_close(&engine);
}

// 测试多层级分裂
void test_multi_level_split() {
    printf("\n=== 测试多层级分裂 ===\n");
    StorageEngine engine;
    char value[1024];
    char key[64];
    
    assert(storage_init(&engine, "test_split.db") == 0);
    
    // 插入足够多的数据以触发多层级分裂
    // B+ 树阶数为 4，每个节点最多 4 个 key
    // 插入 50 个应该能触发多层级分裂
    for (int i = 0; i < 50; i++) {
        snprintf(key, sizeof(key), "key%03d", i);
        snprintf(value, sizeof(value), "value%03d", i);
        assert(storage_put(&engine, key, value) == 0);
    }
    
    // 验证所有数据
    int found = 0;
    for (int i = 0; i < 50; i++) {
        snprintf(key, sizeof(key), "key%03d", i);
        char result[1024];
        if (storage_get(&engine, key, result, sizeof(result)) == 0) {
            found++;
        }
    }
    
    printf("  插入 50 个键值对，成功查找 %d 个\n", found);
    assert(found == 50);
    
    storage_close(&engine);
}

// 测试更新操作
void test_update() {
    printf("\n=== 测试更新操作 ===\n");
    StorageEngine engine;
    char value[1024];
    
    assert(storage_init(&engine, "test_update.db") == 0);
    
    // 插入
    assert(storage_put(&engine, "test", "value1") == 0);
    assert(storage_get(&engine, "test", value, sizeof(value)) == 0);
    assert(strcmp(value, "value1") == 0);
    
    // 更新
    assert(storage_put(&engine, "test", "value2") == 0);
    assert(storage_get(&engine, "test", value, sizeof(value)) == 0);
    assert(strcmp(value, "value2") == 0);
    
    // 再次更新
    assert(storage_put(&engine, "test", "value3") == 0);
    assert(storage_get(&engine, "test", value, sizeof(value)) == 0);
    assert(strcmp(value, "value3") == 0);
    
    printf("  更新操作测试：通过\n");
    
    storage_close(&engine);
}

// 测试持久化
void test_persistence() {
    printf("\n=== 测试持久化 ===\n");
    StorageEngine engine;
    char value[1024];
    char key[64];
    
    // 第一次打开，插入数据
    assert(storage_init(&engine, "test_persist.db") == 0);
    for (int i = 0; i < 30; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        assert(storage_put(&engine, key, value) == 0);
    }
    storage_close(&engine);
    
    // 第二次打开，验证数据
    assert(storage_init(&engine, "test_persist.db") == 0);
    for (int i = 0; i < 30; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        char result[1024];
        assert(storage_get(&engine, key, result, sizeof(result)) == 0);
        assert(strcmp(result, value) == 0);
    }
    
    printf("  持久化测试：通过（30 个键值对）\n");
    
    storage_close(&engine);
}

int main() {
    printf("开始完整 B+ 树功能测试...\n");
    
    test_large_insert();
    test_delete_merge();
    test_multi_level_split();
    test_update();
    test_persistence();
    
    printf("\n所有完整功能测试通过！\n");
    return 0;
}
