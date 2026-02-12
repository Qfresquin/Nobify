#include "arena.h"
#include <stdlib.h>
#include <assert.h>
#include <limits.h>
#include <stddef.h> // max_align_t

// Bloco de memória da arena
typedef struct Arena_Block Arena_Block;
struct Arena_Block {
    Arena_Block* next;
    size_t capacity;
    size_t used;
    // Garante que (block + 1) esteja alinhado para max_align_t quando C11+
    // (o sizeof(struct) é múltiplo do alinhamento da struct).
    max_align_t _align;
    // Os dados seguem imediatamente após esta estrutura
    // (alinhamento garantido por malloc)
};

struct Arena {
    Arena_Block* first;
    Arena_Block* current;
    size_t min_block_size;
};

// Alinhamento para todos os tipos
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ARENA_ALIGNMENT _Alignof(max_align_t)
#else
#define ARENA_ALIGNMENT 8
#endif
#define ALIGN_UP(n) (((n) + (ARENA_ALIGNMENT - 1)) & ~(ARENA_ALIGNMENT - 1))

static bool arena_add_overflow(size_t a, size_t b, size_t *out) {
    if (a > SIZE_MAX - b) return true;
    *out = a + b;
    return false;
}

static bool arena_align_up(size_t n, size_t *out) {
    size_t tmp = 0;
    if (arena_add_overflow(n, ARENA_ALIGNMENT - 1, &tmp)) return false;
    *out = tmp & ~(ARENA_ALIGNMENT - 1);
    return true;
}

static Arena_Block* arena_block_create(size_t capacity) {
    // Aloca memória para a estrutura + capacidade pedida
    size_t total_size = 0;
    if (arena_add_overflow(sizeof(Arena_Block), capacity, &total_size)) {
        return NULL;
    }
    Arena_Block* block = (Arena_Block*)malloc(total_size);
    if (!block) return NULL;
    
    block->next = NULL;
    block->capacity = capacity;
    block->used = 0;
    
    return block;
}

Arena* arena_create(size_t initial_capacity) {
    Arena* arena = (Arena*)malloc(sizeof(Arena));
    if (!arena) return NULL;
    
    // Tamanho mínimo para um bloco: 4KB ou o pedido, o que for maior
    arena->min_block_size = initial_capacity < 4096 ? 4096 : initial_capacity;
    
    arena->first = arena_block_create(arena->min_block_size);
    if (!arena->first) {
        free(arena);
        return NULL;
    }
    
    arena->current = arena->first;
    return arena;
}

void arena_destroy(Arena* arena) {
    if (!arena) return;
    
    Arena_Block* block = arena->first;
    while (block) {
        Arena_Block* next = block->next;
        free(block);
        block = next;
    }
    
    free(arena);
}

static void* arena_alloc_from_block(Arena_Block* block, size_t size) {
    if (!arena_align_up(size, &size)) return NULL;
    
    size_t new_used = 0;
    if (arena_add_overflow(block->used, size, &new_used) || new_used > block->capacity) {
        return NULL;
    }
    
    void* ptr = (uint8_t*)(block + 1) + block->used;
    block->used = new_used;
    return ptr;
}

static Arena_Block* arena_find_reusable_block(Arena_Block *start, size_t size) {
    Arena_Block *block = start;
    while (block) {
        size_t next_used = 0;
        bool no_overflow = !arena_add_overflow(block->used, size, &next_used);
        if (no_overflow && block->capacity >= size && next_used <= block->capacity) {
            return block;
        }
        block = block->next;
    }
    return NULL;
}

static Arena_Block* arena_find_last_block(Arena_Block *first) {
    Arena_Block *block = first;
    while (block && block->next) {
        block = block->next;
    }
    return block;
}

void* arena_alloc(Arena* arena, size_t size) {
    if (!arena || size == 0) return NULL;

    size_t aligned_size = 0;
    if (!arena_align_up(size, &aligned_size)) return NULL;
    
    // Tenta alocar no bloco atual
    void* ptr = arena_alloc_from_block(arena->current, aligned_size);
    if (ptr) return ptr;

    // Tenta reutilizar blocos já existentes (importante após reset/rewind)
    Arena_Block *reusable = arena_find_reusable_block(arena->current->next, aligned_size);
    if (reusable) {
        arena->current = reusable;
        ptr = arena_alloc_from_block(arena->current, aligned_size);
        if (ptr) return ptr;
    }
    
    // Se não couber, cria um novo bloco
    // O novo bloco será pelo menos do tamanho pedido ou o dobro do anterior
    size_t new_capacity = arena->current->capacity;
    if (new_capacity <= SIZE_MAX / 2) {
        new_capacity *= 2;
    } else {
        new_capacity = SIZE_MAX;
    }
    if (new_capacity < aligned_size) {
        new_capacity = aligned_size;
    }
    if (new_capacity < arena->min_block_size) {
        new_capacity = arena->min_block_size;
    }
    
    Arena_Block* new_block = arena_block_create(new_capacity);
    if (!new_block) return NULL;
    
    // Liga o novo bloco ao final da lista para não perder blocos já encadeados
    Arena_Block *last = arena_find_last_block(arena->first);
    if (!last) {
        free(new_block);
        return NULL;
    }
    last->next = new_block;
    arena->current = new_block;
    
    // Agora deve caber
    ptr = arena_alloc_from_block(arena->current, aligned_size);
    assert(ptr && "Failed to allocate in new block");
    return ptr;
}

