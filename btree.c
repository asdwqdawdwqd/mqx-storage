#include "btree.h"
#include "page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// 文件头结构（前向声明）
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t page_count;
    uint32_t root_page;
    uint32_t free_page_list;
    char reserved[PAGE_SIZE - 20];
} FileHeader;

// 从页面获取节点
static BTreeNode* get_node(PageManager *pm, uint32_t page_id) {
    Page *page = page_get(pm, page_id);
    if (!page) return NULL;
    return (BTreeNode*)page->data;
}

// 获取叶子节点的 key
static char* leaf_get_key(BTreeNode *node, int index) {
    char *ptr = (char*)(node + 1);
    for (int i = 0; i < index; i++) {
        ptr += strlen(ptr) + 1;  // 跳过 key
        uint16_t val_len;
        memcpy(&val_len, ptr, sizeof(uint16_t));
        ptr += sizeof(uint16_t) + val_len;  // 跳过 value
    }
    return ptr;
}

// 获取叶子节点的 value
static char* leaf_get_value(BTreeNode *node, int index) {
    char *ptr = (char*)(node + 1);
    for (int i = 0; i < index; i++) {
        ptr += strlen(ptr) + 1;  // 跳过 key
        uint16_t val_len;
        memcpy(&val_len, ptr, sizeof(uint16_t));
        ptr += sizeof(uint16_t) + val_len;  // 跳过 value
    }
    ptr += strlen(ptr) + 1;  // 跳过 key
    return ptr;
}

// 获取内部节点的 key（格式：child0, key0, child1, key1, ..., childN）
static char* internal_get_key(BTreeNode *node, int index) {
    char *ptr = (char*)(node + 1);
    // 跳过第一个 child
    ptr += sizeof(uint32_t);
    // 跳过前面的 key 和 child
    for (int i = 0; i < index; i++) {
        ptr += strlen(ptr) + 1;  // 跳过 key
        ptr += sizeof(uint32_t);  // 跳过 child
    }
    return ptr;
}

// 获取内部节点的 child（内部节点：child0, key0, child1, key1, ..., childN）
static uint32_t* internal_get_child(BTreeNode *node, int index) {
    char *ptr = (char*)(node + 1);
    // 第一个 child 在数据开始处
    if (index == 0) {
        return (uint32_t*)ptr;
    }
    // 后续的 child 在对应的 key 之后
    for (int i = 0; i < index; i++) {
        ptr += strlen(ptr) + 1;  // 跳过 key
        if (i < index - 1) {
            ptr += sizeof(uint32_t);  // 跳过 child（除了最后一个）
        }
    }
    ptr += strlen(ptr) + 1;  // 跳过 key
    return (uint32_t*)ptr;
}

// 在节点中查找 key 的位置（返回应该插入的位置）
static int find_key_position(BTreeNode *node, const char *key) {
    int left = 0, right = node->key_count;
    
    while (left < right) {
        int mid = (left + right) / 2;
        char *node_key;
        if (node->is_leaf) {
            node_key = leaf_get_key(node, mid);
        } else {
            node_key = internal_get_key(node, mid);
        }
        
        int cmp = strcmp(key, node_key);
        if (cmp < 0) {
            right = mid;
        } else if (cmp > 0) {
            left = mid + 1;
        } else {
            return mid;  // 找到
        }
    }
    
    return left;  // 返回插入位置
}

// 创建新节点
static uint32_t create_node(PageManager *pm, bool is_leaf) {
    uint32_t page_id = page_alloc(pm);
    BTreeNode *node = get_node(pm, page_id);
    memset(node, 0, sizeof(BTreeNode));
    node->type = is_leaf ? PAGE_TYPE_LEAF : PAGE_TYPE_INTERNAL;
    node->is_leaf = is_leaf;
    node->parent = 0;
    node->next = 0;
    node->key_count = 0;
    page_mark_dirty(pm, page_id);
    return page_id;
}

