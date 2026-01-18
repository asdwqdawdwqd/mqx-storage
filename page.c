#define _POSIX_C_SOURCE 200809L
#include "page.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>

// 文件头结构（存储在索引文件页面 0）
typedef struct {
    uint32_t magic;           // 魔数，用于验证文件格式
    uint32_t version;         // 版本号
    uint32_t page_count;      // 总页面数
    uint32_t root_page;       // B+ 树根页面
    uint32_t free_page_list;  // 空闲页面链表头
    char reserved[PAGE_SIZE - 20]; // 保留空间
} FileHeader;

#define MAGIC_NUMBER 0x53514C42  // "BLSQ" (B+ Tree Storage)
#define MIN_FILE_SIZE (MAX_PAGES * PAGE_SIZE)  // 最小文件大小

// 初始化页面管理器
int page_manager_init(PageManager *pm, const char *db_file) {
    memset(pm, 0, sizeof(PageManager));
    
    // 构建索引文件和数据文件名
    char index_file[512];
    char data_file[512];
    snprintf(index_file, sizeof(index_file), "%s.idx", db_file);
    snprintf(data_file, sizeof(data_file), "%s.dat", db_file);
    
    // 打开或创建索引文件
    pm->fd_index = open(index_file, O_RDWR | O_CREAT, 0644);
    if (pm->fd_index < 0) {
        return -1;
    }
    
    // 打开或创建数据文件
    pm->fd_data = open(data_file, O_RDWR | O_CREAT, 0644);
    if (pm->fd_data < 0) {
        close(pm->fd_index);
        return -1;
    }
    
    // 检查索引文件大小
    struct stat st;
    if (fstat(pm->fd_index, &st) < 0) {
        close(pm->fd_index);
        close(pm->fd_data);
        return -1;
    }
    
    // 确保文件大小至少为最小大小
    if (st.st_size < MIN_FILE_SIZE) {
        if (ftruncate(pm->fd_index, MIN_FILE_SIZE) < 0) {
            close(pm->fd_index);
            close(pm->fd_data);
            return -1;
        }
        st.st_size = MIN_FILE_SIZE;
    }
    
    pm->index_size = st.st_size;
    
    // 检查数据文件大小
    if (fstat(pm->fd_data, &st) < 0) {
        close(pm->fd_index);
        close(pm->fd_data);
        return -1;
    }
    
    if (st.st_size < MIN_FILE_SIZE) {
        if (ftruncate(pm->fd_data, MIN_FILE_SIZE) < 0) {
            close(pm->fd_index);
            close(pm->fd_data);
            return -1;
        }
        st.st_size = MIN_FILE_SIZE;
    }
    
    pm->data_size = st.st_size;
    
    // 使用 mmap 映射索引文件
    pm->mmap_index = mmap(NULL, pm->index_size, PROT_READ | PROT_WRITE, MAP_SHARED, pm->fd_index, 0);
    if (pm->mmap_index == MAP_FAILED) {
        close(pm->fd_index);
        close(pm->fd_data);
        return -1;
    }
    
    // 使用 mmap 映射数据文件
    pm->mmap_data = mmap(NULL, pm->data_size, PROT_READ | PROT_WRITE, MAP_SHARED, pm->fd_data, 0);
    if (pm->mmap_data == MAP_FAILED) {
        munmap(pm->mmap_index, pm->index_size);
        close(pm->fd_index);
        close(pm->fd_data);
        return -1;
    }
    
    // 读取或初始化文件头
    FileHeader *header = (FileHeader*)pm->mmap_index;
    
    if (header->magic == 0 || header->magic != MAGIC_NUMBER) {
        // 新文件，初始化文件头
        memset(header, 0, sizeof(FileHeader));
        header->magic = MAGIC_NUMBER;
        header->version = 1;
        header->page_count = 1;  // 至少有一个头页面
        header->root_page = 0;
        header->free_page_list = 0;
        
        pm->page_count = 1;
        pm->free_page_list = 0;
        pm->need_sync = true;
        
        // 同步到磁盘
        msync(pm->mmap_index, PAGE_SIZE, MS_SYNC);
    } else {
        // 读取现有文件头
        if (header->magic != MAGIC_NUMBER) {
            munmap(pm->mmap_index, pm->index_size);
            munmap(pm->mmap_data, pm->data_size);
            close(pm->fd_index);
            close(pm->fd_data);
            return -1;  // 文件格式错误
        }
        
        pm->page_count = header->page_count;
        pm->free_page_list = header->free_page_list;
    }
    
    return 0;
}

