#include "build_model.h"
#include "arena_dyn.h"
#include "ds_adapter.h"
#include <ctype.h>

static void build_model_heap_cleanup(void *userdata) {
    Build_Model *model = (Build_Model*)userdata;
    if (!model) return;
    ds_shfree(model->target_index_by_name);
}

static int build_model_lookup_target_index(const Build_Model *model, String_View name) {
    if (!model || !model->target_index_by_name) return -1;
    Build_Target_Index_Entry *index_map = model->target_index_by_name;
    Build_Target_Index_Entry *entry = ds_shgetp_null(index_map, nob_temp_sv_to_cstr(name));
    if (!entry) return -1;
    return entry->value;
}

static bool build_model_put_target_index(Build_Model *model, char *key, int index) {
    if (!model || !key || key[0] == '\0') return false;
    ds_shput(model->target_index_by_name, key, index);
    return true;
}

static Build_Target* build_model_find_target_const(const Build_Model *model, String_View name) {
    if (!model) return NULL;
    int idx = build_model_lookup_target_index(model, name);
    if (idx < 0 || (size_t)idx >= model->target_count) return NULL;
    return (Build_Target*)&model->targets[idx];
}

int build_model_find_target_index(const Build_Model *model, String_View name) {
    return build_model_lookup_target_index(model, name);
}

static bool build_model_has_cycle_dfs(const Build_Model *model, size_t idx, uint8_t *state) {
    if (state[idx] == 1) return true;
    if (state[idx] == 2) return false;

    state[idx] = 1;
    const Build_Target *target = &model->targets[idx];

    for (size_t j = 0; j < target->dependencies.count; j++) {
        int dep_idx = build_model_find_target_index(model, target->dependencies.items[j]);
        if (dep_idx < 0) continue;
        if (build_model_has_cycle_dfs(model, (size_t)dep_idx, state)) {
            return true;
        }
    }

    state[idx] = 2;
    return false;
}

// ============================================================================
// FUNÇÕES AUXILIARES
// ============================================================================

void string_list_init(String_List *list) {
    if (!list) return;
    memset(list, 0, sizeof(*list));
}

void string_list_add(String_List *list, Arena *arena, String_View item) {
    if (!list || !arena) return;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(*list->items), list->count + 1)) return;
    list->items[list->count++] = item;
}

bool string_list_contains(const String_List *list, String_View item) {
    if (!list) return false;
    for (size_t i = 0; i < list->count; i++) {
        if (nob_sv_eq(list->items[i], item)) return true;
    }
    return false;
}

bool string_list_add_unique(String_List *list, Arena *arena, String_View item) {
    if (!list || !arena || item.count == 0) return false;
    if (string_list_contains(list, item)) return false;
    string_list_add(list, arena, item);
    return true;
}

void property_list_init(Property_List *list) {
    if (!list) return;
    memset(list, 0, sizeof(*list));
}

void property_list_add(Property_List *list, Arena *arena, 
                       String_View key, String_View value) {
    if (!list || !arena) return;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(*list->items), list->count + 1)) return;
    list->items[list->count].name = key;
    list->items[list->count].value = value;
    list->count++;
}

String_View property_list_find(Property_List *list, String_View key) {
    if (!list) return sv_from_cstr("");
    for (size_t i = 0; i < list->count; i++) {
        if (nob_sv_eq(list->items[i].name, key)) {
            return list->items[i].value;
        }
    }
    return sv_from_cstr("");
}

// ============================================================================
// IMPLEMENTAÇÃO PÚBLICA
// ============================================================================

Build_Model* build_model_create(Arena *arena) {
    if (!arena) return NULL;
    Build_Model *model = arena_alloc_zero(arena, sizeof(Build_Model));
    if (!model) return NULL;
    model->arena = arena;
    if (!arena_on_destroy(arena, build_model_heap_cleanup, model)) {
        nob_log(NOB_WARNING, "Falha ao registrar cleanup de heap do Build_Model");
    }
    
    // Inicializa listas
    string_list_init(&model->project_languages);
    string_list_init(&model->directories.include_dirs);
    string_list_init(&model->directories.system_include_dirs);
    string_list_init(&model->directories.link_dirs);
    
    property_list_init(&model->cache_variables);
    property_list_init(&model->environment_variables);
    
    string_list_init(&model->global_definitions);
    string_list_init(&model->global_compile_options);
    string_list_init(&model->global_link_options);
    string_list_init(&model->global_link_libraries);
    
    string_list_init(&model->install_rules.targets);
    string_list_init(&model->install_rules.files);
    string_list_init(&model->install_rules.directories);
    string_list_init(&model->install_rules.programs);
    
    // Configurações padrão
    model->default_config = sv_from_cstr("Debug");
    model->config_debug = true;
    model->config_release = true;
    
    // Padrões de linguagem
    model->language_standards.c_standard = sv_from_cstr("11");
    model->language_standards.cxx_standard = sv_from_cstr("11");
    model->language_standards.c_extensions = true;
    model->language_standards.cxx_extensions = true;

#if defined(_WIN32)
    model->is_windows = true;
#endif
#if defined(__APPLE__)
    model->is_apple = true;
    model->is_unix = true;
#endif
#if defined(__linux__)
    model->is_linux = true;
    model->is_unix = true;
#endif
#if defined(__unix__) || defined(__unix)
    model->is_unix = true;
#endif
    
    return model;
}

