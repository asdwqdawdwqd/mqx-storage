#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main() {
    StorageEngine engine;
    char value[1024];
    
    printf("初始化存储引擎...\n");
    if (storage_init(&engine, "test.db") < 0) {
        fprintf(stderr, "初始化存储引擎失败\n");
        return 1;
    }
    
    printf("测试插入操作...\n");
    assert(storage_put(&engine, "name", "Alice") == 0);
    assert(storage_put(&engine, "age", "25") == 0);
    assert(storage_put(&engine, "city", "Beijing") == 0);
    assert(storage_put(&engine, "country", "China") == 0);
    
    printf("测试查找操作...\n");
    assert(storage_get(&engine, "name", value, sizeof(value)) == 0);
    assert(strcmp(value, "Alice") == 0);
    printf("  name = %s\n", value);
    
    assert(storage_get(&engine, "age", value, sizeof(value)) == 0);
    assert(strcmp(value, "25") == 0);
    printf("  age = %s\n", value);
    
    assert(storage_get(&engine, "city", value, sizeof(value)) == 0);
    assert(strcmp(value, "Beijing") == 0);
    printf("  city = %s\n", value);
    
    printf("测试更新操作...\n");
    assert(storage_put(&engine, "age", "26") == 0);
    assert(storage_get(&engine, "age", value, sizeof(value)) == 0);
    assert(strcmp(value, "26") == 0);
    printf("  更新后的 age = %s\n", value);
    
    printf("测试删除操作...\n");
    assert(storage_delete(&engine, "city") == 0);
    assert(storage_get(&engine, "city", value, sizeof(value)) != 0);
    printf("  删除 city 后，查找失败（预期行为）\n");
    
    printf("测试不存在的 key...\n");
    assert(storage_get(&engine, "nonexistent", value, sizeof(value)) != 0);
    printf("  查找不存在的 key 失败（预期行为）\n");
    
    printf("关闭存储引擎...\n");
    storage_close(&engine);
    
    printf("重新打开存储引擎测试持久化...\n");
    assert(storage_init(&engine, "test.db") == 0);
    assert(storage_get(&engine, "name", value, sizeof(value)) == 0);
    assert(strcmp(value, "Alice") == 0);
    printf("  持久化测试通过: name = %s\n", value);
    
    storage_close(&engine);
    
    printf("\n所有测试通过！\n");
    return 0;
}

