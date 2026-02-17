#include "arena.h"
#include <stdlib.h>
#include <string.h>

// Default to 4KB blocks if not specified
#ifndef ARENA_BLOCK_SIZE
#define ARENA_BLOCK_SIZE (4 * 1024 * 16) // 64 KB
#endif

#define ARENA_ALIGNMENT sizeof(void*)

struct ArenaBlock {
    ArenaBlock *next;
    size_t capacity;
    size_t used;
    // C99 Flexible array member for the actual memory
    char data[]; 
};

void arena_init(Arena *a) {
    if (a) {
        a->head = NULL;
        a->current = NULL;
        a->default_block_size = ARENA_BLOCK_SIZE;
    }
}

static ArenaBlock* arena_create_block(size_t size) {
    ArenaBlock *block = (ArenaBlock*)malloc(sizeof(ArenaBlock) + size);
    if (block) {
        block->next = NULL;
        block->capacity = size;
        block->used = 0;
    }
    return block;
}

void* arena_alloc(Arena *a, size_t size) {
    if (!a || size == 0) return NULL;

    size_t aligned_size = (size + ARENA_ALIGNMENT - 1) & ~(ARENA_ALIGNMENT - 1);

    if (a->current) {
        if (a->current->used + aligned_size <= a->current->capacity) {
            void *ptr = a->current->data + a->current->used;
            a->current->used += aligned_size;
            return ptr;
        }

        if (a->current->next) {
            ArenaBlock *next = a->current->next;
            if (next->capacity >= aligned_size) {
                a->current = next;
                if (a->current->used + aligned_size <= a->current->capacity) {
                    void *ptr = a->current->data + a->current->used;
                    a->current->used += aligned_size;
                    return ptr;
                }
            }
        }
    }

    // 3. Allocate a new block
    size_t block_size = (aligned_size > a->default_block_size) ? aligned_size : a->default_block_size;
    ArenaBlock *new_block = arena_create_block(block_size);
    if (!new_block) return NULL; 

    if (!a->head) {
        // First block
        a->head = new_block;
        a->current = new_block;
    } else {
        // Link new block
        // If we were in the middle of a chain (due to reset), we insert/splice here
        // to preserve the list structure or just append. 
        // Simple append strategy:
        new_block->next = a->current->next;
        a->current->next = new_block;
        a->current = new_block;
    }

    void *ptr = new_block->data + new_block->used;
    new_block->used += aligned_size;
    return ptr;
}

void arena_reset(Arena *a) {
    if (!a) return;
    ArenaBlock *block = a->head;
    while (block) {
        block->used = 0;
        block = block->next;
    }
    a->current = a->head;
}

void arena_free(Arena *a) {
    if (!a) return;
    ArenaBlock *block = a->head;
    while (block) {
        ArenaBlock *next = block->next;
        free(block);
        block = next;
    }
    a->head = NULL;
    a->current = NULL;
}