Build_Target* build_model_add_target(Build_Model *model, 
                                     String_View name, 
                                     Target_Type type) {
    if (!model) return NULL;
    // Verifica se ja existe
    int existing_idx = build_model_lookup_target_index(model, name);
    if (existing_idx >= 0 && (size_t)existing_idx < model->target_count) {
        Build_Target *existing = &model->targets[existing_idx];
        if (existing->type != type) {
            nob_log(NOB_ERROR, "Target '"SV_Fmt"' redefinido com tipo diferente", SV_Arg(name));
            return NULL;
        }
        return existing;
    }
    
    // Expande array se necessário
    if (model->target_count >= model->target_capacity) {
        if (!arena_da_reserve(model->arena, (void**)&model->targets, &model->target_capacity,
                sizeof(*model->targets), model->target_count + 1)) {
            return NULL;
        }
    }
    
    Build_Target *target = &model->targets[model->target_count++];
    memset(target, 0, sizeof(Build_Target));
    
    char *name_copy = arena_strndup(model->arena, name.data, name.count);
    if (!name_copy) {
        model->target_count--;
        memset(target, 0, sizeof(*target));
        nob_log(NOB_ERROR, "Falha ao copiar nome do target '"SV_Fmt"'", SV_Arg(name));
        return NULL;
    }
    target->name = sv_from_cstr(name_copy);
    target->type = type;
    
    // Inicializa listas
    string_list_init(&target->sources);
    string_list_init(&target->source_groups);
    string_list_init(&target->dependencies);
    string_list_init(&target->interface_dependencies);
    string_list_init(&target->link_libraries);
    string_list_init(&target->interface_libs);
    string_list_init(&target->interface_compile_definitions);
    string_list_init(&target->interface_compile_options);
    string_list_init(&target->interface_include_directories);
    string_list_init(&target->interface_link_options);
    string_list_init(&target->interface_link_directories);
    property_list_init(&target->custom_properties);
    
    // Inicializa propriedades por configuração
    for (int i = 0; i <= CONFIG_ALL; i++) {
        string_list_init(&target->properties[i].compile_definitions);
        string_list_init(&target->properties[i].compile_options);
        string_list_init(&target->properties[i].include_directories);
        string_list_init(&target->properties[i].link_options);
        string_list_init(&target->properties[i].link_directories);
    }
    
    // Prefixos e sufixos padrão
    switch (type) {
        case TARGET_EXECUTABLE:
            target->prefix = sv_from_cstr("");
#if defined(_WIN32)
            target->suffix = sv_from_cstr(".exe");
#else
            target->suffix = sv_from_cstr("");
#endif
            break;
        case TARGET_STATIC_LIB:
#if defined(_WIN32)
            target->prefix = sv_from_cstr("");
            target->suffix = sv_from_cstr(".lib");
#else
            target->prefix = sv_from_cstr("lib");
            target->suffix = sv_from_cstr(".a");
#endif
            break;
        case TARGET_SHARED_LIB:
#if defined(_WIN32)
            target->prefix = sv_from_cstr("");
            target->suffix = sv_from_cstr(".dll");
#elif defined(__APPLE__)
            target->prefix = sv_from_cstr("lib");
            target->suffix = sv_from_cstr(".dylib");
#else
            target->prefix = sv_from_cstr("lib");
            target->suffix = sv_from_cstr(".so");
#endif
            break;
        case TARGET_OBJECT_LIB:
            target->prefix = sv_from_cstr("");
            target->suffix = sv_from_cstr("");
            break;
        default:
            target->prefix = sv_from_cstr("");
            target->suffix = sv_from_cstr("");
    }
    
    if (!build_model_put_target_index(model, name_copy, (int)(model->target_count - 1))) {
        model->target_count--;
        memset(target, 0, sizeof(*target));
        nob_log(NOB_ERROR, "Falha ao indexar target '"SV_Fmt"'", SV_Arg(name));
        return NULL;
    }

    return target;
}

Build_Target* build_model_find_target(Build_Model *model, String_View name) {
    return build_model_find_target_const(model, name);
}

void build_target_add_source(Build_Target *target, Arena *arena, String_View source) {
    if (!target || !arena) return;
    string_list_add_unique(&target->sources, arena, source);
}

void build_target_add_dependency(Build_Target *target, Arena *arena, String_View dep_name) {
    if (!target || !arena) return;
    for (size_t i = 0; i < target->dependencies.count; i++) {
        if (nob_sv_eq(target->dependencies.items[i], dep_name)) {
            return;
        }
    }
    string_list_add(&target->dependencies, arena, dep_name);
}

void build_target_add_definition(Build_Target *target, 
                                 Arena *arena,
                                 String_View definition, 
                                 Visibility visibility,
                                 Build_Config config) {
    if (!target || !arena || config > CONFIG_ALL) return;
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->properties[config].compile_definitions, arena, definition);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_compile_definitions, arena, definition);
    }
}

void build_target_add_include_directory(Build_Target *target,
                                        Arena *arena,
                                        String_View directory,
                                        Visibility visibility,
                                        Build_Config config) {
    if (!target || !arena || config > CONFIG_ALL) return;
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->properties[config].include_directories, arena, directory);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_include_directories, arena, directory);
    }
}


void build_target_add_library(Build_Target *target,
                              Arena *arena,
                              String_View library,
                              Visibility visibility) {
    if (!target || !arena) return;
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->link_libraries, arena, library);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_libs, arena, library);
    }
}

void build_target_add_compile_option(Build_Target *target,
                                     Arena *arena,
                                     String_View option,
                                     Visibility visibility,
                                     Build_Config config) {
    if (!target || !arena || config > CONFIG_ALL) return;
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->properties[config].compile_options, arena, option);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_compile_options, arena, option);
    }
}

void build_target_add_link_option(Build_Target *target,
                                  Arena *arena,
                                  String_View option,
                                  Visibility visibility,
                                  Build_Config config) {
    if (!target || !arena || config > CONFIG_ALL) return;
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->properties[config].link_options, arena, option);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_link_options, arena, option);
    }
}

void build_target_add_link_directory(Build_Target *target,
                                     Arena *arena,
                                     String_View directory,
                                     Visibility visibility,
                                     Build_Config config) {
    if (!target || !arena || config > CONFIG_ALL) return;
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->properties[config].link_directories, arena, directory);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_link_directories, arena, directory);
    }
}

void build_target_add_interface_dependency(Build_Target *target,
                                           Arena *arena,
                                           String_View dep_name) {
    if (!target || !arena) return;
    for (size_t i = 0; i < target->interface_dependencies.count; i++) {
        if (nob_sv_eq(target->interface_dependencies.items[i], dep_name)) {
            return;
        }
    }
    string_list_add(&target->interface_dependencies, arena, dep_name);
}

void build_target_set_property(Build_Target *target,
                               Arena *arena,
                               String_View key,
                               String_View value) {
    if (!target || !arena) return;
    // Atualiza se já existir
    for (size_t i = 0; i < target->custom_properties.count; i++) {
        if (nob_sv_eq(target->custom_properties.items[i].name, key)) {
            target->custom_properties.items[i].value = value;
            return;
        }
    }
    
    // Adiciona nova
    property_list_add(&target->custom_properties, arena, key, value);
}

String_View build_target_get_property(Build_Target *target, String_View key) {
    if (!target) return sv_from_cstr("");
    return property_list_find(&target->custom_properties, key);
}