void* arena_alloc_zero(Arena* arena, size_t size) {
    void* ptr = arena_alloc(arena, size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void* arena_realloc_last(Arena* arena, void* ptr, size_t old_size, size_t new_size) {
    if (!arena) return NULL;
    if (!ptr) return arena_alloc(arena, new_size);
    if (new_size == 0) return NULL;
    
    size_t old_size_aligned = 0;
    size_t new_size_aligned = 0;
    if (!arena_align_up(old_size, &old_size_aligned)) return NULL;
    if (!arena_align_up(new_size, &new_size_aligned)) return NULL;
    
    // Verifica se ptr é de fato a última alocação no bloco atual
    uint8_t* expected_ptr = NULL;
    bool valid_old_size = old_size_aligned <= arena->current->used;
    if (valid_old_size) {
        expected_ptr = (uint8_t*)(arena->current + 1) +
                       (arena->current->used - old_size_aligned);
    }
    
    if (valid_old_size && ptr == expected_ptr) {
        // É a última alocação, podemos tentar redimensionar in-place
        size_t available = arena->current->capacity - 
                          (arena->current->used - old_size_aligned);
        
        if (new_size_aligned <= available) {
            // Cabe no espaço restante, apenas ajusta o used
            arena->current->used = arena->current->used - old_size_aligned + new_size_aligned;
            return ptr;
        }
    }
    
    // Não é a última ou não cabe: aloca novo e copia
    void* new_ptr = arena_alloc(arena, new_size_aligned);
    if (new_ptr && old_size > 0) {
        size_t copy_size = old_size < new_size ? old_size : new_size;
        memcpy(new_ptr, ptr, copy_size);
    }
    return new_ptr;
}

void arena_reset(Arena* arena) {
    if (!arena) return;
    
    Arena_Block* block = arena->first;
    while (block) {
        block->used = 0;
        block = block->next;
    }
    arena->current = arena->first;
}

size_t arena_total_allocated(const Arena* arena) {
    if (!arena) return 0;
    
    size_t total = 0;
    Arena_Block* block = arena->first;
    while (block) {
        total += block->used;
        block = block->next;
    }
    return total;
}

size_t arena_total_capacity(const Arena* arena) {
    if (!arena) return 0;
    
    size_t total = 0;
    Arena_Block* block = arena->first;
    while (block) {
        total += block->capacity;
        block = block->next;
    }
    return total;
}

Arena_Mark arena_mark(Arena* arena) {
    Arena_Mark mark = {0};
    if (arena && arena->current) {
        // Calcula offset global
        size_t offset = 0;
        Arena_Block* block = arena->first;
        while (block && block != arena->current) {
            offset += block->capacity;
            block = block->next;
        }
        if (block) { // block == arena->current
            offset += arena->current->used;
        }
        mark.offset = offset;
    }
    return mark;
}

void arena_rewind(Arena* arena, Arena_Mark mark) {
    if (!arena) return;
    
    // Encontra o bloco correto e a posição dentro dele
    size_t remaining = mark.offset;
    Arena_Block* block = arena->first;
    
    while (block && remaining >= block->capacity) {
        remaining -= block->capacity;
        block->used = block->capacity; // Bloco completamente usado
        block = block->next;
    }
    
    if (block) {
        block->used = remaining;
        arena->current = block;
        
        // Zera os blocos seguintes
        Arena_Block* next = block->next;
        while (next) {
            next->used = 0;
            next = next->next;
        }
    } else {
        // Mark além do alocado, reseta tudo
        arena_reset(arena);
    }
}

char* arena_strdup(Arena* arena, const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* copy = (char*)arena_alloc(arena, len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

char* arena_strndup(Arena* arena, const char* str, size_t max_len) {
    if (!str) return NULL;
    
    size_t len = 0;
    while (len < max_len && str[len]) len++;
    
    char* copy = (char*)arena_alloc(arena, len + 1);
    if (copy) {
        memcpy(copy, str, len);
        copy[len] = '\0';
    }
    return copy;
}

void* arena_memdup(Arena* arena, const void* src, size_t size) {
    if (!src || size == 0) return NULL;
    void* copy = arena_alloc(arena, size);
    if (copy) {
        memcpy(copy, src, size);
    }
    return copy;
}