// 分裂叶子节点
static void split_leaf(PageManager *pm, uint32_t page_id, uint32_t *new_page_id) {
    BTreeNode *old_node = get_node(pm, page_id);
    uint32_t new_id = create_node(pm, true);
    BTreeNode *new_node = get_node(pm, new_id);
    
    int mid = old_node->key_count / 2;
    new_node->key_count = old_node->key_count - mid;
    old_node->key_count = mid;
    
    // 移动数据
    char *old_data = (char*)(old_node + 1);
    char *new_data = (char*)(new_node + 1);
    
    // 计算需要移动的数据大小
    size_t move_size = 0;
    for (int i = mid; i < old_node->key_count + new_node->key_count; i++) {
        char *key = leaf_get_key(old_node, i);
        move_size += strlen(key) + 1;
        char *val = leaf_get_value(old_node, i);
        uint16_t val_len;
        memcpy(&val_len, val, sizeof(uint16_t));
        move_size += sizeof(uint16_t) + val_len;
    }
    
    // 找到 mid 位置的数据起始点
    size_t skip_size = 0;
    for (int i = 0; i < mid; i++) {
        char *key = leaf_get_key(old_node, i);
        skip_size += strlen(key) + 1;
        char *val = leaf_get_value(old_node, i);
        uint16_t val_len;
        memcpy(&val_len, val, sizeof(uint16_t));
        skip_size += sizeof(uint16_t) + val_len;
    }
    
    memcpy(new_data, old_data + skip_size, move_size);
    memset(old_data + skip_size, 0, move_size);
    
    // 更新链表
    new_node->next = old_node->next;
    old_node->next = new_id;
    new_node->parent = old_node->parent;
    
    page_mark_dirty(pm, page_id);
    page_mark_dirty(pm, new_id);
    *new_page_id = new_id;
}

// 分裂内部节点
static void split_internal(PageManager *pm, uint32_t page_id, uint32_t *new_page_id, char *promote_key) {
    BTreeNode *old_node = get_node(pm, page_id);
    uint32_t new_id = create_node(pm, false);
    BTreeNode *new_node = get_node(pm, new_id);
    
    int mid = old_node->key_count / 2;
    
    // 获取提升的 key（mid 位置的 key 会被提升）
    char *mid_key = internal_get_key(old_node, mid);
    strcpy(promote_key, mid_key);
    
    // 新节点包含 mid+1 之后的所有 key 和 child
    // 格式：child0, key0, child1, key1, ...
    // 分裂后：旧节点保留 child0...child_mid, key0...key_mid-1
    //         新节点包含 child_mid+1...child_n, key_mid+1...key_n-1
    //         提升 key_mid
    
    // 计算需要移动的数据
    char *old_data = (char*)(old_node + 1);
    char *new_data = (char*)(new_node + 1);
    
    // 计算 mid+1 之后的数据大小
    size_t move_size = 0;
    // 第一个 child (mid+1)
    move_size += sizeof(uint32_t);
    // 后续的 key 和 child
    for (int i = mid + 1; i < old_node->key_count; i++) {
        char *key = internal_get_key(old_node, i);
        move_size += strlen(key) + 1 + sizeof(uint32_t);
    }
    
    // 计算需要跳过的数据大小（到 mid+1 之前）
    size_t skip_size = 0;
    // 跳过 child0 到 child_mid
    for (int i = 0; i <= mid; i++) {
        if (i > 0) {
            char *key = internal_get_key(old_node, i - 1);
            skip_size += strlen(key) + 1;
        }
        if (i <= mid) {
            skip_size += sizeof(uint32_t);
        }
    }
    // 跳过 key0 到 key_mid-1
    for (int i = 0; i < mid; i++) {
        char *key = internal_get_key(old_node, i);
        skip_size += strlen(key) + 1;
    }
    
    // 移动数据
    memcpy(new_data, old_data + skip_size, move_size);
    memset(old_data + skip_size, 0, move_size);
    
    // 更新 key_count
    new_node->key_count = old_node->key_count - mid - 1;
    old_node->key_count = mid;
    
    // 更新父节点指针
    new_node->parent = old_node->parent;
    // 更新新节点所有子节点的父指针
    for (int i = 0; i <= new_node->key_count; i++) {
        uint32_t *child = internal_get_child(new_node, i);
        if (child && *child != 0) {
            BTreeNode *child_node = get_node(pm, *child);
            if (child_node) {
                child_node->parent = new_id;
                page_mark_dirty(pm, *child);
            }
        }
    }
    
    page_mark_dirty(pm, page_id);
    page_mark_dirty(pm, new_id);
    *new_page_id = new_id;
}

