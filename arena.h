#ifndef ARENA_H_
#define ARENA_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef struct Arena Arena;

// Cria uma arena com capacidade inicial (em bytes)
Arena* arena_create(size_t initial_capacity);

// Destroi a arena, liberando toda a memória
void arena_destroy(Arena* arena);

// Aloca memória na arena (alinhada para 8 bytes)
void* arena_alloc(Arena* arena, size_t size);

// Aloca memória e zera os bytes
void* arena_alloc_zero(Arena* arena, size_t size);

// Aloca memória para um array de itens
#define arena_alloc_array(arena, type, count) \
    ((type*)arena_alloc((arena), sizeof(type) * (count)))

// Aloca memória para um array e zera
#define arena_alloc_array_zero(arena, type, count) \
    ((type*)arena_alloc_zero((arena), sizeof(type) * (count)))

// Realoca o último bloco alocado (útil para dynamic arrays)
// Só funciona se ptr for a última alocação!
void* arena_realloc_last(Arena* arena, void* ptr, size_t old_size, size_t new_size);

// Reinicia a arena (libera todas as alocações, mantendo a memória)
void arena_reset(Arena* arena);

// Retorna o tamanho total alocado na arena
size_t arena_total_allocated(const Arena* arena);

// Retorna o tamanho total da capacidade da arena
size_t arena_total_capacity(const Arena* arena);

// Salva a posição atual da arena para restaurar depois
typedef struct {
    size_t offset;
} Arena_Mark;

// Marca a posição atual
Arena_Mark arena_mark(Arena* arena);

// Restaura para uma marca anterior (libera tudo após a marca)
void arena_rewind(Arena* arena, Arena_Mark mark);

// Duplica uma string na arena
char* arena_strdup(Arena* arena, const char* str);

// Duplica uma string com tamanho máximo
char* arena_strndup(Arena* arena, const char* str, size_t max_len);

// Duplica uma memória na arena
void* arena_memdup(Arena* arena, const void* src, size_t size);

#endif // ARENA_H_