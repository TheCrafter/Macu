#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "leak_detect.h"
#include <string.h>
#define __USE_MINGW_ANSI_STDIO 1
#include <stdio.h>
#include <assert.h>
#include <limits.h>
#include "tinycthread.h"

#ifdef _MSC_VER
#define PATH_LIM _MAX_PATH
#else
#define PATH_LIM PATH_MAX
#endif

#undef malloc
#undef calloc
#undef realloc
#undef free

#include "hashmap.h"

#define PRINT_LEAK_STR "\
-----------------------------\n\
-        Memory Leak        -\n\
-----------------------------\n\
File: %s\n\
Line: %u\n\
Size: %zu bytes\n\
Address: %#zx\n\
-----------------------------\n"

/* Allocation entry */
struct ld_alloc_info {
    size_t size;
    char filename[PATH_LIM];
    unsigned int line;
};

/* Allocation database */
struct ld_alloc_db {
    struct hashmap allocations;
};

/* Allocation database instance */
static struct ld_alloc_db alloc_db;
static mtx_t lock;

static size_t hash_fn(hm_ptr key)
{
    long h = (long) key;
    return (13 * h) ^ (h >> 15);
}

static int hm_eql(hm_ptr k1, hm_ptr k2)
{
    return k1 == k2;
}

void ld_init()
{
    mtx_init(&lock, mtx_plain);
    hashmap_init(&alloc_db.allocations, hash_fn, hm_eql);
}

static void allocations_free_iter(hm_ptr key, hm_ptr value)
{
    struct ld_alloc_info* ai = (struct ld_alloc_info*)hm_pcast(value);
    free(hm_pcast(key));
    free(ai);
}

void ld_shutdown()
{
    hashmap_iter(&alloc_db.allocations, allocations_free_iter);
    hashmap_destroy(&alloc_db.allocations);
    mtx_destroy(&lock);
}

static void add_leak(struct ld_alloc_db* db, void* ptr, struct ld_alloc_info* info)
{
    mtx_lock(&lock);
    hashmap_put(&db->allocations, hm_cast(ptr), hm_cast(info));
    mtx_unlock(&lock);
}

static struct ld_alloc_info* get_leak(struct ld_alloc_db* db, void* ptr)
{
    struct ld_alloc_info** ai = (struct ld_alloc_info**)hashmap_get(&db->allocations, hm_cast(ptr));
    return ai == 0 ? 0 : *ai;
}

static void remove_leak(struct ld_alloc_db* db, void* ptr)
{
    mtx_lock(&lock);
    struct ld_alloc_info* alloc_info = (struct ld_alloc_info*)get_leak(db, ptr);
    hashmap_remove(&db->allocations, hm_cast(ptr));
    free(alloc_info);
    mtx_unlock(&lock);
}

void* ld_malloc(size_t size, const char* file, unsigned int line)
{
    struct ld_alloc_info* alloc_info = malloc(sizeof(struct ld_alloc_info));
    void* memptr = malloc(size);
    alloc_info->size = size;
    strcpy(alloc_info->filename, file);
    alloc_info->line = line;
    add_leak(&alloc_db, memptr, alloc_info);
    return memptr;
}

void* ld_calloc(size_t num, size_t size, const char* file, unsigned int line)
{
    struct ld_alloc_info* alloc_info = malloc(sizeof(struct ld_alloc_info));
    void* memptr = calloc(num, size);
    alloc_info->size = num * size;
    strcpy(alloc_info->filename, file);
    alloc_info->line = line;
    add_leak(&alloc_db, memptr, alloc_info);
    return memptr;
}

void* ld_realloc(void* ptr, size_t size, const char* file, unsigned int line)
{
    struct ld_alloc_info* ai = get_leak(&alloc_db, ptr);
    /* Allocation exists */
    if (ai) {
        /* Behave like free */
        if (size == 0) {
            ld_free(ptr);
            return 0;
        } else {
            void* newmptr = realloc(ptr, size);
            ai->size = size;
            ai->line = line;
            strcpy(ai->filename, file);
            return newmptr;
        }
    } else if (ptr == 0) {
        /* Behave like malloc */
        return ld_malloc(size, file, line);
    }
    /* Probably some error in case we get here */
    assert(0 && "realloc with missing allocation info!");
    return 0;
}

void ld_free(void* addr)
{
    remove_leak(&alloc_db, addr);
    free(addr);
}

static void allocations_print_iter(hm_ptr key, hm_ptr value)
{
    struct ld_alloc_info* ai = (struct ld_alloc_info*)hm_pcast(value);
    printf(PRINT_LEAK_STR,
           ai->filename,
           ai->line,
           ai->size,
           (size_t)key);
}

void ld_print_leaks()
{
    hashmap_iter(&alloc_db.allocations, allocations_print_iter);
}
