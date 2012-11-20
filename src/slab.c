/* 
 *Copyright (C) 2012 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>

#include "common.h"
#include "slab.h"

/*!
 * \brief Free all slabs from a slab cache.
 * \return Number of freed slabs.
 */
static inline int slab_cache_free_slabs(slab_t* slab)
{
    int count = 0;
    while (slab) {
        slab_t* next = slab->next;
        free(slab); /* no need to disconnect */
        ++count;
        slab = next;
        
    }
    return count;
}

/*
 * Slab helper functions.
 */

/*! \brief Return number of slabs in a linked list. */
static inline unsigned slab_list_walk(slab_t* slab)
{
    unsigned count = 0;
    while(slab) {
        slab = slab->next;
        ++count;
    }
    return count;
}

/*! \brief Remove slab from a linked list. */
static void slab_list_remove(slab_t* slab)
{
    // Disconnect from list
    if (slab->prev) {
        slab->prev->next = slab->next;
    }
    if(slab->next) {
        slab->next->prev = slab->prev;
    }
    
    // Disconnect from cache
    slab_cache_t* cache = slab->cache;
    {
        if (cache->slabs_free == slab) {
            cache->slabs_free = slab->next;
        } else if (cache->slabs_full == slab) {
            cache->slabs_full = slab->next;
        }
    }
}

/*! \brief Insert slab into a linked list. */
static void slab_list_insert(slab_t** list, slab_t* item)
{
    // If list exists, push to the top
    item->prev = 0;
    item->next = *list;
    if(*list) {
        (*list)->prev = item;
    }
    *list = item;
}

/*! \brief Move slab from one linked list to another. */
static inline void slab_list_move(slab_t** target, slab_t* slab)
{
    slab_list_remove(slab);
    slab_list_insert(target, slab);
}

/*
 * API functions.
 */

slab_t* slab_create(slab_cache_t* cache)
{
    const size_t size = SLAB_SIZE;
    
    slab_t* slab = NULL;
    if(posix_memalign((void*)&slab, size, size) == 0) {
        slab->bufsize = 0;
    }
    
    if (slab == NULL) {
        dbg_mem("%s: failed to allocate aligned memory block\n",
                __func__);
        return slab;
    }
    
    /* Initialize slab. */
    slab->cache = cache;
    slab_list_insert(&cache->slabs_free, slab);

    /* Already initialized? */
    if (slab->bufsize == cache->bufsize) {
        return slab;
    } else {
        slab->bufsize = cache->bufsize;
    }
    
    /* Ensure the item size can hold at least a size of ptr. */
    size_t item_size = slab->bufsize;
    if (item_size < SLAB_MIN_BUFLEN) {
        item_size = SLAB_MIN_BUFLEN;
    }
    
    /* Ensure at least some space for coloring */
    size_t data_size = size - sizeof(slab_t);
#ifdef MEM_COLORING
    size_t free_space = data_size % item_size;
    if (free_space < SLAB_MINCOLOR) {
        free_space = SLAB_MINCOLOR;
    }
    
    
    /// unsigned short color = __sync_fetch_and_add(&cache->color, 1);
    unsigned short color = (cache->color += sizeof(void*));
    color = color % free_space;
#else
    const unsigned short color = 0;
#endif
    
    /* Calculate useable data size */
    data_size -= color;
    slab->bufs_count = data_size / item_size;
    slab->bufs_free = slab->bufs_count;
    
    // Save first item as next free
    slab->base = (char*)slab + sizeof(slab_t) + color;
    slab->head = (void**)slab->base;
    
    // Create freelist, skip last member, which is set to NULL
    char* item = (char*)slab->head;
    for(unsigned i = 0; i < slab->bufs_count - 1; ++i) {
        *((void**)item) = item + item_size;
        item += item_size;
    }
    
    // Set last buf to NULL (tail)
    *((void**)item) = (void*)0;
    
    // Ensure the last item has a NULL next
    dbg_mem("%s: created slab (%p, %p) (%zu B)\n",
            __func__, slab, slab + size, size);
    return slab;
}

void slab_destroy(slab_t** slab)
{
    /* Disconnect from the list */
    slab_list_remove(*slab);
    
    /* Free slab */
    free(*slab);
    
    /* Invalidate pointer. */
    dbg_mem("%s: deleted slab %p\n", __func__, *slab);
    *slab = 0;
}

void* slab_alloc(slab_t* slab)
{
    // Fetch first free item
    void **item = 0;
    {
        if((item = slab->head)) {
            slab->head = (void**)*item;
            --slab->bufs_free;
        } else {
            // No more free items
            return 0;
        }
    }
    
    // Move to full?
    if (slab->bufs_free == 0) {
        slab_list_move(&slab->cache->slabs_full, slab);
    }
    
    return item;
}

void slab_free(void* ptr)
{
#ifdef SLAB_OFF
    free(ptr);
#else
    // Null pointer check
    if (!ptr) {
        return;
    }
    
    // Get slab start address
    slab_t* slab = slab_from_ptr(ptr);
    assert(slab);
    
    // Return buf to slab
    *((void**)ptr) = (void*)slab->head;
    slab->head = (void**)ptr;
    ++slab->bufs_free;
    
    
    // Return to partial
    if(slab->bufs_free == 1) {
        slab_list_move(&slab->cache->slabs_free, slab);
    }
#endif
}

int slab_cache_init(slab_cache_t* cache, unsigned short bufsize)
{
    if (!bufsize) {
        return -1;
    }
    
    cache->empty = 0;
    cache->bufsize = bufsize;
#ifndef SLAB_OFF
    cache->slabs_free = cache->slabs_full = 0;
    cache->color = 0;
    
    dbg_mem("%s: created cache of size %zu\n",
            __func__, bufsize);
#endif
    
    return 0;
}

void slab_cache_destroy(slab_cache_t* cache)
{
#ifndef SLAB_OFF
    // Free slabs
    slab_cache_free_slabs(cache->slabs_free);
    slab_cache_free_slabs(cache->slabs_full);
#endif
    
    // Invalidate cache
    cache->bufsize = 0;
    cache->slabs_free = cache->slabs_full = 0;
}

void* slab_cache_alloc(slab_cache_t* cache)
{
#ifdef SLAB_OFF
    return malloc(cache->bufsize);
#else
    slab_t* slab = cache->slabs_free;
    if(!cache->slabs_free) {
        slab = slab_create(cache);
        if (slab == NULL) {
            return NULL;
        }
    }
    
    
    return slab_alloc(slab);
#endif
}

int slab_cache_reap(slab_cache_t* cache)
{
    // For now, just free empty slabs
    slab_t* slab = cache->slabs_free;
    int count = 0;
    while (slab) {
        slab_t* next = slab->next;
        if (slab_isempty(slab)) {
            slab_destroy(&slab);
            ++count;
        }
        slab = next;
        
    }
    
    cache->empty = 0;
    return count;
}