Found_Package* build_model_add_package(Build_Model *model,
                                       String_View name,
                                       bool found) {
    if (!model) return NULL;
    // Verifica se já existe
    for (size_t i = 0; i < model->package_count; i++) {
        if (nob_sv_eq(model->found_packages[i].name, name)) {
            return &model->found_packages[i];
        }
    }
    
    // Expande array se necessário
    if (model->package_count >= model->package_capacity) {
        if (!arena_da_reserve(model->arena, (void**)&model->found_packages, &model->package_capacity,
                sizeof(*model->found_packages), model->package_count + 1)) {
            return NULL;
        }
    }
    
    Found_Package *pkg = &model->found_packages[model->package_count++];
    memset(pkg, 0, sizeof(Found_Package));
    
    pkg->name = name;
    pkg->found = found;
    
    // Inicializa listas
    string_list_init(&pkg->include_dirs);
    string_list_init(&pkg->libraries);
    string_list_init(&pkg->definitions);
    string_list_init(&pkg->options);
    property_list_init(&pkg->properties);
    
    return pkg;
}

Build_Test* build_model_add_test(Build_Model *model,
                                 String_View name,
                                 String_View command,
                                 String_View working_directory,
                                 bool command_expand_lists) {
    if (!model || name.count == 0) return NULL;

    for (size_t i = 0; i < model->test_count; i++) {
        if (nob_sv_eq(model->tests[i].name, name)) {
            model->tests[i].command = command;
            model->tests[i].working_directory = working_directory;
            model->tests[i].command_expand_lists = command_expand_lists;
            return &model->tests[i];
        }
    }

    if (model->test_count >= model->test_capacity) {
        if (!arena_da_reserve(model->arena, (void**)&model->tests, &model->test_capacity,
                sizeof(*model->tests), model->test_count + 1)) {
            return NULL;
        }
    }

    Build_Test *test = &model->tests[model->test_count++];
    memset(test, 0, sizeof(*test));
    test->name = name;
    test->command = command;
    test->working_directory = working_directory;
    test->command_expand_lists = command_expand_lists;
    return test;
}

CPack_Component_Group* build_model_add_cpack_component_group(Build_Model *model, String_View name) {
    if (!model || name.count == 0) return NULL;
    for (size_t i = 0; i < model->cpack_component_group_count; i++) {
        if (nob_sv_eq(model->cpack_component_groups[i].name, name)) {
            return &model->cpack_component_groups[i];
        }
    }
    if (model->cpack_component_group_count >= model->cpack_component_group_capacity) {
        if (!arena_da_reserve(model->arena, (void**)&model->cpack_component_groups, &model->cpack_component_group_capacity,
                sizeof(*model->cpack_component_groups), model->cpack_component_group_count + 1)) {
            return NULL;
        }
    }
    CPack_Component_Group *group = &model->cpack_component_groups[model->cpack_component_group_count++];
    memset(group, 0, sizeof(*group));
    group->name = name;
    return group;
}

CPack_Install_Type* build_model_add_cpack_install_type(Build_Model *model, String_View name) {
    if (!model || name.count == 0) return NULL;
    for (size_t i = 0; i < model->cpack_install_type_count; i++) {
        if (nob_sv_eq(model->cpack_install_types[i].name, name)) {
            return &model->cpack_install_types[i];
        }
    }
    if (model->cpack_install_type_count >= model->cpack_install_type_capacity) {
        if (!arena_da_reserve(model->arena, (void**)&model->cpack_install_types, &model->cpack_install_type_capacity,
                sizeof(*model->cpack_install_types), model->cpack_install_type_count + 1)) {
            return NULL;
        }
    }
    CPack_Install_Type *install_type = &model->cpack_install_types[model->cpack_install_type_count++];
    memset(install_type, 0, sizeof(*install_type));
    install_type->name = name;
    return install_type;
}

CPack_Component* build_model_add_cpack_component(Build_Model *model, String_View name) {
    if (!model || name.count == 0) return NULL;
    for (size_t i = 0; i < model->cpack_component_count; i++) {
        if (nob_sv_eq(model->cpack_components[i].name, name)) {
            return &model->cpack_components[i];
        }
    }
    if (model->cpack_component_count >= model->cpack_component_capacity) {
        if (!arena_da_reserve(model->arena, (void**)&model->cpack_components, &model->cpack_component_capacity,
                sizeof(*model->cpack_components), model->cpack_component_count + 1)) {
            return NULL;
        }
    }
    CPack_Component *component = &model->cpack_components[model->cpack_component_count++];
    memset(component, 0, sizeof(*component));
    component->name = name;
    string_list_init(&component->depends);
    string_list_init(&component->install_types);
    return component;
}

void build_model_set_cache_variable(Build_Model *model,
                                    String_View key,
                                    String_View value,
                                    String_View type,
                                    String_View docstring) {
    if (!model) return;
    (void)type;
    (void)docstring;
    
    // Atualiza se já existir
    for (size_t i = 0; i < model->cache_variables.count; i++) {
        if (nob_sv_eq(model->cache_variables.items[i].name, key)) {
            model->cache_variables.items[i].value = value;
            return;
        }
    }
    
    // Adiciona nova
    property_list_add(&model->cache_variables, model->arena, key, value);
}

String_View build_model_get_cache_variable(Build_Model *model, String_View key) {
    if (!model) return sv_from_cstr("");
    return property_list_find(&model->cache_variables, key);
}

