#ifndef BTREE_H
#define BTREE_H

#include "page.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// B+ 树配置
#define BTREE_ORDER 4         // B+ 树的阶数（每个节点最多 key 数量）
#define MAX_KEY_SIZE 255      // 最大 key 长度
#define MAX_VAL_SIZE 1024     // 最大 value 长度

// B+ 树节点结构（存储在页面中）
typedef struct {
    PageType type;            // 节点类型
    uint32_t parent;          // 父节点页面 ID
    uint32_t next;            // 下一个叶子节点（仅叶子节点使用）
    uint16_t key_count;       // 当前 key 数量
    uint16_t is_leaf;         // 是否为叶子节点
    // 数据部分：
    // 对于叶子节点：key1, val1, key2, val2, ...
    // 对于内部节点：key1, child1, key2, child2, ...
} BTreeNode;

// B+ 树结构
typedef struct {
    PageManager *pm;
    uint32_t root_page;       // 根节点页面 ID
} BTree;

// 初始化 B+ 树
int btree_init(BTree *tree, PageManager *pm);

// 插入键值对
int btree_insert(BTree *tree, const char *key, const char *value);

// 查找值
int btree_get(BTree *tree, const char *key, char *value, size_t value_size);

// 删除键值对
int btree_delete(BTree *tree, const char *key);

// 销毁 B+ 树（释放资源）
void btree_destroy(BTree *tree);

#endif // BTREE_H