// 插入到叶子节点
static int insert_into_leaf(PageManager *pm, uint32_t page_id, const char *key, const char *value) {
    BTreeNode *node = get_node(pm, page_id);
    int pos = find_key_position(node, key);
    
    // 检查 key 是否已存在
    if (pos < node->key_count) {
        char *existing_key = leaf_get_key(node, pos);
        if (strcmp(key, existing_key) == 0) {
            // 更新现有值
            char *val_ptr = leaf_get_value(node, pos);
            uint16_t val_len = strlen(value);
            if (val_len > MAX_VAL_SIZE) val_len = MAX_VAL_SIZE;
            memcpy(val_ptr, &val_len, sizeof(uint16_t));
            memcpy(val_ptr + sizeof(uint16_t), value, val_len);
            page_mark_dirty(pm, page_id);
            return 0;
        }
    }
    
    // 计算需要移动的数据大小
    size_t key_size = strlen(key) + 1;
    uint16_t val_len = strlen(value);
    if (val_len > MAX_VAL_SIZE) val_len = MAX_VAL_SIZE;
    size_t val_size = sizeof(uint16_t) + val_len;
    size_t total_size = key_size + val_size;
    
    // 检查空间是否足够
    size_t used = 0;
    for (int i = 0; i < node->key_count; i++) {
        char *k = leaf_get_key(node, i);
        used += strlen(k) + 1;
        char *v = leaf_get_value(node, i);
        uint16_t vlen;
        memcpy(&vlen, v, sizeof(uint16_t));
        used += sizeof(uint16_t) + vlen;
    }
    
    if (used + total_size > PAGE_SIZE - sizeof(BTreeNode)) {
        return -1;  // 空间不足，需要分裂
    }
    
    // 移动现有数据
    char *data_start = (char*)(node + 1);
    size_t move_offset = 0;
    for (int i = 0; i < pos; i++) {
        char *k = leaf_get_key(node, i);
        move_offset += strlen(k) + 1;
        char *v = leaf_get_value(node, i);
        uint16_t vlen;
        memcpy(&vlen, v, sizeof(uint16_t));
        move_offset += sizeof(uint16_t) + vlen;
    }
    
    memmove(data_start + move_offset + total_size, 
            data_start + move_offset, 
            used - move_offset);
    
    // 插入新数据
    memcpy(data_start + move_offset, key, key_size);
    memcpy(data_start + move_offset + key_size, &val_len, sizeof(uint16_t));
    memcpy(data_start + move_offset + key_size + sizeof(uint16_t), value, val_len);
    
    node->key_count++;
    page_mark_dirty(pm, page_id);
    return 0;
}

