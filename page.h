#ifndef PAGE_H
#define PAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PAGE_SIZE 4096        // 页面大小 4KB
#define MAX_PAGES 1024        // 最大页面数

// 页面类型
typedef enum {
    PAGE_TYPE_FREE = 0,       // 空闲页面
    PAGE_TYPE_LEAF = 1,       // B+ 树叶子节点
    PAGE_TYPE_INTERNAL = 2,   // B+ 树内部节点
    PAGE_TYPE_HEADER = 3      // 文件头页面
} PageType;

// 页面结构
typedef struct {
    uint8_t data[PAGE_SIZE];
} Page;

// 页面管理器
typedef struct {
    int fd_index;             // 索引文件描述符
    int fd_data;              // 数据文件描述符
    void *mmap_index;         // 索引文件 mmap 映射
    void *mmap_data;          // 数据文件 mmap 映射
    size_t index_size;        // 索引文件大小
    size_t data_size;         // 数据文件大小
    uint32_t page_count;      // 当前页面数
    uint32_t free_page_list;  // 空闲页面链表头
    bool need_sync;           // 是否需要同步
} PageManager;

// 初始化页面管理器
int page_manager_init(PageManager *pm, const char *filename);

// 关闭页面管理器
int page_manager_close(PageManager *pm);

// 分配新页面
uint32_t page_alloc(PageManager *pm);

// 释放页面
void page_free(PageManager *pm, uint32_t page_id);

// 读取页面
Page* page_get(PageManager *pm, uint32_t page_id);

// 标记页面为脏
void page_mark_dirty(PageManager *pm, uint32_t page_id);

// 刷新所有脏页到磁盘
int page_flush(PageManager *pm);

// 刷新指定页面到磁盘
int page_flush_page(PageManager *pm, uint32_t page_id);

#endif // PAGE_H