bool build_model_validate_dependencies(Build_Model *model) {
    if (!model) return false;

    for (size_t i = 0; i < model->target_count; i++) {
        Build_Target *target = &model->targets[i];
        for (size_t j = 0; j < target->dependencies.count; j++) {
            String_View dep_name = target->dependencies.items[j];
            if (!build_model_find_target(model, dep_name)) {
                return false;
            }
        }
    }

    if (model->target_count == 0) return true;

    uint8_t *state = arena_alloc_zero(model->arena, model->target_count * sizeof(uint8_t));
    if (!state) return false;
    for (size_t i = 0; i < model->target_count; i++) {
        if (state[i] == 0 && build_model_has_cycle_dfs(model, i, state)) {
            return false;
        }
    }

    return true;
}
Build_Target** build_model_topological_sort(Build_Model *model, size_t *count) {
    if (!count) return NULL;
    *count = 0;
    if (!model) return NULL;
    if (model->target_count == 0) return NULL;

    size_t n = model->target_count;
    size_t *in_degree = arena_alloc_array_zero(model->arena, size_t, n);
    size_t *queue = arena_alloc_array(model->arena, size_t, n);
    Build_Target **sorted = arena_alloc_array(model->arena, Build_Target*, n);
    if (!in_degree || !queue || !sorted) return NULL;

    for (size_t i = 0; i < n; i++) {
        Build_Target *target = &model->targets[i];
        for (size_t j = 0; j < target->dependencies.count; j++) {
            int dep_idx = build_model_find_target_index(model, target->dependencies.items[j]);
            if (dep_idx >= 0) {
                in_degree[i]++;
            }
        }
    }

    size_t q_front = 0;
    size_t q_back = 0;
    for (size_t i = 0; i < n; i++) {
        if (in_degree[i] == 0) {
            queue[q_back++] = i;
        }
    }

    size_t sorted_count = 0;
    while (q_front < q_back) {
        size_t current_idx = queue[q_front++];
        Build_Target *current = &model->targets[current_idx];
        sorted[sorted_count++] = current;

        for (size_t i = 0; i < n; i++) {
            if (in_degree[i] == 0) continue;

            Build_Target *other = &model->targets[i];
            for (size_t j = 0; j < other->dependencies.count; j++) {
                if (nob_sv_eq(other->dependencies.items[j], current->name)) {
                    in_degree[i]--;
                    if (in_degree[i] == 0) {
                        queue[q_back++] = i;
                    }
                    break;
                }
            }
        }
    }

    if (sorted_count != n) {
        *count = 0;
        return NULL;
    }

    *count = sorted_count;
    return sorted;
}
void build_model_dump(Build_Model *model, FILE *output) {
    fprintf(output, "=== BUILD MODEL DUMP ===\n\n");
    
    fprintf(output, "Project: "SV_Fmt" (Version: "SV_Fmt")\n", 
            SV_Arg(model->project_name), SV_Arg(model->project_version));
    
    fprintf(output, "Languages: ");
    for (size_t i = 0; i < model->project_languages.count; i++) {
        fprintf(output, SV_Fmt" ", SV_Arg(model->project_languages.items[i]));
    }
    fprintf(output, "\n\n");
    
    fprintf(output, "Targets (%zu):\n", model->target_count);
    for (size_t i = 0; i < model->target_count; i++) {
        Build_Target *t = &model->targets[i];
        
        const char *type_str = "UNKNOWN";
        switch (t->type) {
            case TARGET_EXECUTABLE: type_str = "EXECUTABLE"; break;
            case TARGET_STATIC_LIB: type_str = "STATIC LIB"; break;
            case TARGET_SHARED_LIB: type_str = "SHARED LIB"; break;
            case TARGET_OBJECT_LIB: type_str = "OBJECT LIB"; break;
            case TARGET_INTERFACE_LIB: type_str = "INTERFACE"; break;
            case TARGET_UTILITY: type_str = "UTILITY"; break;
            case TARGET_IMPORTED: type_str = "IMPORTED"; break;
            case TARGET_ALIAS: type_str = "ALIAS"; break;
        }
        
        fprintf(output, "  [%zu] "SV_Fmt" (%s)\n", i, SV_Arg(t->name), type_str);
        fprintf(output, "    Sources: %zu\n", t->sources.count);
        fprintf(output, "    Dependencies: %zu\n", t->dependencies.count);
        fprintf(output, "    Link libraries: %zu\n", t->link_libraries.count);
        
        if (t->custom_properties.count > 0) {
            fprintf(output, "    Properties:\n");
            for (size_t j = 0; j < t->custom_properties.count; j++) {
                fprintf(output, "      "SV_Fmt": "SV_Fmt"\n",
                        SV_Arg(t->custom_properties.items[j].name),
                        SV_Arg(t->custom_properties.items[j].value));
            }
        }
        fprintf(output, "\n");
    }
    
    fprintf(output, "Cache variables (%zu):\n", model->cache_variables.count);
    for (size_t i = 0; i < model->cache_variables.count; i++) {
        fprintf(output, "  "SV_Fmt" = "SV_Fmt"\n",
                SV_Arg(model->cache_variables.items[i].name),
                SV_Arg(model->cache_variables.items[i].value));
    }
    
    fprintf(output, "\nPackages found (%zu):\n", model->package_count);
    for (size_t i = 0; i < model->package_count; i++) {
        Found_Package *pkg = &model->found_packages[i];
        fprintf(output, "  "SV_Fmt": %s\n", 
                SV_Arg(pkg->name), pkg->found ? "YES" : "NO");
    }

    fprintf(output, "\nTests (%zu):\n", model->test_count);
    for (size_t i = 0; i < model->test_count; i++) {
        fprintf(output, "  "SV_Fmt" -> "SV_Fmt"\n",
                SV_Arg(model->tests[i].name),
                SV_Arg(model->tests[i].command));
    }
}

static void bm_install_rules_add_entry(Arena *arena, String_List *list,
                                       String_View item, String_View destination)
{
    if (!arena || !list || item.count == 0) return;

    // Formato interno: "item\tDEST"
    String_Builder sb = {0};
    nob_sb_append_buf(&sb, item.data, item.count);
    nob_sb_append(&sb, '\t');
    if (destination.count) nob_sb_append_buf(&sb, destination.data, destination.count);

    if (sb.count) {
        char *s = arena_strndup(arena, sb.items, sb.count);
        string_list_add(list, arena, sv_from_cstr(s));
    }
    nob_sb_free(sb);
}

void build_model_set_install_prefix(Build_Model *model, String_View prefix)
{
    if (!model) return;
    model->install_rules.prefix = prefix;
}

void build_model_add_install_rule(Build_Model *model, Arena *arena,
                                  Install_Rule_Type type,
                                  String_View item, String_View destination)
{
    if (!model || !arena) return;

    switch (type) {
        case INSTALL_RULE_TARGET:
            bm_install_rules_add_entry(arena, &model->install_rules.targets, item, destination);
            break;
        case INSTALL_RULE_FILE:
            bm_install_rules_add_entry(arena, &model->install_rules.files, item, destination);
            break;
        case INSTALL_RULE_PROGRAM:
            bm_install_rules_add_entry(arena, &model->install_rules.programs, item, destination);
            break;
        case INSTALL_RULE_DIRECTORY:
            bm_install_rules_add_entry(arena, &model->install_rules.directories, item, destination);
            break;
        default:
            break;
    }

    model->enable_install = true;
}

// ============================================================================
// Fase 1: APIs movidas do evaluator para o modelo
// ============================================================================