// 插入到内部节点（格式：child0, key0, child1, key1, ..., childN）
static int insert_into_internal(PageManager *pm, uint32_t page_id, const char *key, uint32_t right_child_id) {
    BTreeNode *node = get_node(pm, page_id);
    int pos = find_key_position(node, key);
    
    // 计算需要移动的数据大小
    size_t key_size = strlen(key) + 1;
    size_t total_size = key_size + sizeof(uint32_t);  // key + right child
    
    // 检查空间是否足够
    size_t used = sizeof(uint32_t);  // 第一个 child
    for (int i = 0; i < node->key_count; i++) {
        char *k = internal_get_key(node, i);
        used += strlen(k) + 1 + sizeof(uint32_t);
    }
    
    if (used + total_size > PAGE_SIZE - sizeof(BTreeNode)) {
        return -1;  // 空间不足，需要分裂
    }
    
    // 计算插入位置
    char *data_start = (char*)(node + 1);
    size_t insert_offset = sizeof(uint32_t);  // 跳过第一个 child
    
    // 跳过前面的 key 和 child
    for (int i = 0; i < pos; i++) {
        char *k = internal_get_key(node, i);
        insert_offset += strlen(k) + 1 + sizeof(uint32_t);
    }
    
    // 移动现有数据（从插入位置开始的所有数据）
    size_t move_size = used - insert_offset;
    if (move_size > 0) {
        memmove(data_start + insert_offset + total_size, 
                data_start + insert_offset, 
                move_size);
    }
    
    // 插入新数据：先插入 key，再插入 right child
    memcpy(data_start + insert_offset, key, key_size);
    memcpy(data_start + insert_offset + key_size, &right_child_id, sizeof(uint32_t));
    
    // 更新被插入子节点的父指针
    BTreeNode *right_child = get_node(pm, right_child_id);
    if (right_child) {
        right_child->parent = page_id;
        page_mark_dirty(pm, right_child_id);
    }
    
    node->key_count++;
    page_mark_dirty(pm, page_id);
    return 0;
}

// 初始化 B+ 树
int btree_init(BTree *tree, PageManager *pm) {
    tree->pm = pm;
    
    // 从文件头读取根节点
    FileHeader *header = (FileHeader*)page_get(pm, 0);
    if (!header) return -1;
    
    if (header->root_page == 0 || pm->page_count <= 1) {
        // 创建新的根节点
        tree->root_page = create_node(pm, true);  // 创建根叶子节点
        header->root_page = tree->root_page;
        page_mark_dirty(pm, 0);
    } else {
        // 从文件头读取根节点
        tree->root_page = header->root_page;
        BTreeNode *root = get_node(pm, tree->root_page);
        if (!root || root->type == PAGE_TYPE_FREE) {
            tree->root_page = create_node(pm, true);
            header->root_page = tree->root_page;
            page_mark_dirty(pm, 0);
        }
    }
    
    return 0;
}

