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


TEST(compile_options_visibility) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    Build_Target *t = build_model_add_target(model, sv_from_cstr("t"), TARGET_STATIC_LIB);
    ASSERT(t != NULL);

    // PRIVATE: only target properties
    build_target_add_compile_option(t, arena, sv_from_cstr("-DPRIV"), VISIBILITY_PRIVATE, CONFIG_ALL);
    ASSERT(t->properties[CONFIG_ALL].compile_options.count == 1);
    ASSERT(nob_sv_eq(t->properties[CONFIG_ALL].compile_options.items[0], sv_from_cstr("-DPRIV")));
    ASSERT(t->interface_compile_options.count == 0);

    // INTERFACE: only interface list
    build_target_add_compile_option(t, arena, sv_from_cstr("-DIFACE"), VISIBILITY_INTERFACE, CONFIG_ALL);
    ASSERT(t->properties[CONFIG_ALL].compile_options.count == 1);
    ASSERT(t->interface_compile_options.count == 1);
    ASSERT(nob_sv_eq(t->interface_compile_options.items[0], sv_from_cstr("-DIFACE")));

    // PUBLIC: both
    build_target_add_compile_option(t, arena, sv_from_cstr("-DPUB"), VISIBILITY_PUBLIC, CONFIG_ALL);
    ASSERT(t->properties[CONFIG_ALL].compile_options.count == 2);
    ASSERT(nob_sv_eq(t->properties[CONFIG_ALL].compile_options.items[1], sv_from_cstr("-DPUB")));
    ASSERT(t->interface_compile_options.count == 2);
    ASSERT(nob_sv_eq(t->interface_compile_options.items[1], sv_from_cstr("-DPUB")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(link_options_visibility) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    Build_Target *t = build_model_add_target(model, sv_from_cstr("t"), TARGET_SHARED_LIB);
    ASSERT(t != NULL);

    build_target_add_link_option(t, arena, sv_from_cstr("-Wl,--as-needed"), VISIBILITY_PRIVATE, CONFIG_ALL);
    ASSERT(t->properties[CONFIG_ALL].link_options.count == 1);
    ASSERT(t->interface_link_options.count == 0);

    build_target_add_link_option(t, arena, sv_from_cstr("-Wl,--no-undefined"), VISIBILITY_INTERFACE, CONFIG_ALL);
    ASSERT(t->properties[CONFIG_ALL].link_options.count == 1);
    ASSERT(t->interface_link_options.count == 1);

    build_target_add_link_option(t, arena, sv_from_cstr("-Wl,-rpath,$ORIGIN"), VISIBILITY_PUBLIC, CONFIG_ALL);
    ASSERT(t->properties[CONFIG_ALL].link_options.count == 2);
    ASSERT(t->interface_link_options.count == 2);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(link_directories_visibility) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    Build_Target *t = build_model_add_target(model, sv_from_cstr("t"), TARGET_EXECUTABLE);
    ASSERT(t != NULL);

    build_target_add_link_directory(t, arena, sv_from_cstr("priv_dir"), VISIBILITY_PRIVATE, CONFIG_ALL);
    ASSERT(t->properties[CONFIG_ALL].link_directories.count == 1);
    ASSERT(t->interface_link_directories.count == 0);

    build_target_add_link_directory(t, arena, sv_from_cstr("iface_dir"), VISIBILITY_INTERFACE, CONFIG_ALL);
    ASSERT(t->properties[CONFIG_ALL].link_directories.count == 1);
    ASSERT(t->interface_link_directories.count == 1);

    build_target_add_link_directory(t, arena, sv_from_cstr("pub_dir"), VISIBILITY_PUBLIC, CONFIG_ALL);
    ASSERT(t->properties[CONFIG_ALL].link_directories.count == 2);
    ASSERT(t->interface_link_directories.count == 2);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(interface_dependencies_dedupe) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    Build_Target *t = build_model_add_target(model, sv_from_cstr("t"), TARGET_STATIC_LIB);
    ASSERT(t != NULL);

    build_target_add_interface_dependency(t, arena, sv_from_cstr("dep"));
    build_target_add_interface_dependency(t, arena, sv_from_cstr("dep"));
    ASSERT(t->interface_dependencies.count == 1);
    ASSERT(nob_sv_eq(t->interface_dependencies.items[0], sv_from_cstr("dep")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(custom_property_overwrite) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    Build_Target *t = build_model_add_target(model, sv_from_cstr("t"), TARGET_EXECUTABLE);
    ASSERT(t != NULL);

    build_target_set_property(t, arena, sv_from_cstr("KEY"), sv_from_cstr("v1"));
    ASSERT(nob_sv_eq(build_target_get_property(t, sv_from_cstr("KEY")), sv_from_cstr("v1")));
    build_target_set_property(t, arena, sv_from_cstr("KEY"), sv_from_cstr("v2"));
    ASSERT(nob_sv_eq(build_target_get_property(t, sv_from_cstr("KEY")), sv_from_cstr("v2")));
    ASSERT(t->custom_properties.count == 1);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(cache_variable_overwrite) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    build_model_set_cache_variable(model, sv_from_cstr("CMAKE_CXX_STANDARD"), sv_from_cstr("17"), sv_from_cstr("STRING"), sv_from_cstr(""));
    ASSERT(nob_sv_eq(build_model_get_cache_variable(model, sv_from_cstr("CMAKE_CXX_STANDARD")), sv_from_cstr("17")));
    build_model_set_cache_variable(model, sv_from_cstr("CMAKE_CXX_STANDARD"), sv_from_cstr("20"), sv_from_cstr("STRING"), sv_from_cstr(""));
    ASSERT(nob_sv_eq(build_model_get_cache_variable(model, sv_from_cstr("CMAKE_CXX_STANDARD")), sv_from_cstr("20")));
    ASSERT(model->cache_variables.count == 1);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_package_dedup_and_init) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    Found_Package *p1 = build_model_add_package(model, sv_from_cstr("ZLIB"), true);
    ASSERT(p1 != NULL);
    ASSERT(model->package_count == 1);
    ASSERT(nob_sv_eq(p1->name, sv_from_cstr("ZLIB")));
    ASSERT(p1->found == true);
    ASSERT(p1->include_dirs.count == 0);
    ASSERT(p1->libraries.count == 0);
    ASSERT(p1->definitions.count == 0);
    ASSERT(p1->options.count == 0);

    // Dedupe: returns existing, does not add new
    Found_Package *p2 = build_model_add_package(model, sv_from_cstr("ZLIB"), false);
    ASSERT(p2 == p1);
    ASSERT(model->package_count == 1);
    ASSERT(p1->found == true);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(add_test_dedup_updates_fields) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    Build_Test *t1 = build_model_add_test(model, sv_from_cstr("t"), sv_from_cstr("cmd1"), sv_from_cstr("wd1"), false);
    ASSERT(t1 != NULL);
    ASSERT(model->test_count == 1);
    ASSERT(nob_sv_eq(t1->command, sv_from_cstr("cmd1")));
    ASSERT(nob_sv_eq(t1->working_directory, sv_from_cstr("wd1")));
    ASSERT(t1->command_expand_lists == false);

    Build_Test *t2 = build_model_add_test(model, sv_from_cstr("t"), sv_from_cstr("cmd2"), sv_from_cstr("wd2"), true);
    ASSERT(t2 == t1);
    ASSERT(model->test_count == 1);
    ASSERT(nob_sv_eq(t1->command, sv_from_cstr("cmd2")));
    ASSERT(nob_sv_eq(t1->working_directory, sv_from_cstr("wd2")));
    ASSERT(t1->command_expand_lists == true);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(cpack_dedup_basic) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    CPack_Install_Type *it1 = build_model_add_cpack_install_type(model, sv_from_cstr("Full"));
    ASSERT(it1 != NULL);
    ASSERT(model->cpack_install_type_count == 1);
    CPack_Install_Type *it2 = build_model_add_cpack_install_type(model, sv_from_cstr("Full"));
    ASSERT(it2 == it1);
    ASSERT(model->cpack_install_type_count == 1);

    CPack_Component_Group *g1 = build_model_add_cpack_component_group(model, sv_from_cstr("Runtime"));
    ASSERT(g1 != NULL);
    ASSERT(model->cpack_component_group_count == 1);
    CPack_Component_Group *g2 = build_model_add_cpack_component_group(model, sv_from_cstr("Runtime"));
    ASSERT(g2 == g1);
    ASSERT(model->cpack_component_group_count == 1);

    CPack_Component *c1 = build_model_add_cpack_component(model, sv_from_cstr("App"));
    ASSERT(c1 != NULL);
    ASSERT(model->cpack_component_count == 1);
    CPack_Component *c2 = build_model_add_cpack_component(model, sv_from_cstr("App"));
    ASSERT(c2 == c1);
    ASSERT(model->cpack_component_count == 1);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(fase1_target_flags_and_alias) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    Build_Target *t = build_model_add_target(model, sv_from_cstr("app"), TARGET_EXECUTABLE);
    Build_Target *alias = build_model_add_target(model, sv_from_cstr("app_alias"), TARGET_ALIAS);
    ASSERT(t != NULL);
    ASSERT(alias != NULL);

    build_target_set_flag(t, TARGET_FLAG_WIN32_EXECUTABLE, true);
    build_target_set_flag(t, TARGET_FLAG_MACOSX_BUNDLE, true);
    build_target_set_flag(t, TARGET_FLAG_EXCLUDE_FROM_ALL, true);
    build_target_set_flag(t, TARGET_FLAG_IMPORTED, true);
    ASSERT(t->win32_executable == true);
    ASSERT(t->macosx_bundle == true);
    ASSERT(t->exclude_from_all == true);
    ASSERT(t->imported == true);

    build_target_set_alias(alias, arena, sv_from_cstr("app"));
    ASSERT(alias->alias == true);
    ASSERT(alias->dependencies.count == 1);
    ASSERT(nob_sv_eq(alias->dependencies.items[0], sv_from_cstr("app")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(fase1_env_and_global_args) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    build_model_set_env_var(model, arena, sv_from_cstr("PATH"), sv_from_cstr("A"));
    build_model_set_env_var(model, arena, sv_from_cstr("PATH"), sv_from_cstr("B"));
    ASSERT(model->environment_variables.count == 1);
    ASSERT(nob_sv_eq(model->environment_variables.items[0].name, sv_from_cstr("PATH")));
    ASSERT(nob_sv_eq(model->environment_variables.items[0].value, sv_from_cstr("B")));

    build_model_process_global_definition_arg(model, arena, sv_from_cstr("-DONE=1"));
    build_model_process_global_definition_arg(model, arena, sv_from_cstr("/DTWO=2"));
    build_model_process_global_definition_arg(model, arena, sv_from_cstr("-Wall"));
    ASSERT(model->global_definitions.count == 2);
    ASSERT(string_list_contains(&model->global_definitions, sv_from_cstr("ONE=1")) == true);
    ASSERT(string_list_contains(&model->global_definitions, sv_from_cstr("TWO=2")) == true);
    ASSERT(model->global_compile_options.count == 1);
    ASSERT(nob_sv_eq(model->global_compile_options.items[0], sv_from_cstr("-Wall")));

    build_model_remove_global_definition(model, sv_from_cstr("ONE=1"));
    ASSERT(model->global_definitions.count == 1);
    ASSERT(nob_sv_eq(model->global_definitions.items[0], sv_from_cstr("TWO=2")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(fase1_property_smart_and_computed) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    Build_Target *t = build_model_add_target(model, sv_from_cstr("core"), TARGET_STATIC_LIB);
    ASSERT(t != NULL);

    build_target_set_property_smart(t, arena, sv_from_cstr("OUTPUT_NAME"), sv_from_cstr("core_out"));
    build_target_set_property_smart(t, arena, sv_from_cstr("PREFIX"), sv_from_cstr("pre_"));
    build_target_set_property_smart(t, arena, sv_from_cstr("SUFFIX"), sv_from_cstr(".ext"));
    build_target_set_property_smart(t, arena, sv_from_cstr("COMPILE_DEFINITIONS"), sv_from_cstr("A=1;B=2"));
    build_target_set_property_smart(t, arena, sv_from_cstr("COMPILE_DEFINITIONS_DEBUG"), sv_from_cstr("DBG=1"));

    ASSERT(nob_sv_eq(t->output_name, sv_from_cstr("core_out")));
    ASSERT(nob_sv_eq(t->prefix, sv_from_cstr("pre_")));
    ASSERT(nob_sv_eq(t->suffix, sv_from_cstr(".ext")));
    ASSERT(t->properties[CONFIG_ALL].compile_definitions.count == 2);
    ASSERT(t->properties[CONFIG_DEBUG].compile_definitions.count == 1);

    ASSERT(nob_sv_eq(build_target_get_property_computed(t, sv_from_cstr("NAME"), sv_from_cstr("Debug")), sv_from_cstr("core")));
    ASSERT(nob_sv_eq(build_target_get_property_computed(t, sv_from_cstr("TYPE"), sv_from_cstr("Debug")), sv_from_cstr("STATIC_LIBRARY")));
    ASSERT(nob_sv_eq(build_target_get_property_computed(t, sv_from_cstr("OUTPUT_NAME"), sv_from_cstr("Debug")), sv_from_cstr("core_out")));
    ASSERT(nob_sv_eq(build_target_get_property_computed(t, sv_from_cstr("COMPILE_DEFINITIONS"), sv_from_cstr("Debug")), sv_from_cstr("A=1;B=2")));
    ASSERT(nob_sv_eq(build_target_get_property_computed(t, sv_from_cstr("COMPILE_DEFINITIONS_DEBUG"), sv_from_cstr("Debug")), sv_from_cstr("DBG=1")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(fase1_global_link_library_framework) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    build_model_add_global_link_library(model, arena, sv_from_cstr("-framework Cocoa"));
    build_model_add_global_link_library(model, arena, sv_from_cstr("m"));

    ASSERT(model->global_link_libraries.count == 3);
    ASSERT(nob_sv_eq(model->global_link_libraries.items[0], sv_from_cstr("-framework")));
    ASSERT(nob_sv_eq(model->global_link_libraries.items[1], sv_from_cstr("Cocoa")));
    ASSERT(nob_sv_eq(model->global_link_libraries.items[2], sv_from_cstr("m")));

    arena_destroy(arena);
    TEST_PASS();
}

TEST(fase1_cpack_wrappers_and_setters) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    CPack_Install_Type *it1 = build_model_get_or_create_cpack_install_type(model, arena, sv_from_cstr("Full"));
    CPack_Install_Type *it2 = build_model_get_or_create_cpack_install_type(model, arena, sv_from_cstr("Full"));
    ASSERT(it1 == it2);
    build_cpack_install_type_set_display_name(it1, sv_from_cstr("Full Install"));
    ASSERT(nob_sv_eq(it1->display_name, sv_from_cstr("Full Install")));

    CPack_Component_Group *g = build_model_get_or_create_cpack_group(model, arena, sv_from_cstr("Runtime"));
    build_cpack_group_set_display_name(g, sv_from_cstr("Runtime Group"));
    build_cpack_group_set_description(g, sv_from_cstr("Runtime files"));
    build_cpack_group_set_parent_group(g, sv_from_cstr("Parent"));
    build_cpack_group_set_expanded(g, true);
    build_cpack_group_set_bold_title(g, true);
    ASSERT(nob_sv_eq(g->display_name, sv_from_cstr("Runtime Group")));
    ASSERT(nob_sv_eq(g->description, sv_from_cstr("Runtime files")));
    ASSERT(nob_sv_eq(g->parent_group, sv_from_cstr("Parent")));
    ASSERT(g->expanded == true);
    ASSERT(g->bold_title == true);

    CPack_Component *c = build_model_get_or_create_cpack_component(model, arena, sv_from_cstr("core"));
    build_cpack_component_set_display_name(c, sv_from_cstr("Core"));
    build_cpack_component_set_description(c, sv_from_cstr("Core component"));
    build_cpack_component_set_group(c, sv_from_cstr("Runtime"));
    build_cpack_component_add_dependency(c, arena, sv_from_cstr("base"));
    build_cpack_component_add_install_type(c, arena, sv_from_cstr("Full"));
    build_cpack_component_set_required(c, true);
    build_cpack_component_set_hidden(c, true);
    build_cpack_component_set_disabled(c, true);
    build_cpack_component_set_downloaded(c, true);
    ASSERT(nob_sv_eq(c->display_name, sv_from_cstr("Core")));
    ASSERT(c->depends.count == 1);
    ASSERT(c->install_types.count == 1);
    ASSERT(c->required == true && c->hidden == true && c->disabled == true && c->downloaded == true);
    build_cpack_component_clear_dependencies(c);
    build_cpack_component_clear_install_types(c);
    ASSERT(c->depends.count == 0);
    ASSERT(c->install_types.count == 0);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(fase1_custom_commands_and_path_helpers) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);
    Build_Target *t = build_model_add_target(model, sv_from_cstr("tool"), TARGET_UTILITY);
    ASSERT(t != NULL);

    Custom_Command *pre = build_target_add_custom_command_ex(
        t, arena, true, sv_from_cstr("echo pre"), sv_from_cstr("wd"), sv_from_cstr("comment"));
    ASSERT(pre != NULL);
    ASSERT(t->pre_build_count == 1);
    ASSERT(nob_sv_eq(pre->command, sv_from_cstr("echo pre")));

    Custom_Command *post = build_target_add_custom_command(t, arena, false, sv_from_cstr("echo post"));
    ASSERT(post != NULL);
    ASSERT(t->post_build_count == 1);

    Custom_Command *out = build_model_add_custom_command_output(model, arena, sv_from_cstr("out.txt"), sv_from_cstr("echo out"));
    ASSERT(out != NULL);
    ASSERT(model->output_custom_command_count == 1);
    ASSERT(out->outputs.count == 1);
    ASSERT(nob_sv_eq(out->outputs.items[0], sv_from_cstr("out.txt")));

    ASSERT(build_path_is_absolute(sv_from_cstr("/tmp")) == true);
    ASSERT(build_path_is_absolute(sv_from_cstr("C:/tmp")) == true);
    ASSERT(build_path_is_absolute(sv_from_cstr("rel/path")) == false);
    ASSERT(nob_sv_eq(build_path_join(arena, sv_from_cstr("base"), sv_from_cstr("rel")), sv_from_cstr("base/rel")));
    ASSERT(nob_sv_eq(build_path_parent_dir(arena, sv_from_cstr("a/b/c.txt")), sv_from_cstr("a/b")));
    ASSERT(build_path_make_absolute(arena, sv_from_cstr("local.txt")).count > 0);

    arena_destroy(arena);
    TEST_PASS();
}

TEST(fase1_add_test_ex_wrapper) {
    Arena *arena = arena_create(1024 * 1024);
    Build_Model *model = build_model_create(arena);

    Build_Test *t = build_model_add_test_ex(model, arena, sv_from_cstr("smoke"), sv_from_cstr("app"), sv_from_cstr("tests"));
    ASSERT(t != NULL);
    ASSERT(model->test_count == 1);
    ASSERT(t->command_expand_lists == false);
    ASSERT(nob_sv_eq(t->working_directory, sv_from_cstr("tests")));

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
    test_compile_options_visibility(passed, failed);
    test_link_options_visibility(passed, failed);
    test_link_directories_visibility(passed, failed);
    test_interface_dependencies_dedupe(passed, failed);
    test_custom_property_overwrite(passed, failed);
    test_set_property(passed, failed);
    test_cache_variable(passed, failed);
    test_cache_variable_overwrite(passed, failed);
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
    test_add_package_dedup_and_init(passed, failed);
    test_add_test_dedup_updates_fields(passed, failed);
    test_cpack_dedup_basic(passed, failed);
    test_fase1_target_flags_and_alias(passed, failed);
    test_fase1_env_and_global_args(passed, failed);
    test_fase1_property_smart_and_computed(passed, failed);
    test_fase1_global_link_library_framework(passed, failed);
    test_fase1_cpack_wrappers_and_setters(passed, failed);
    test_fase1_custom_commands_and_path_helpers(passed, failed);
    test_fase1_add_test_ex_wrapper(passed, failed);
}