static String_View bm_sv_copy_to_arena(Arena *arena, String_View sv) {
    if (!arena) return sv;
    if (sv.count == 0) return sv_from_cstr("");
    const char *c = arena_strndup(arena, sv.data, sv.count);
    return sv_from_cstr(c);
}

static String_View bm_sv_trim_ws(String_View sv) {
    size_t b = 0;
    size_t e = sv.count;
    while (b < e && (sv.data[b] == ' ' || sv.data[b] == '\t' || sv.data[b] == '\n' || sv.data[b] == '\r')) b++;
    while (e > b && (sv.data[e - 1] == ' ' || sv.data[e - 1] == '\t' || sv.data[e - 1] == '\n' || sv.data[e - 1] == '\r')) e--;
    return nob_sv_from_parts(sv.data + b, e - b);
}

static bool bm_sv_eq_ci(String_View a, String_View b) {
    if (a.count != b.count) return false;
    for (size_t i = 0; i < a.count; i++) {
        unsigned char ca = (unsigned char)a.data[i];
        unsigned char cb = (unsigned char)b.data[i];
        if (toupper(ca) != toupper(cb)) return false;
    }
    return true;
}

static String_View bm_target_type_to_cmake_name(Target_Type type) {
    switch (type) {
        case TARGET_EXECUTABLE: return sv_from_cstr("EXECUTABLE");
        case TARGET_STATIC_LIB: return sv_from_cstr("STATIC_LIBRARY");
        case TARGET_SHARED_LIB: return sv_from_cstr("SHARED_LIBRARY");
        case TARGET_OBJECT_LIB: return sv_from_cstr("OBJECT_LIBRARY");
        case TARGET_INTERFACE_LIB: return sv_from_cstr("INTERFACE_LIBRARY");
        case TARGET_UTILITY: return sv_from_cstr("UTILITY");
        case TARGET_IMPORTED: return sv_from_cstr("UNKNOWN_LIBRARY");
        case TARGET_ALIAS: return sv_from_cstr("ALIAS");
        default: return sv_from_cstr("");
    }
}

static String_View bm_join_list_semicolon_temp(const String_List *list) {
    if (!list || list->count == 0) return sv_from_cstr("");
    String_Builder sb = {0};
    for (size_t i = 0; i < list->count; i++) {
        if (i > 0) nob_sb_append(&sb, ';');
        nob_sb_append_buf(&sb, list->items[i].data, list->items[i].count);
    }
    String_View out = sv_from_cstr(nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : ""));
    nob_sb_free(sb);
    return out;
}

static void bm_replace_list_from_semicolon(Arena *arena, String_List *list, String_View value) {
    if (!arena || !list) return;
    list->count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= value.count; i++) {
        bool sep = (i == value.count) || (value.data[i] == ';');
        if (!sep) continue;
        if (i > start) {
            String_View item = bm_sv_trim_ws(nob_sv_from_parts(value.data + start, i - start));
            if (item.count > 0) string_list_add(list, arena, item);
        }
        start = i + 1;
    }
}