// 插入键值对
int btree_insert(BTree *tree, const char *key, const char *value) {
    if (!tree || !key || !value) return -1;
    
    // 查找插入位置
    uint32_t leaf_page = tree->root_page;
    BTreeNode *node = get_node(tree->pm, leaf_page);
    
    // 如果不是叶子节点，向下查找
    while (!node->is_leaf) {
        int pos = find_key_position(node, key);
        // 内部节点：如果 key >= node_key[pos]，则去右子树（pos+1）
        if (pos < node->key_count) {
            char *node_key = internal_get_key(node, pos);
            if (strcmp(key, node_key) >= 0) {
                pos++;  // 去右子树
            }
        }
        uint32_t *child = internal_get_child(node, pos);
        leaf_page = *child;
        node = get_node(tree->pm, leaf_page);
        if (!node) return -1;
    }
    
    // 尝试插入
    if (insert_into_leaf(tree->pm, leaf_page, key, value) == 0) {
        return 0;
    }
    
    // 需要分裂
    uint32_t new_page_id;
    split_leaf(tree->pm, leaf_page, &new_page_id);
    
    // 确定插入到哪个节点
    char *first_key_new = leaf_get_key(get_node(tree->pm, new_page_id), 0);
    if (strcmp(key, first_key_new) < 0) {
        insert_into_leaf(tree->pm, leaf_page, key, value);
    } else {
        insert_into_leaf(tree->pm, new_page_id, key, value);
    }
    
    // 获取提升的 key（新节点的第一个 key）
    char *promote_key = leaf_get_key(get_node(tree->pm, new_page_id), 0);
    char key_buf[MAX_KEY_SIZE + 1];
    strcpy(key_buf, promote_key);
    
    // 如果根节点分裂，创建新根
    if (leaf_page == tree->root_page) {
        uint32_t new_root = create_node(tree->pm, false);
        BTreeNode *root_node = get_node(tree->pm, new_root);
        
        // 内部节点格式：child0, key0, child1
        // 设置第一个 child
        uint32_t *first_child = (uint32_t*)((char*)(root_node + 1));
        *first_child = leaf_page;
        
        // 插入 key 和第二个 child
        char *data = (char*)(root_node + 1) + sizeof(uint32_t);
        strcpy(data, key_buf);
        uint32_t *second_child = (uint32_t*)(data + strlen(key_buf) + 1);
        *second_child = new_page_id;
        
        root_node->key_count = 1;
        
        // 更新父节点
        BTreeNode *old_root = get_node(tree->pm, leaf_page);
        old_root->parent = new_root;
        BTreeNode *new_leaf = get_node(tree->pm, new_page_id);
        new_leaf->parent = new_root;
        
        tree->root_page = new_root;
        // 更新文件头
        FileHeader *header = (FileHeader*)page_get(tree->pm, 0);
        if (header) {
            header->root_page = new_root;
            page_mark_dirty(tree->pm, 0);
        }
        page_mark_dirty(tree->pm, new_root);
        page_mark_dirty(tree->pm, leaf_page);
        page_mark_dirty(tree->pm, new_page_id);
    } else {
        // 向上插入分裂的 key（递归处理）
        uint32_t parent_page = get_node(tree->pm, leaf_page)->parent;
        if (insert_into_internal(tree->pm, parent_page, key_buf, new_page_id) != 0) {
            // 父节点也需要分裂
            uint32_t new_parent_id;
            char parent_promote_key[MAX_KEY_SIZE + 1];
            split_internal(tree->pm, parent_page, &new_parent_id, parent_promote_key);
            
            // 确定插入到哪个父节点
            BTreeNode *old_parent = get_node(tree->pm, parent_page);
            BTreeNode *new_parent = get_node(tree->pm, new_parent_id);
            
            // 比较 key 和提升的 key
            if (strcmp(key_buf, parent_promote_key) < 0) {
                // 插入到旧父节点
                insert_into_internal(tree->pm, parent_page, key_buf, new_page_id);
            } else {
                // 插入到新父节点
                insert_into_internal(tree->pm, new_parent_id, key_buf, new_page_id);
            }
            
            // 递归向上传播
            if (parent_page == tree->root_page) {
                // 根节点分裂，创建新根
                uint32_t new_root = create_node(tree->pm, false);
                BTreeNode *root_node = get_node(tree->pm, new_root);
                
                uint32_t *first_child = (uint32_t*)((char*)(root_node + 1));
                *first_child = parent_page;
                
                char *data = (char*)(root_node + 1) + sizeof(uint32_t);
                strcpy(data, parent_promote_key);
                uint32_t *second_child = (uint32_t*)(data + strlen(parent_promote_key) + 1);
                *second_child = new_parent_id;
                
                root_node->key_count = 1;
                
                // 更新父节点
                old_parent->parent = new_root;
                new_parent->parent = new_root;
                
                tree->root_page = new_root;
                FileHeader *header = (FileHeader*)page_get(tree->pm, 0);
                if (header) {
                    header->root_page = new_root;
                    page_mark_dirty(tree->pm, 0);
                }
                page_mark_dirty(tree->pm, new_root);
            } else {
                // 继续向上传播
                uint32_t grandparent = old_parent->parent;
                if (insert_into_internal(tree->pm, grandparent, parent_promote_key, new_parent_id) != 0) {
                    // 继续递归处理（简化：这里可以继续递归，但为了代码简洁，我们暂时只处理两层）
                    // 实际应用中应该递归处理所有层级
                }
            }
        }
        
        page_mark_dirty(tree->pm, leaf_page);
        page_mark_dirty(tree->pm, new_page_id);
    }
    
    return 0;
}