// 关闭页面管理器
int page_manager_close(PageManager *pm) {
    if (pm->fd_index < 0) {
        return -1;
    }
    
    // 更新文件头
    FileHeader *header = (FileHeader*)pm->mmap_index;
    if (header) {
        header->page_count = pm->page_count;
        header->free_page_list = pm->free_page_list;
    }
    
    // 同步所有更改
    if (pm->need_sync) {
        msync(pm->mmap_index, pm->index_size, MS_SYNC);
        msync(pm->mmap_data, pm->data_size, MS_SYNC);
    }
    
    // 取消映射
    if (pm->mmap_index && pm->mmap_index != MAP_FAILED) {
        munmap(pm->mmap_index, pm->index_size);
    }
    if (pm->mmap_data && pm->mmap_data != MAP_FAILED) {
        munmap(pm->mmap_data, pm->data_size);
    }
    
    // 关闭文件
    if (pm->fd_index >= 0) {
        close(pm->fd_index);
        pm->fd_index = -1;
    }
    if (pm->fd_data >= 0) {
        close(pm->fd_data);
        pm->fd_data = -1;
    }
    
    return 0;
}

// 扩展文件大小（如果需要）
static int ensure_page_space(PageManager *pm, uint32_t page_id) {
    size_t needed_size = ((size_t)page_id + 1) * PAGE_SIZE;
    
    if (needed_size > pm->index_size) {
        // 扩展索引文件
        size_t new_size = needed_size;
        if (new_size < pm->index_size * 2) {
            new_size = pm->index_size * 2;
        }
        
        // 取消映射
        munmap(pm->mmap_index, pm->index_size);
        
        // 扩展文件
        if (ftruncate(pm->fd_index, new_size) < 0) {
            return -1;
        }
        
        // 重新映射
        pm->mmap_index = mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, pm->fd_index, 0);
        if (pm->mmap_index == MAP_FAILED) {
            return -1;
        }
        
        pm->index_size = new_size;
    }
    
    return 0;
}

// 分配新页面
uint32_t page_alloc(PageManager *pm) {
    uint32_t page_id;
    
    if (pm->free_page_list != 0) {
        // 从空闲链表分配
        page_id = pm->free_page_list;
        Page *page = page_get(pm, page_id);
        if (!page) return 0;
        // 读取下一个空闲页面
        memcpy(&pm->free_page_list, page->data, sizeof(uint32_t));
    } else {
        // 分配新页面
        page_id = pm->page_count++;
        if (ensure_page_space(pm, page_id) < 0) {
            return 0;  // 分配失败
        }
    }
    
    // 初始化页面
    Page *page = page_get(pm, page_id);
    if (page) {
        memset(page->data, 0, PAGE_SIZE);
        pm->need_sync = true;
    }
    
    return page_id;
}

// 释放页面
void page_free(PageManager *pm, uint32_t page_id) {
    Page *page = page_get(pm, page_id);
    if (!page) return;
    
    // 将页面加入空闲链表
    memcpy(page->data, &pm->free_page_list, sizeof(uint32_t));
    pm->free_page_list = page_id;
    pm->need_sync = true;
}

// 读取页面（从 mmap 直接访问）
Page* page_get(PageManager *pm, uint32_t page_id) {
    if (page_id >= MAX_PAGES) {
        return NULL;
    }
    
    // 确保有足够空间
    if (ensure_page_space(pm, page_id) < 0) {
        return NULL;
    }
    
    // 直接从 mmap 返回页面指针
    return (Page*)((char*)pm->mmap_index + (size_t)page_id * PAGE_SIZE);
}

// 标记页面为脏（使用 mmap 时，修改会自动反映，但需要同步）
void page_mark_dirty(PageManager *pm, uint32_t page_id) {
    if (page_id < MAX_PAGES) {
        pm->need_sync = true;
    }
}

// 刷新所有脏页到磁盘
int page_flush(PageManager *pm) {
    if (pm->need_sync) {
        msync(pm->mmap_index, pm->index_size, MS_SYNC);
        msync(pm->mmap_data, pm->data_size, MS_SYNC);
        pm->need_sync = false;
    }
    return 0;
}

// 刷新指定页面到磁盘
int page_flush_page(PageManager *pm, uint32_t page_id) {
    if (page_id >= MAX_PAGES) {
        return 0;
    }
    
    // 使用 mmap 时，同步整个映射区域
    if (pm->need_sync) {
        return page_flush(pm);
    }
    
    return 0;
}