static bool bm_parse_target_property_list_key(String_View key, Build_Config *out_cfg, String_List **out_list, Build_Target *target) {
    if (!out_cfg || !out_list || !target) return false;

    if (nob_sv_eq(key, sv_from_cstr("COMPILE_DEFINITIONS"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->properties[CONFIG_ALL].compile_definitions;
        return true;
    }
    if (nob_sv_eq(key, sv_from_cstr("COMPILE_OPTIONS"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->properties[CONFIG_ALL].compile_options;
        return true;
    }
    if (nob_sv_eq(key, sv_from_cstr("INCLUDE_DIRECTORIES"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->properties[CONFIG_ALL].include_directories;
        return true;
    }
    if (nob_sv_eq(key, sv_from_cstr("LINK_OPTIONS"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->properties[CONFIG_ALL].link_options;
        return true;
    }
    if (nob_sv_eq(key, sv_from_cstr("LINK_DIRECTORIES"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->properties[CONFIG_ALL].link_directories;
        return true;
    }

    String_View suffix = sv_from_cstr("");
    if (nob_sv_end_with(key, "_DEBUG")) suffix = sv_from_cstr("DEBUG");
    else if (nob_sv_end_with(key, "_RELEASE")) suffix = sv_from_cstr("RELEASE");
    else if (nob_sv_end_with(key, "_RELWITHDEBINFO")) suffix = sv_from_cstr("RELWITHDEBINFO");
    else if (nob_sv_end_with(key, "_MINSIZEREL")) suffix = sv_from_cstr("MINSIZEREL");
    else return false;

    Build_Config cfg = build_model_config_from_string(suffix);
    if (cfg == CONFIG_ALL) return false;

    if (nob_sv_starts_with(key, sv_from_cstr("COMPILE_DEFINITIONS_"))) {
        *out_cfg = cfg;
        *out_list = &target->properties[cfg].compile_definitions;
        return true;
    }
    if (nob_sv_starts_with(key, sv_from_cstr("COMPILE_OPTIONS_"))) {
        *out_cfg = cfg;
        *out_list = &target->properties[cfg].compile_options;
        return true;
    }
    if (nob_sv_starts_with(key, sv_from_cstr("INCLUDE_DIRECTORIES_"))) {
        *out_cfg = cfg;
        *out_list = &target->properties[cfg].include_directories;
        return true;
    }
    if (nob_sv_starts_with(key, sv_from_cstr("LINK_OPTIONS_"))) {
        *out_cfg = cfg;
        *out_list = &target->properties[cfg].link_options;
        return true;
    }
    if (nob_sv_starts_with(key, sv_from_cstr("LINK_DIRECTORIES_"))) {
        *out_cfg = cfg;
        *out_list = &target->properties[cfg].link_directories;
        return true;
    }

    return false;
}

static String_View bm_target_property_for_config(Build_Target *target, Build_Config cfg, const char *base_key, String_View fallback) {
    if (!target || !base_key) return fallback;
    String_View suffix = build_model_config_suffix(cfg);
    if (suffix.count > 0) {
        String_View cfg_key = sv_from_cstr(nob_temp_sprintf("%s_%s", base_key, nob_temp_sv_to_cstr(suffix)));
        String_View cfg_val = build_target_get_property(target, cfg_key);
        if (cfg_val.count > 0) return cfg_val;
    }
    String_View base = build_target_get_property(target, sv_from_cstr(base_key));
    if (base.count > 0) return base;
    return fallback;
}

void build_target_set_flag(Build_Target *target, Target_Flag flag, bool value) {
    if (!target) return;
    switch (flag) {
        case TARGET_FLAG_WIN32_EXECUTABLE: target->win32_executable = value; break;
        case TARGET_FLAG_MACOSX_BUNDLE:    target->macosx_bundle = value; break;
        case TARGET_FLAG_EXCLUDE_FROM_ALL: target->exclude_from_all = value; break;
        case TARGET_FLAG_IMPORTED:         target->imported = value; break;
        case TARGET_FLAG_ALIAS:            target->alias = value; break;
        default: break;
    }
}

void build_target_set_alias(Build_Target *target, Arena *arena, String_View aliased_name) {
    if (!target || !arena) return;
    build_target_set_flag(target, TARGET_FLAG_ALIAS, true);
    aliased_name = bm_sv_trim_ws(aliased_name);
    if (aliased_name.count > 0) {
        build_target_add_dependency(target, arena, aliased_name);
    }
}

void build_model_set_project_info(Build_Model *model, String_View name, String_View version) {
    if (!model) return;
    model->project_name = name;
    if (version.count > 0) model->project_version = version;
}

Build_Config build_model_config_from_string(String_View cfg) {
    if (bm_sv_eq_ci(cfg, sv_from_cstr("DEBUG"))) return CONFIG_DEBUG;
    if (bm_sv_eq_ci(cfg, sv_from_cstr("RELEASE"))) return CONFIG_RELEASE;
    if (bm_sv_eq_ci(cfg, sv_from_cstr("RELWITHDEBINFO"))) return CONFIG_RELWITHDEBINFO;
    if (bm_sv_eq_ci(cfg, sv_from_cstr("MINSIZEREL"))) return CONFIG_MINSIZEREL;
    return CONFIG_ALL;
}

String_View build_model_config_suffix(Build_Config cfg) {
    switch (cfg) {
        case CONFIG_DEBUG: return sv_from_cstr("DEBUG");
        case CONFIG_RELEASE: return sv_from_cstr("RELEASE");
        case CONFIG_RELWITHDEBINFO: return sv_from_cstr("RELWITHDEBINFO");
        case CONFIG_MINSIZEREL: return sv_from_cstr("MINSIZEREL");
        default: return sv_from_cstr("");
    }
}

void build_model_set_default_config(Build_Model *model, String_View config) {
    if (!model) return;
    Build_Config parsed = build_model_config_from_string(config);
    if (parsed == CONFIG_DEBUG) model->default_config = sv_from_cstr("Debug");
    else if (parsed == CONFIG_RELEASE) model->default_config = sv_from_cstr("Release");
    else if (parsed == CONFIG_RELWITHDEBINFO) model->default_config = sv_from_cstr("RelWithDebInfo");
    else if (parsed == CONFIG_MINSIZEREL) model->default_config = sv_from_cstr("MinSizeRel");
    else model->default_config = config;
}

void build_model_enable_language(Build_Model *model, Arena *arena, String_View lang) {
    if (!model || !arena) return;
    lang = bm_sv_trim_ws(lang);
    if (lang.count == 0) return;
    string_list_add_unique(&model->project_languages, arena, lang);
}

void build_model_set_testing_enabled(Build_Model *model, bool enabled) {
    if (!model) return;
    model->enable_testing = enabled;
}

void build_model_set_install_enabled(Build_Model *model, bool enabled) {
    if (!model) return;
    model->enable_install = enabled;
}

void build_model_set_env_var(Build_Model *model, Arena *arena, String_View key, String_View value) {
    if (!model || !arena) return;
    key = bm_sv_trim_ws(key);
    if (key.count == 0) return;

    String_View safe_key = bm_sv_copy_to_arena(arena, key);
    String_View safe_val = bm_sv_copy_to_arena(arena, value);

    for (size_t i = 0; i < model->environment_variables.count; i++) {
        if (nob_sv_eq(model->environment_variables.items[i].name, safe_key)) {
            model->environment_variables.items[i].value = safe_val;
            return;
        }
    }

    property_list_add(&model->environment_variables, arena, safe_key, safe_val);
}

void build_model_add_global_definition(Build_Model *model, Arena *arena, String_View def) {
    if (!model || !arena) return;
    def = bm_sv_trim_ws(def);
    if (def.count == 0) return;
    string_list_add_unique(&model->global_definitions, arena, def);
}

void build_model_add_global_compile_option(Build_Model *model, Arena *arena, String_View opt) {
    if (!model || !arena) return;
    opt = bm_sv_trim_ws(opt);
    if (opt.count == 0) return;
    string_list_add_unique(&model->global_compile_options, arena, opt);
}

void build_model_add_global_link_option(Build_Model *model, Arena *arena, String_View opt) {
    if (!model || !arena) return;
    opt = bm_sv_trim_ws(opt);
    if (opt.count == 0) return;
    string_list_add_unique(&model->global_link_options, arena, opt);
}

void build_model_process_global_definition_arg(Build_Model *model, Arena *arena, String_View arg) {
    if (!model || !arena) return;
    arg = bm_sv_trim_ws(arg);
    if (arg.count == 0) return;

    if (nob_sv_starts_with(arg, sv_from_cstr("-D")) || nob_sv_starts_with(arg, sv_from_cstr("/D"))) {
        if (arg.count <= 2) return;
        String_View def = nob_sv_from_parts(arg.data + 2, arg.count - 2);
        def = bm_sv_trim_ws(def);
        if (def.count == 0) return;
        build_model_add_global_definition(model, arena, def);
    } else {
        build_model_add_global_compile_option(model, arena, arg);
    }
}

void build_model_add_include_directory(Build_Model *model, Arena *arena, String_View dir, bool is_system) {
    if (!model || !arena) return;
    dir = bm_sv_trim_ws(dir);
    if (dir.count == 0) return;
    if (is_system) string_list_add_unique(&model->directories.system_include_dirs, arena, dir);
    else           string_list_add_unique(&model->directories.include_dirs, arena, dir);
}

void build_model_add_link_directory(Build_Model *model, Arena *arena, String_View dir) {
    if (!model || !arena) return;
    dir = bm_sv_trim_ws(dir);
    if (dir.count == 0) return;
    string_list_add_unique(&model->directories.link_dirs, arena, dir);
}

void build_model_add_global_link_library(Build_Model *model, Arena *arena, String_View lib) {
    if (!model || !arena) return;
    lib = bm_sv_trim_ws(lib);
    if (lib.count == 0) return;
    if (nob_sv_starts_with(lib, sv_from_cstr("-framework "))) {
        String_View fw = bm_sv_trim_ws(nob_sv_from_parts(lib.data + 11, lib.count - 11));
        string_list_add_unique(&model->global_link_libraries, arena, sv_from_cstr("-framework"));
        if (fw.count > 0) string_list_add_unique(&model->global_link_libraries, arena, fw);
        return;
    }
    string_list_add_unique(&model->global_link_libraries, arena, lib);
}

void build_model_remove_global_definition(Build_Model *model, String_View def) {
    if (!model) return;
    size_t out = 0;
    for (size_t i = 0; i < model->global_definitions.count; i++) {
        if (!nob_sv_eq(model->global_definitions.items[i], def)) {
            model->global_definitions.items[out++] = model->global_definitions.items[i];
        }
    }
    model->global_definitions.count = out;
}

void build_target_set_property_smart(Build_Target *target,
                                     Arena *arena,
                                     String_View key,
                                     String_View value) {
    if (!target || !arena) return;
    build_target_set_property(target, arena, key, value);

    if (nob_sv_eq(key, sv_from_cstr("OUTPUT_NAME"))) {
        target->output_name = value;
    } else if (nob_sv_eq(key, sv_from_cstr("PREFIX"))) {
        target->prefix = value;
    } else if (nob_sv_eq(key, sv_from_cstr("SUFFIX"))) {
        target->suffix = value;
    } else if (nob_sv_eq(key, sv_from_cstr("RUNTIME_OUTPUT_DIRECTORY"))) {
        target->runtime_output_directory = value;
    } else if (nob_sv_eq(key, sv_from_cstr("ARCHIVE_OUTPUT_DIRECTORY"))) {
        target->archive_output_directory = value;
    } else if (nob_sv_eq(key, sv_from_cstr("OUTPUT_DIRECTORY"))) {
        target->output_directory = value;
    }

    Build_Config cfg = CONFIG_ALL;
    String_List *list = NULL;
    if (bm_parse_target_property_list_key(key, &cfg, &list, target) && list) {
        bm_replace_list_from_semicolon(arena, list, value);
    }
}

String_View build_target_get_property_computed(Build_Target *target,
                                               String_View key,
                                               String_View default_config) {
    if (!target) return sv_from_cstr("");
    Build_Config active_cfg = build_model_config_from_string(default_config);

    if (bm_sv_eq_ci(key, sv_from_cstr("TYPE"))) {
        return bm_target_type_to_cmake_name(target->type);
    }
    if (bm_sv_eq_ci(key, sv_from_cstr("NAME"))) {
        return target->name;
    }
    if (bm_sv_eq_ci(key, sv_from_cstr("OUTPUT_NAME"))) {
        return bm_target_property_for_config(target, active_cfg, "OUTPUT_NAME",
            target->output_name.count > 0 ? target->output_name : target->name);
    }
    if (bm_sv_eq_ci(key, sv_from_cstr("OUTPUT_DIRECTORY"))) {
        return bm_target_property_for_config(target, active_cfg, "OUTPUT_DIRECTORY",
            target->output_directory.count > 0 ? target->output_directory : sv_from_cstr("build"));
    }
    if (bm_sv_eq_ci(key, sv_from_cstr("RUNTIME_OUTPUT_DIRECTORY"))) {
        return bm_target_property_for_config(target, active_cfg, "RUNTIME_OUTPUT_DIRECTORY",
            target->runtime_output_directory.count > 0 ? target->runtime_output_directory : sv_from_cstr(""));
    }
    if (bm_sv_eq_ci(key, sv_from_cstr("ARCHIVE_OUTPUT_DIRECTORY"))) {
        return bm_target_property_for_config(target, active_cfg, "ARCHIVE_OUTPUT_DIRECTORY",
            target->archive_output_directory.count > 0 ? target->archive_output_directory : sv_from_cstr(""));
    }
    if (bm_sv_eq_ci(key, sv_from_cstr("PREFIX"))) {
        return bm_target_property_for_config(target, active_cfg, "PREFIX", target->prefix);
    }
    if (bm_sv_eq_ci(key, sv_from_cstr("SUFFIX"))) {
        return bm_target_property_for_config(target, active_cfg, "SUFFIX", target->suffix);
    }
    if (bm_sv_eq_ci(key, sv_from_cstr("IMPORTED_LOCATION"))) {
        return bm_target_property_for_config(target, active_cfg, "IMPORTED_LOCATION", sv_from_cstr(""));
    }

    Build_Config cfg = CONFIG_ALL;
    String_List *list = NULL;
    if (bm_parse_target_property_list_key(key, &cfg, &list, target) && list) {
        return bm_join_list_semicolon_temp(list);
    }

    return bm_target_property_for_config(target, active_cfg, nob_temp_sv_to_cstr(key), sv_from_cstr(""));
}

Build_Test* build_model_add_test_ex(Build_Model *model,
                                    Arena *arena,
                                    String_View name,
                                    String_View command,
                                    String_View working_dir) {
    (void)arena;
    return build_model_add_test(model, name, command, working_dir, false);
}

CPack_Component_Group* build_model_get_or_create_cpack_group(Build_Model *model,
                                                             Arena *arena,
                                                             String_View name) {
    (void)arena;
    return build_model_add_cpack_component_group(model, name);
}

CPack_Component* build_model_get_or_create_cpack_component(Build_Model *model,
                                                           Arena *arena,
                                                           String_View name) {
    (void)arena;
    return build_model_add_cpack_component(model, name);
}

CPack_Install_Type* build_model_get_or_create_cpack_install_type(Build_Model *model,
                                                                  Arena *arena,
                                                                  String_View name) {
    (void)arena;
    return build_model_add_cpack_install_type(model, name);
}

void build_cpack_install_type_set_display_name(CPack_Install_Type *install_type,
                                               String_View display_name) {
    if (!install_type) return;
    install_type->display_name = display_name;
}

void build_cpack_group_set_display_name(CPack_Component_Group *group, String_View display_name) {
    if (!group) return;
    group->display_name = display_name;
}

void build_cpack_group_set_description(CPack_Component_Group *group, String_View description) {
    if (!group) return;
    group->description = description;
}

void build_cpack_group_set_parent_group(CPack_Component_Group *group, String_View parent_group) {
    if (!group) return;
    group->parent_group = parent_group;
}

void build_cpack_group_set_expanded(CPack_Component_Group *group, bool expanded) {
    if (!group) return;
    group->expanded = expanded;
}

void build_cpack_group_set_bold_title(CPack_Component_Group *group, bool bold_title) {
    if (!group) return;
    group->bold_title = bold_title;
}

void build_cpack_component_clear_dependencies(CPack_Component *component) {
    if (!component) return;
    component->depends.count = 0;
}

void build_cpack_component_clear_install_types(CPack_Component *component) {
    if (!component) return;
    component->install_types.count = 0;
}

void build_cpack_component_set_display_name(CPack_Component *component, String_View display_name) {
    if (!component) return;
    component->display_name = display_name;
}

void build_cpack_component_set_description(CPack_Component *component, String_View description) {
    if (!component) return;
    component->description = description;
}

void build_cpack_component_set_group(CPack_Component *component, String_View group) {
    if (!component) return;
    component->group = group;
}

void build_cpack_component_add_dependency(CPack_Component *component, Arena *arena, String_View dependency) {
    if (!component || !arena || dependency.count == 0) return;
    string_list_add_unique(&component->depends, arena, dependency);
}

void build_cpack_component_add_install_type(CPack_Component *component, Arena *arena, String_View install_type) {
    if (!component || !arena || install_type.count == 0) return;
    string_list_add_unique(&component->install_types, arena, install_type);
}

void build_cpack_component_set_required(CPack_Component *component, bool required) {
    if (!component) return;
    component->required = required;
}

void build_cpack_component_set_hidden(CPack_Component *component, bool hidden) {
    if (!component) return;
    component->hidden = hidden;
}

void build_cpack_component_set_disabled(CPack_Component *component, bool disabled) {
    if (!component) return;
    component->disabled = disabled;
}

void build_cpack_component_set_downloaded(CPack_Component *component, bool downloaded) {
    if (!component) return;
    component->downloaded = downloaded;
}

Custom_Command* build_target_add_custom_command_ex(Build_Target *target,
                                                   Arena *arena,
                                                   bool pre_build,
                                                   String_View command,
                                                   String_View working_dir,
                                                   String_View comment) {
    if (!target || !arena || command.count == 0) return NULL;

    Custom_Command *list = pre_build ? target->pre_build_commands : target->post_build_commands;
    size_t *count = pre_build ? &target->pre_build_count : &target->post_build_count;
    size_t *capacity = pre_build ? &target->pre_build_capacity : &target->post_build_capacity;
    if (!arena_da_reserve(arena, (void**)&list, capacity, sizeof(*list), *count + 1)) return NULL;

    if (pre_build) target->pre_build_commands = list;
    else target->post_build_commands = list;
    list = pre_build ? target->pre_build_commands : target->post_build_commands;

    Custom_Command *cmd = &list[*count];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = CUSTOM_COMMAND_SHELL;
    cmd->command = command;
    cmd->working_dir = working_dir;
    cmd->comment = comment;
    cmd->echo = true;
    string_list_init(&cmd->outputs);
    string_list_init(&cmd->byproducts);
    string_list_init(&cmd->inputs);
    string_list_init(&cmd->depends);
    (*count)++;
    return cmd;
}

Custom_Command* build_target_add_custom_command(Build_Target *target,
                                                Arena *arena,
                                                bool pre_build,
                                                String_View command) {
    return build_target_add_custom_command_ex(target, arena, pre_build, command, sv_from_cstr(""), sv_from_cstr(""));
}

Custom_Command* build_model_add_custom_command_output_ex(Build_Model *model,
                                                         Arena *arena,
                                                         String_View command,
                                                         String_View working_dir,
                                                         String_View comment) {
    if (!model || !arena || command.count == 0) return NULL;
    if (!arena_da_reserve(arena, (void**)&model->output_custom_commands, &model->output_custom_command_capacity,
            sizeof(*model->output_custom_commands), model->output_custom_command_count + 1)) {
        return NULL;
    }
    Custom_Command *cmd = &model->output_custom_commands[model->output_custom_command_count++];
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = CUSTOM_COMMAND_SHELL;
    cmd->command = command;
    cmd->working_dir = working_dir;
    cmd->comment = comment;
    cmd->echo = true;
    string_list_init(&cmd->outputs);
    string_list_init(&cmd->byproducts);
    string_list_init(&cmd->inputs);
    string_list_init(&cmd->depends);
    return cmd;
}

Custom_Command* build_model_add_custom_command_output(Build_Model *model,
                                                      Arena *arena,
                                                      String_View output,
                                                      String_View command) {
    Custom_Command *cmd = build_model_add_custom_command_output_ex(model, arena, command, sv_from_cstr(""), sv_from_cstr(""));
    if (!cmd) return NULL;
    if (output.count > 0) string_list_add_unique(&cmd->outputs, arena, output);
    return cmd;
}

bool build_path_is_absolute(String_View path) {
    if (path.count == 0) return false;
    if (path.count >= 1 && (path.data[0] == '/' || path.data[0] == '\\')) return true;
    if (path.count >= 2 && path.data[1] == ':') return true;
    return false;
}

String_View build_path_join(Arena *arena, String_View base, String_View rel) {
    if (!arena) return sv_from_cstr("");
    if (build_path_is_absolute(rel)) return rel;
    String_Builder sb = {0};
    nob_sb_append_buf(&sb, base.data, base.count);
    if (sb.count > 0 && sb.items[sb.count - 1] != '/' && sb.items[sb.count - 1] != '\\') {
        nob_sb_append(&sb, '/');
    }
    nob_sb_append_buf(&sb, rel.data, rel.count);
    String_View out = sv_from_cstr(arena_strndup(arena, sb.items ? sb.items : "", sb.count));
    nob_sb_free(sb);
    return out;
}

String_View build_path_parent_dir(Arena *arena, String_View full_path) {
    if (!arena || full_path.count == 0) return sv_from_cstr(".");
    size_t cut = full_path.count;
    while (cut > 0) {
        char c = full_path.data[cut - 1];
        if (c == '/' || c == '\\') break;
        cut--;
    }
    if (cut == 0) return sv_from_cstr(".");
    return sv_from_cstr(arena_strndup(arena, full_path.data, cut - 1));
}

String_View build_path_make_absolute(Arena *arena, String_View path) {
    if (!arena) return path;
    if (build_path_is_absolute(path)) return bm_sv_copy_to_arena(arena, path);
    return build_path_join(arena, sv_from_cstr(nob_get_current_dir_temp()), path);
}