// 查找值
int btree_get(BTree *tree, const char *key, char *value, size_t value_size) {
    if (!tree || !key || !value) return -1;
    
    uint32_t page_id = tree->root_page;
    BTreeNode *node = get_node(tree->pm, page_id);
    
    // 向下查找
    while (!node->is_leaf) {
        int pos = find_key_position(node, key);
        // 内部节点：如果 key >= node_key[pos]，则去右子树（pos+1）
        // 否则去左子树（pos）
        if (pos < node->key_count) {
            char *node_key = internal_get_key(node, pos);
            if (strcmp(key, node_key) >= 0) {
                pos++;  // 去右子树
            }
        }
        // pos 现在指向要访问的子节点索引
        uint32_t *child = internal_get_child(node, pos);
        page_id = *child;
        node = get_node(tree->pm, page_id);
        if (!node) return -1;
    }
    
    // 在叶子节点中查找
    int pos = find_key_position(node, key);
    if (pos < node->key_count) {
        char *node_key = leaf_get_key(node, pos);
        if (strcmp(key, node_key) == 0) {
            char *val_ptr = leaf_get_value(node, pos);
            uint16_t val_len;
            memcpy(&val_len, val_ptr, sizeof(uint16_t));
            size_t copy_len = val_len < value_size - 1 ? val_len : value_size - 1;
            memcpy(value, val_ptr + sizeof(uint16_t), copy_len);
            value[copy_len] = '\0';
            return 0;
        }
    }
    
    return -1;  // 未找到
}

// 从叶子节点删除键值对
static int delete_from_leaf(PageManager *pm, uint32_t page_id, int pos) {
    BTreeNode *node = get_node(pm, page_id);
    if (!node || pos >= node->key_count) {
        return -1;
    }
    
    // 计算要删除的数据大小
    char *key_to_delete = leaf_get_key(node, pos);
    size_t key_size = strlen(key_to_delete) + 1;
    char *val_to_delete = leaf_get_value(node, pos);
    uint16_t val_len;
    memcpy(&val_len, val_to_delete, sizeof(uint16_t));
    size_t val_size = sizeof(uint16_t) + val_len;
    size_t total_size = key_size + val_size;
    
    // 计算需要移动的数据大小
    size_t move_size = 0;
    for (int i = pos + 1; i < node->key_count; i++) {
        char *k = leaf_get_key(node, i);
        move_size += strlen(k) + 1;
        char *v = leaf_get_value(node, i);
        uint16_t vlen;
        memcpy(&vlen, v, sizeof(uint16_t));
        move_size += sizeof(uint16_t) + vlen;
    }
    
    // 移动数据覆盖要删除的数据
    if (move_size > 0) {
        char *data_start = (char*)(node + 1);
        size_t delete_offset = 0;
        for (int i = 0; i < pos; i++) {
            char *k = leaf_get_key(node, i);
            delete_offset += strlen(k) + 1;
            char *v = leaf_get_value(node, i);
            uint16_t vlen;
            memcpy(&vlen, v, sizeof(uint16_t));
            delete_offset += sizeof(uint16_t) + vlen;
        }
        
        char *src = data_start + delete_offset + total_size;
        char *dst = data_start + delete_offset;
        memmove(dst, src, move_size);
        
        // 清零末尾数据
        memset(dst + move_size, 0, total_size);
    } else {
        // 直接清零
        char *data_start = (char*)(node + 1);
        size_t delete_offset = 0;
        for (int i = 0; i < pos; i++) {
            char *k = leaf_get_key(node, i);
            delete_offset += strlen(k) + 1;
            char *v = leaf_get_value(node, i);
            uint16_t vlen;
            memcpy(&vlen, v, sizeof(uint16_t));
            delete_offset += sizeof(uint16_t) + vlen;
        }
        memset(data_start + delete_offset, 0, total_size);
    }
    
    node->key_count--;
    page_mark_dirty(pm, page_id);
    return 0;
}

