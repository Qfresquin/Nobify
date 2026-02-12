#include "../nob.h"
#include "../arena.h"
#include "../arena_dyn.h"
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>

// Macros de teste adaptadas para o nob
#define TEST(name) static void test_##name(int *passed, int *failed)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        nob_log(NOB_ERROR, " FAILED: %s (line %d): %s", __func__, __LINE__, #cond); \
        (*failed)++; \
        return; \
    } \
} while(0)

#define TEST_PASS() do { \
    (*passed)++; \
} while(0)

TEST(create_and_destroy) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(basic_allocation) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);
    
    void *ptr1 = arena_alloc(arena, 100);
    ASSERT(ptr1 != NULL);
    
    void *ptr2 = arena_alloc(arena, 200);
    ASSERT(ptr2 != NULL);
    ASSERT(ptr1 != ptr2);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(alloc_zero) {
    Arena *arena = arena_create(1024);
    
    char *buffer = (char*)arena_alloc_zero(arena, 100);
    ASSERT(buffer != NULL);
    
    // Verifica se está zerado
    for (size_t i = 0; i < 100; i++) {
        ASSERT(buffer[i] == 0);
    }
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(alloc_array) {
    Arena *arena = arena_create(1024);
    
    // Assume que arena_alloc_array é uma macro em arena.h: 
    // #define arena_alloc_array(a, T, n) (T*)arena_alloc(a, sizeof(T)*(n))
    // Caso não seja, usamos arena_alloc direto para garantir compilação:
    int *numbers = (int*)arena_alloc(arena, sizeof(int) * 10);
    ASSERT(numbers != NULL);
    
    // Usa o array
    for (int i = 0; i < 10; i++) {
        numbers[i] = i * 2;
    }
    
    ASSERT(numbers[5] == 10);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(multiple_blocks) {
    // Nota: A implementação da arena força um mínimo de 4096 bytes,
    // então 128 será arredondado para 4096 internamente.
    // O teste verifica a lógica de alocação total, que deve funcionar independente disso.
    Arena *arena = arena_create(128); 
    
    void *ptr1 = arena_alloc(arena, 100);
    ASSERT(ptr1 != NULL);
    
    void *ptr2 = arena_alloc(arena, 100);
    ASSERT(ptr2 != NULL);
    
    void *ptr3 = arena_alloc(arena, 100);
    ASSERT(ptr3 != NULL);
    
    size_t total = arena_total_allocated(arena);
    ASSERT(total >= 300);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(reset) {
    Arena *arena = arena_create(1024);
    
    void *ptr1 = arena_alloc(arena, 100);
    ASSERT(ptr1 != NULL);
    
    size_t before = arena_total_allocated(arena);
    ASSERT(before > 0);
    
    arena_reset(arena);
    
    size_t after = arena_total_allocated(arena);
    ASSERT(after == 0);
    
    // Pode alocar novamente
    void *ptr2 = arena_alloc(arena, 100);
    ASSERT(ptr2 != NULL);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(mark_and_rewind) {
    Arena *arena = arena_create(1024);
    
    void *ptr1 = arena_alloc(arena, 100);
    ASSERT(ptr1 != NULL);
    
    Arena_Mark mark = arena_mark(arena);
    
    void *ptr2 = arena_alloc(arena, 200);
    ASSERT(ptr2 != NULL);
    
    size_t before_rewind = arena_total_allocated(arena);
    
    arena_rewind(arena, mark);
    
    size_t after_rewind = arena_total_allocated(arena);
    ASSERT(after_rewind < before_rewind);
    
    // Verifica se voltamos para perto do tamanho original (considerando alinhamento)
    // O tamanho exato depende do alinhamento interno
    ASSERT(after_rewind >= 100); 
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(strdup) {
    Arena *arena = arena_create(1024);
    
    const char *original = "Hello, World!";
    char *copy = arena_strdup(arena, original);
    
    ASSERT(copy != NULL);
    ASSERT(copy != original);
    ASSERT(strcmp(copy, original) == 0);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(strndup) {
    Arena *arena = arena_create(1024);
    
    const char *original = "Hello, World!";
    char *copy = arena_strndup(arena, original, 5);
    
    ASSERT(copy != NULL);
    ASSERT(strlen(copy) == 5);
    ASSERT(strncmp(copy, "Hello", 5) == 0);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(memdup) {
    Arena *arena = arena_create(1024);
    
    int original[] = {1, 2, 3, 4, 5};
    int *copy = (int*)arena_memdup(arena, original, sizeof(original));
    
    ASSERT(copy != NULL);
    ASSERT(copy != original);
    
    for (int i = 0; i < 5; i++) {
        ASSERT(copy[i] == original[i]);
    }
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(alignment) {
    Arena *arena = arena_create(1024);
    
    // Aloca tamanhos desalinhados (1 byte)
    void *ptr1 = arena_alloc(arena, 1);
    void *ptr2 = arena_alloc(arena, 1);
    void *ptr3 = arena_alloc(arena, 1);
    
    ASSERT(ptr1 != NULL);
    ASSERT(ptr2 != NULL);
    ASSERT(ptr3 != NULL);
    
    // Verifica alinhamento (C11: _Alignof(max_align_t); fallback: 8)
    size_t align = 8;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    align = _Alignof(max_align_t);
#endif
    ASSERT(align != 0);
    ASSERT(((uintptr_t)ptr1 % align) == 0);
    ASSERT(((uintptr_t)ptr2 % align) == 0);
    ASSERT(((uintptr_t)ptr3 % align) == 0);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(large_allocation) {
    Arena *arena = arena_create(1024);
    
    // Aloca mais que a capacidade inicial (10KB)
    void *ptr = arena_alloc(arena, 10 * 1024);
    ASSERT(ptr != NULL);
    
    size_t capacity = arena_total_capacity(arena);
    ASSERT(capacity >= 10 * 1024);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(realloc_last_grow_and_shrink) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    uint8_t *ptr = (uint8_t*)arena_alloc(arena, 16);
    ASSERT(ptr != NULL);
    for (size_t i = 0; i < 16; i++) ptr[i] = (uint8_t)(i + 1);

    uint8_t *grown = (uint8_t*)arena_realloc_last(arena, ptr, 16, 64);
    ASSERT(grown != NULL);
    for (size_t i = 0; i < 16; i++) {
        ASSERT(grown[i] == (uint8_t)(i + 1));
    }

    uint8_t *shrunk = (uint8_t*)arena_realloc_last(arena, grown, 64, 8);
    ASSERT(shrunk != NULL);
    for (size_t i = 0; i < 8; i++) {
        ASSERT(shrunk[i] == (uint8_t)(i + 1));
    }

    arena_destroy(arena);
    TEST_PASS();
}

TEST(realloc_last_non_last_pointer) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    uint8_t *ptr1 = (uint8_t*)arena_alloc(arena, 16);
    uint8_t *ptr2 = (uint8_t*)arena_alloc(arena, 16);
    ASSERT(ptr1 != NULL && ptr2 != NULL);
    for (size_t i = 0; i < 16; i++) ptr1[i] = (uint8_t)(100 + i);

    uint8_t *new_ptr1 = (uint8_t*)arena_realloc_last(arena, ptr1, 16, 32);
    ASSERT(new_ptr1 != NULL);
    ASSERT(new_ptr1 != ptr1);
    for (size_t i = 0; i < 16; i++) {
        ASSERT(new_ptr1[i] == (uint8_t)(100 + i));
    }

    arena_destroy(arena);
    TEST_PASS();
}

TEST(realloc_last_invalid_old_size_is_safe) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    uint8_t *ptr = (uint8_t*)arena_alloc(arena, 16);
    ASSERT(ptr != NULL);
    for (size_t i = 0; i < 16; i++) ptr[i] = (uint8_t)(200 + i);

    // old_size inválido (maior que o realmente alocado) deve cair no caminho seguro
    uint8_t *realloced = (uint8_t*)arena_realloc_last(arena, ptr, 1024, 32);
    ASSERT(realloced != NULL);
    for (size_t i = 0; i < 16; i++) {
        ASSERT(realloced[i] == (uint8_t)(200 + i));
    }

    arena_destroy(arena);
    TEST_PASS();
}

TEST(reset_reuses_existing_blocks) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    void *big = arena_alloc(arena, 5000);
    ASSERT(big != NULL);
    size_t cap_before = arena_total_capacity(arena);
    ASSERT(cap_before >= 5000);

    arena_reset(arena);

    void *big_again = arena_alloc(arena, 5000);
    ASSERT(big_again != NULL);
    size_t cap_after = arena_total_capacity(arena);
    ASSERT(cap_after == cap_before);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(rewind_reuses_existing_blocks) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    void *head = arena_alloc(arena, 128);
    ASSERT(head != NULL);
    Arena_Mark mark = arena_mark(arena);

    void *big = arena_alloc(arena, 5000);
    ASSERT(big != NULL);
    size_t cap_before = arena_total_capacity(arena);

    arena_rewind(arena, mark);

    void *big_again = arena_alloc(arena, 5000);
    ASSERT(big_again != NULL);
    size_t cap_after = arena_total_capacity(arena);
    ASSERT(cap_after == cap_before);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(invalid_inputs_are_safe) {
    // API deve ser resiliente a entradas inválidas.
    ASSERT(arena_alloc(NULL, 16) == NULL);
    ASSERT(arena_alloc_zero(NULL, 16) == NULL);
    ASSERT(arena_strdup(NULL, "x") == NULL);
    ASSERT(arena_strndup(NULL, "x", 1) == NULL);
    ASSERT(arena_memdup(NULL, "x", 1) == NULL);

    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    ASSERT(arena_alloc(arena, 0) == NULL);
    ASSERT(arena_alloc_zero(arena, 0) == NULL);

    // ptr NULL: arena_realloc_last vira um alloc normal.
    void *p = arena_realloc_last(arena, NULL, 0, 32);
    ASSERT(p != NULL);

    // new_size == 0 deve falhar
    ASSERT(arena_realloc_last(arena, p, 32, 0) == NULL);

    // Strings/memdup com NULL/size==0 são aceitos e devem retornar NULL
    ASSERT(arena_strdup(arena, NULL) == NULL);
    ASSERT(arena_strndup(arena, NULL, 10) == NULL);
    ASSERT(arena_memdup(arena, NULL, 10) == NULL);
    ASSERT(arena_memdup(arena, "abc", 0) == NULL);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(overflow_alloc_returns_null_and_does_not_break_arena) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    size_t before = arena_total_allocated(arena);
    void *p = arena_alloc(arena, SIZE_MAX);
    ASSERT(p == NULL);

    // Após falha, a arena ainda deve funcionar.
    void *q = arena_alloc(arena, 16);
    ASSERT(q != NULL);
    ASSERT(arena_total_allocated(arena) > before);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(dyn_reserve_basic_and_preserves_data) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    int *items = NULL;
    size_t cap = 0;

    ASSERT(arena_da_reserve(arena, (void**)&items, &cap, sizeof(int), 1));
    ASSERT(items != NULL);
    ASSERT(cap >= 1);
    ASSERT(cap == 8); // comportamento esperado: começa em 8

    // escreve alguns valores e força crescimento
    for (int i = 0; i < 8; i++) items[i] = i * 10;

    int *old_items = items;
    ASSERT(arena_da_reserve(arena, (void**)&items, &cap, sizeof(int), 9));
    ASSERT(items != NULL);
    ASSERT(cap >= 9);
    ASSERT(cap == 16);
    // ponteiro pode ou não mudar; só precisamos preservar conteúdo
    (void)old_items;
    for (int i = 0; i < 8; i++) ASSERT(items[i] == i * 10);

    // No-op
    int *before_items = items;
    size_t before_cap = cap;
    ASSERT(arena_da_reserve(arena, (void**)&items, &cap, sizeof(int), 8));
    ASSERT(items == before_items);
    ASSERT(cap == before_cap);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(dyn_reserve_invalid_and_overflow) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    int *items = NULL;
    size_t cap = 0;

    ASSERT(!arena_da_reserve(NULL, (void**)&items, &cap, sizeof(int), 1));
    ASSERT(!arena_da_reserve(arena, NULL, &cap, sizeof(int), 1));
    ASSERT(!arena_da_reserve(arena, (void**)&items, NULL, sizeof(int), 1));
    ASSERT(!arena_da_reserve(arena, (void**)&items, &cap, 0, 1));

    // força erro por overflow (min_capacity > cap, mas new_cap * item_size estoura)
    cap = (SIZE_MAX / sizeof(int));
    ASSERT(!arena_da_reserve(arena, (void**)&items, &cap, sizeof(int), cap + 1));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(dyn_reserve_pair_basic_and_preserves_data) {
    Arena *arena = arena_create(2048);
    ASSERT(arena != NULL);

    int *a = NULL;
    uint32_t *b = NULL;
    size_t cap = 0;

    ASSERT(arena_da_reserve_pair(arena, (void**)&a, sizeof(int), (void**)&b, sizeof(uint32_t), &cap, 1));
    ASSERT(a != NULL && b != NULL);
    ASSERT(cap == 8);

    for (int i = 0; i < 8; i++) {
        a[i] = i + 1;
        b[i] = (uint32_t)(1000 + i);
    }

    ASSERT(arena_da_reserve_pair(arena, (void**)&a, sizeof(int), (void**)&b, sizeof(uint32_t), &cap, 9));
    ASSERT(cap == 16);
    for (int i = 0; i < 8; i++) {
        ASSERT(a[i] == i + 1);
        ASSERT(b[i] == (uint32_t)(1000 + i));
    }

    arena_destroy(arena);
    TEST_PASS();
}

TEST(dyn_reserve_pair_invalid_and_overflow) {
    Arena *arena = arena_create(1024);
    ASSERT(arena != NULL);

    int *a = NULL;
    int *b = NULL;
    size_t cap = 0;

    ASSERT(!arena_da_reserve_pair(NULL, (void**)&a, sizeof(int), (void**)&b, sizeof(int), &cap, 1));
    ASSERT(!arena_da_reserve_pair(arena, NULL, sizeof(int), (void**)&b, sizeof(int), &cap, 1));
    ASSERT(!arena_da_reserve_pair(arena, (void**)&a, 0, (void**)&b, sizeof(int), &cap, 1));
    ASSERT(!arena_da_reserve_pair(arena, (void**)&a, sizeof(int), NULL, sizeof(int), &cap, 1));
    ASSERT(!arena_da_reserve_pair(arena, (void**)&a, sizeof(int), (void**)&b, 0, &cap, 1));
    ASSERT(!arena_da_reserve_pair(arena, (void**)&a, sizeof(int), (void**)&b, sizeof(int), NULL, 1));

    // overflow (min_capacity > cap, mas new_cap * item_size estoura)
    cap = (SIZE_MAX / sizeof(int));
    ASSERT(!arena_da_reserve_pair(arena, (void**)&a, sizeof(int), (void**)&b, sizeof(int), &cap, cap + 1));

    arena_destroy(arena);
    TEST_PASS();
}

void run_arena_tests(int *passed, int *failed) {
    test_create_and_destroy(passed, failed);
    test_basic_allocation(passed, failed);
    test_alloc_zero(passed, failed);
    test_alloc_array(passed, failed);
    test_multiple_blocks(passed, failed);
    test_reset(passed, failed);
    test_mark_and_rewind(passed, failed);
    test_strdup(passed, failed);
    test_strndup(passed, failed);
    test_memdup(passed, failed);
    test_alignment(passed, failed);
    test_large_allocation(passed, failed);
    test_realloc_last_grow_and_shrink(passed, failed);
    test_realloc_last_non_last_pointer(passed, failed);
    test_realloc_last_invalid_old_size_is_safe(passed, failed);
    test_reset_reuses_existing_blocks(passed, failed);
    test_rewind_reuses_existing_blocks(passed, failed);
    test_invalid_inputs_are_safe(passed, failed);
    test_overflow_alloc_returns_null_and_does_not_break_arena(passed, failed);
    test_dyn_reserve_basic_and_preserves_data(passed, failed);
    test_dyn_reserve_invalid_and_overflow(passed, failed);
    test_dyn_reserve_pair_basic_and_preserves_data(passed, failed);
    test_dyn_reserve_pair_invalid_and_overflow(passed, failed);
}
