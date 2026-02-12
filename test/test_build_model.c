#include "../nob.h"
#include "../build_model.h"
#include <stdio.h>
#include <stdbool.h>

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

// --- Testes ---

TEST(create_model) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    ASSERT(model != NULL);
    ASSERT(model->arena == arena);
    ASSERT(model->target_count == 0);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_target) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *target = build_model_add_target(model, 
                                                  sv_from_cstr("app"),
                                                  TARGET_EXECUTABLE);
    
    ASSERT(target != NULL);
    ASSERT(model->target_count == 1);
    ASSERT(nob_sv_eq(target->name, sv_from_cstr("app")));
    ASSERT(target->type == TARGET_EXECUTABLE);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(find_target) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    
    Build_Target *found = build_model_find_target(model, sv_from_cstr("app"));
    ASSERT(found != NULL);
    ASSERT(nob_sv_eq(found->name, sv_from_cstr("app")));
    
    Build_Target *not_found = build_model_find_target(model, sv_from_cstr("missing"));
    ASSERT(not_found == NULL);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_source) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *target = build_model_add_target(model, 
                                                  sv_from_cstr("app"),
                                                  TARGET_EXECUTABLE);
    
    build_target_add_source(target, arena, sv_from_cstr("main.c"));
    build_target_add_source(target, arena, sv_from_cstr("util.c"));
    
    ASSERT(target->sources.count == 2);
    ASSERT(nob_sv_eq(target->sources.items[0], sv_from_cstr("main.c")));
    ASSERT(nob_sv_eq(target->sources.items[1], sv_from_cstr("util.c")));
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_source_deduplicates) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    Build_Target *target = build_model_add_target(model,
                                                  sv_from_cstr("app"),
                                                  TARGET_EXECUTABLE);

    build_target_add_source(target, arena, sv_from_cstr("main.c"));
    build_target_add_source(target, arena, sv_from_cstr("main.c"));
    build_target_add_source(target, arena, sv_from_cstr("util.c"));
    build_target_add_source(target, arena, sv_from_cstr("util.c"));

    ASSERT(target->sources.count == 2);
    ASSERT(nob_sv_eq(target->sources.items[0], sv_from_cstr("main.c")));
    ASSERT(nob_sv_eq(target->sources.items[1], sv_from_cstr("util.c")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_dependency) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *app = build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    // Adiciona o target mylib para validar a referência
    build_model_add_target(model, sv_from_cstr("mylib"), TARGET_STATIC_LIB);
    
    build_target_add_dependency(app, arena, sv_from_cstr("mylib"));
    
    ASSERT(app->dependencies.count == 1);
    ASSERT(nob_sv_eq(app->dependencies.items[0], sv_from_cstr("mylib")));
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_library) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *target = build_model_add_target(model, 
                                                  sv_from_cstr("app"),
                                                  TARGET_EXECUTABLE);
    
    build_target_add_library(target, arena, sv_from_cstr("pthread"), VISIBILITY_PUBLIC);
    build_target_add_library(target, arena, sv_from_cstr("m"), VISIBILITY_PUBLIC);
    
    ASSERT(target->link_libraries.count == 2);
    ASSERT(nob_sv_eq(target->link_libraries.items[0], sv_from_cstr("pthread")));
    ASSERT(nob_sv_eq(target->link_libraries.items[1], sv_from_cstr("m")));
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_definition) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *target = build_model_add_target(model, 
                                                  sv_from_cstr("app"),
                                                  TARGET_EXECUTABLE);
    
    build_target_add_definition(target, arena, 
                                sv_from_cstr("DEBUG"), 
                                VISIBILITY_PRIVATE,
                                CONFIG_ALL);
    
    ASSERT(target->properties[CONFIG_ALL].compile_definitions.count == 1);
    ASSERT(nob_sv_eq(target->properties[CONFIG_ALL].compile_definitions.items[0], 
                     sv_from_cstr("DEBUG")));
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_include_directory) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *target = build_model_add_target(model, 
                                                  sv_from_cstr("app"),
                                                  TARGET_EXECUTABLE);
    
    build_target_add_include_directory(target, arena, 
                                       sv_from_cstr("include"),
                                       VISIBILITY_PUBLIC,
                                       CONFIG_ALL);
    
    ASSERT(target->properties[CONFIG_ALL].include_directories.count == 1);
    ASSERT(nob_sv_eq(target->properties[CONFIG_ALL].include_directories.items[0], 
                     sv_from_cstr("include")));
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(set_property) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *target = build_model_add_target(model, 
                                                  sv_from_cstr("app"),
                                                  TARGET_EXECUTABLE);
    
    build_target_set_property(target, arena, 
                             sv_from_cstr("OUTPUT_NAME"),
                             sv_from_cstr("myapp"));
    
    String_View prop = build_target_get_property(target, sv_from_cstr("OUTPUT_NAME"));
    ASSERT(nob_sv_eq(prop, sv_from_cstr("myapp")));
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(cache_variable) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    build_model_set_cache_variable(model, 
                                   sv_from_cstr("CMAKE_BUILD_TYPE"),
                                   sv_from_cstr("Debug"),
                                   sv_from_cstr("STRING"),
                                   sv_from_cstr("Build type"));
    
    String_View value = build_model_get_cache_variable(model, 
                                                       sv_from_cstr("CMAKE_BUILD_TYPE"));
    ASSERT(nob_sv_eq(value, sv_from_cstr("Debug")));
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(multiple_targets) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    build_model_add_target(model, sv_from_cstr("lib1"), TARGET_STATIC_LIB);
    build_model_add_target(model, sv_from_cstr("lib2"), TARGET_SHARED_LIB);
    
    ASSERT(model->target_count == 3);
    ASSERT(nob_sv_eq(model->targets[0].name, sv_from_cstr("app")));
    ASSERT(nob_sv_eq(model->targets[1].name, sv_from_cstr("lib1")));
    ASSERT(nob_sv_eq(model->targets[2].name, sv_from_cstr("lib2")));
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(validate_dependencies) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *app = build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    build_model_add_target(model, sv_from_cstr("mylib"), TARGET_STATIC_LIB);
    
    build_target_add_dependency(app, arena, sv_from_cstr("mylib"));
    
    bool valid = build_model_validate_dependencies(model);
    ASSERT(valid == true);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(invalid_dependency) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *app = build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    build_target_add_dependency(app, arena, sv_from_cstr("missing_lib"));
    
    bool valid = build_model_validate_dependencies(model);
    ASSERT(valid == false);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(topological_sort) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *app = build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    build_model_add_target(model, sv_from_cstr("lib1"), TARGET_STATIC_LIB);
    build_model_add_target(model, sv_from_cstr("lib2"), TARGET_STATIC_LIB);
    
    // app depende de lib1 e lib2
    build_target_add_dependency(app, arena, sv_from_cstr("lib1"));
    build_target_add_dependency(app, arena, sv_from_cstr("lib2"));
    
    size_t count = 0;
    Build_Target **sorted = build_model_topological_sort(model, &count);
    
    ASSERT(sorted != NULL);
    ASSERT(count == 3);
    
    // O último elemento deve ser app, pois depende dos outros dois
    ASSERT(nob_sv_eq(sorted[2]->name, sv_from_cstr("app")));
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(topological_sort_linear_chain) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    Build_Target *app = build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    Build_Target *libb = build_model_add_target(model, sv_from_cstr("libb"), TARGET_STATIC_LIB);
    Build_Target *liba = build_model_add_target(model, sv_from_cstr("liba"), TARGET_STATIC_LIB);

    ASSERT(app != NULL);
    ASSERT(libb != NULL);
    ASSERT(liba != NULL);

    // app -> libb -> liba
    build_target_add_dependency(app, arena, sv_from_cstr("libb"));
    build_target_add_dependency(libb, arena, sv_from_cstr("liba"));

    size_t count = 0;
    Build_Target **sorted = build_model_topological_sort(model, &count);

    ASSERT(sorted != NULL);
    ASSERT(count == 3);
    ASSERT(nob_sv_eq(sorted[0]->name, sv_from_cstr("liba")));
    ASSERT(nob_sv_eq(sorted[1]->name, sv_from_cstr("libb")));
    ASSERT(nob_sv_eq(sorted[2]->name, sv_from_cstr("app")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(cyclic_dependencies_invalid) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    Build_Target *a = build_model_add_target(model, sv_from_cstr("a"), TARGET_STATIC_LIB);
    Build_Target *b = build_model_add_target(model, sv_from_cstr("b"), TARGET_STATIC_LIB);
    ASSERT(a != NULL);
    ASSERT(b != NULL);

    build_target_add_dependency(a, arena, sv_from_cstr("b"));
    build_target_add_dependency(b, arena, sv_from_cstr("a"));

    ASSERT(build_model_validate_dependencies(model) == false);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(topological_sort_cycle_returns_null) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    Build_Target *a = build_model_add_target(model, sv_from_cstr("a"), TARGET_STATIC_LIB);
    Build_Target *b = build_model_add_target(model, sv_from_cstr("b"), TARGET_STATIC_LIB);
    ASSERT(a != NULL);
    ASSERT(b != NULL);

    build_target_add_dependency(a, arena, sv_from_cstr("b"));
    build_target_add_dependency(b, arena, sv_from_cstr("a"));

    size_t count = 123;
    Build_Target **sorted = build_model_topological_sort(model, &count);
    ASSERT(sorted == NULL);
    ASSERT(count == 0);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(duplicate_target_type_conflict) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    Build_Target *first = build_model_add_target(model, sv_from_cstr("same"), TARGET_STATIC_LIB);
    Build_Target *second = build_model_add_target(model, sv_from_cstr("same"), TARGET_SHARED_LIB);

    ASSERT(first != NULL);
    ASSERT(second == NULL);
    ASSERT(model->target_count == 1);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(topological_sort_empty_model) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    size_t count = 123;
    Build_Target **sorted = build_model_topological_sort(model, &count);
    ASSERT(sorted == NULL);
    ASSERT(count == 0);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(topological_sort_null_args) {
    // count NULL deve ser tratado com segurança
    ASSERT(build_model_topological_sort(NULL, NULL) == NULL);

    size_t count = 999;
    ASSERT(build_model_topological_sort(NULL, &count) == NULL);
    ASSERT(count == 0);

    TEST_PASS();
}

TEST(project_info) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    model->project_name = sv_from_cstr("MyProject");
    model->project_version = sv_from_cstr("1.0.0");
    model->project_description = sv_from_cstr("A test project");
    
    string_list_add(&model->project_languages, arena, sv_from_cstr("C"));
    string_list_add(&model->project_languages, arena, sv_from_cstr("CXX"));
    
    ASSERT(nob_sv_eq(model->project_name, sv_from_cstr("MyProject")));
    ASSERT(nob_sv_eq(model->project_version, sv_from_cstr("1.0.0")));
    ASSERT(model->project_languages.count == 2);
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(target_prefixes_suffixes) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    
    Build_Target *exe = build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    Build_Target *static_lib = build_model_add_target(model, sv_from_cstr("mylib"), TARGET_STATIC_LIB);
    Build_Target *shared_lib = build_model_add_target(model, sv_from_cstr("shared"), TARGET_SHARED_LIB);
    
    ASSERT(nob_sv_eq(exe->prefix, sv_from_cstr("")));
#if defined(_WIN32)
    ASSERT(nob_sv_eq(exe->suffix, sv_from_cstr(".exe")));
    ASSERT(nob_sv_eq(static_lib->prefix, sv_from_cstr("")));
    ASSERT(nob_sv_eq(static_lib->suffix, sv_from_cstr(".lib")));
    ASSERT(nob_sv_eq(shared_lib->prefix, sv_from_cstr("")));
    ASSERT(nob_sv_eq(shared_lib->suffix, sv_from_cstr(".dll")));
#elif defined(__APPLE__)
    ASSERT(nob_sv_eq(exe->suffix, sv_from_cstr("")));
    ASSERT(nob_sv_eq(static_lib->prefix, sv_from_cstr("lib")));
    ASSERT(nob_sv_eq(static_lib->suffix, sv_from_cstr(".a")));
    ASSERT(nob_sv_eq(shared_lib->prefix, sv_from_cstr("lib")));
    ASSERT(nob_sv_eq(shared_lib->suffix, sv_from_cstr(".dylib")));
#else
    ASSERT(nob_sv_eq(exe->suffix, sv_from_cstr("")));
    ASSERT(nob_sv_eq(static_lib->prefix, sv_from_cstr("lib")));
    ASSERT(nob_sv_eq(static_lib->suffix, sv_from_cstr(".a")));
    ASSERT(nob_sv_eq(shared_lib->prefix, sv_from_cstr("lib")));
    ASSERT(nob_sv_eq(shared_lib->suffix, sv_from_cstr(".so")));
#endif
    
    arena_destroy(arena);
    TEST_PASS();
}

TEST(find_target_index_api) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    build_model_add_target(model, sv_from_cstr("core"), TARGET_STATIC_LIB);
    build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);

    ASSERT(build_model_find_target_index(model, sv_from_cstr("core")) == 0);
    ASSERT(build_model_find_target_index(model, sv_from_cstr("app")) == 1);
    ASSERT(build_model_find_target_index(model, sv_from_cstr("missing")) == -1);
    ASSERT(build_model_find_target_index(NULL, sv_from_cstr("core")) == -1);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(string_list_unique_helpers) {
    Arena *arena = arena_create(1024 * 1024);
    String_List list = {0};
    string_list_init(&list);

    ASSERT(string_list_contains(&list, sv_from_cstr("A")) == false);
    ASSERT(string_list_add_unique(&list, arena, sv_from_cstr("A")) == true);
    ASSERT(string_list_add_unique(&list, arena, sv_from_cstr("A")) == false);
    ASSERT(string_list_add_unique(&list, arena, sv_from_cstr("B")) == true);
    ASSERT(list.count == 2);
    ASSERT(string_list_contains(&list, sv_from_cstr("A")) == true);
    ASSERT(string_list_contains(&list, sv_from_cstr("B")) == true);
    ASSERT(string_list_add_unique(&list, arena, sv_from_cstr("")) == false);
    ASSERT(string_list_add_unique(NULL, arena, sv_from_cstr("X")) == false);
    ASSERT(string_list_add_unique(&list, NULL, sv_from_cstr("X")) == false);

    arena_destroy(arena);
    TEST_PASS();
}

void run_build_model_tests(int *passed, int *failed) {
    test_create_model(passed, failed);
    test_add_target(passed, failed);
    test_find_target(passed, failed);
    test_add_source(passed, failed);
    test_add_source_deduplicates(passed, failed);
    test_add_dependency(passed, failed);
    test_add_library(passed, failed);
    test_add_definition(passed, failed);
    test_add_include_directory(passed, failed);
    test_set_property(passed, failed);
    test_cache_variable(passed, failed);
    test_multiple_targets(passed, failed);
    test_validate_dependencies(passed, failed);
    test_invalid_dependency(passed, failed);
    test_topological_sort(passed, failed);
    test_topological_sort_linear_chain(passed, failed);
    test_cyclic_dependencies_invalid(passed, failed);
    test_topological_sort_cycle_returns_null(passed, failed);
    test_duplicate_target_type_conflict(passed, failed);
    test_topological_sort_empty_model(passed, failed);
    test_topological_sort_null_args(passed, failed);
    test_project_info(passed, failed);
    test_target_prefixes_suffixes(passed, failed);
    test_find_target_index_api(passed, failed);
    test_string_list_unique_helpers(passed, failed);
}