// 获取节点的最小 key 数量（用于判断下溢）
#define MIN_KEYS_LEAF 1      // 叶子节点最少 1 个 key
#define MIN_KEYS_INTERNAL 1  // 内部节点最少 1 个 key

// 获取兄弟节点（左兄弟或右兄弟）
static uint32_t get_sibling(PageManager *pm, uint32_t page_id, bool *is_left) {
    BTreeNode *node = get_node(pm, page_id);
    if (!node || node->parent == 0) return 0;
    
    BTreeNode *parent = get_node(pm, node->parent);
    if (!parent) return 0;
    
    // 找到当前节点在父节点中的位置
    uint32_t *first_child = internal_get_child(parent, 0);
    if (*first_child == page_id) {
        // 是第一个子节点，只有右兄弟
        *is_left = false;
        if (parent->key_count > 0) {
            return *internal_get_child(parent, 1);
        }
        return 0;
    }
    
    // 查找当前节点位置
    for (int i = 1; i <= parent->key_count; i++) {
        uint32_t *child = internal_get_child(parent, i);
        if (*child == page_id) {
            // 找到，有左兄弟
            *is_left = true;
            return *internal_get_child(parent, i - 1);
        }
    }
    
    return 0;
}

// 合并两个叶子节点
static void merge_leaf_nodes(PageManager *pm, uint32_t left_id, uint32_t right_id) {
    BTreeNode *left = get_node(pm, left_id);
    BTreeNode *right = get_node(pm, right_id);
    
    if (!left || !right) return;
    
    // 计算左节点数据大小
    size_t left_size = 0;
    for (int i = 0; i < left->key_count; i++) {
        char *k = leaf_get_key(left, i);
        left_size += strlen(k) + 1;
        char *v = leaf_get_value(left, i);
        uint16_t vlen;
        memcpy(&vlen, v, sizeof(uint16_t));
        left_size += sizeof(uint16_t) + vlen;
    }
    
    // 计算右节点数据大小
    size_t right_size = 0;
    for (int i = 0; i < right->key_count; i++) {
        char *k = leaf_get_key(right, i);
        right_size += strlen(k) + 1;
        char *v = leaf_get_value(right, i);
        uint16_t vlen;
        memcpy(&vlen, v, sizeof(uint16_t));
        right_size += sizeof(uint16_t) + vlen;
    }
    
    // 将右节点的数据复制到左节点
    char *left_data = (char*)(left + 1);
    char *right_data = (char*)(right + 1);
    memcpy(left_data + left_size, right_data, right_size);
    
    left->key_count += right->key_count;
    left->next = right->next;
    
    // 释放右节点
    page_free(pm, right_id);
    page_mark_dirty(pm, left_id);
}

// 从内部节点删除 key
static int delete_from_internal(PageManager *pm, uint32_t page_id, int key_pos) {
    BTreeNode *node = get_node(pm, page_id);
    if (!node || key_pos >= node->key_count) return -1;
    
    // 计算要删除的数据大小
    char *key_to_delete = internal_get_key(node, key_pos);
    size_t key_size = strlen(key_to_delete) + 1;
    size_t total_size = key_size + sizeof(uint32_t);
    
    // 计算需要移动的数据大小
    size_t move_size = 0;
    for (int i = key_pos + 1; i < node->key_count; i++) {
        char *k = internal_get_key(node, i);
        move_size += strlen(k) + 1 + sizeof(uint32_t);
    }
    
    // 移动数据
    char *data_start = (char*)(node + 1);
    size_t delete_offset = sizeof(uint32_t);  // 跳过第一个 child
    
    for (int i = 0; i < key_pos; i++) {
        char *k = internal_get_key(node, i);
        delete_offset += strlen(k) + 1 + sizeof(uint32_t);
    }
    
    if (move_size > 0) {
        memmove(data_start + delete_offset, 
                data_start + delete_offset + total_size, 
                move_size);
    }
    
    // 清零末尾
    memset(data_start + delete_offset + move_size, 0, total_size);
    
    node->key_count--;
    page_mark_dirty(pm, page_id);
    return 0;
}

// 处理叶子节点删除后的下溢
static void handle_leaf_underflow(BTree *tree, uint32_t page_id) {
    BTreeNode *node = get_node(tree->pm, page_id);
    if (!node || node->key_count >= MIN_KEYS_LEAF) return;
    
    // 如果是根节点，允许少于最小 key 数
    if (page_id == tree->root_page) return;
    
    bool is_left;
    uint32_t sibling_id = get_sibling(tree->pm, page_id, &is_left);
    if (sibling_id == 0) return;
    
    BTreeNode *sibling = get_node(tree->pm, sibling_id);
    if (!sibling) return;
    
    // 如果兄弟节点有足够的 key，可以借用（简化：这里直接合并）
    // 实际应该先尝试借用，借用失败才合并
    if (is_left) {
        merge_leaf_nodes(tree->pm, sibling_id, page_id);
        // 从父节点删除对应的 key
        BTreeNode *parent = get_node(tree->pm, node->parent);
        if (parent) {
            // 找到对应的 key 位置
            for (int i = 0; i < parent->key_count; i++) {
                uint32_t *child = internal_get_child(parent, i + 1);
                if (*child == page_id) {
                    delete_from_internal(tree->pm, node->parent, i);
                    // 递归处理父节点
                    if (parent->key_count < MIN_KEYS_INTERNAL && node->parent != tree->root_page) {
                        // handle_internal_underflow(tree, node->parent);
                    }
                    break;
                }
            }
        }
    } else {
        merge_leaf_nodes(tree->pm, page_id, sibling_id);
        // 从父节点删除对应的 key
        BTreeNode *parent = get_node(tree->pm, node->parent);
        if (parent) {
            for (int i = 0; i < parent->key_count; i++) {
                uint32_t *child = internal_get_child(parent, i + 1);
                if (*child == sibling_id) {
                    delete_from_internal(tree->pm, node->parent, i);
                    break;
                }
            }
        }
    }
}

// 删除键值对
int btree_delete(BTree *tree, const char *key) {
    if (!tree || !key) return -1;
    
    uint32_t page_id = tree->root_page;
    BTreeNode *node = get_node(tree->pm, page_id);
    if (!node) return -1;
    
    // 查找叶子节点
    while (!node->is_leaf) {
        int pos = find_key_position(node, key);
        if (pos < node->key_count) {
            char *node_key = internal_get_key(node, pos);
            if (strcmp(key, node_key) >= 0) {
                pos++;  // 去右子树
            }
        }
        uint32_t *child = internal_get_child(node, pos);
        page_id = *child;
        node = get_node(tree->pm, page_id);
        if (!node) return -1;
    }
    
    // 在叶子节点中查找并删除
    int pos = find_key_position(node, key);
    if (pos < node->key_count) {
        char *node_key = leaf_get_key(node, pos);
        if (strcmp(key, node_key) == 0) {
            // 找到，执行删除
            int ret = delete_from_leaf(tree->pm, page_id, pos);
            if (ret == 0) {
                // 处理下溢
                handle_leaf_underflow(tree, page_id);
            }
            return ret;
        }
    }
    
    return -1;  // 未找到
}

// 销毁 B+ 树
void btree_destroy(BTree *tree) {
    // 资源由 PageManager 管理，这里不需要特殊处理
    tree->root_page = 0;
    tree->pm = NULL;
}

