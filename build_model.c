#include "build_model.h"
#include "arena_dyn.h"

static Build_Target* build_model_find_target_const(const Build_Model *model, String_View name) {
    if (!model) return NULL;
    for (size_t i = 0; i < model->target_count; i++) {
        if (nob_sv_eq(model->targets[i].name, name)) {
            return (Build_Target*)&model->targets[i];
        }
    }
    return NULL;
}

int build_model_find_target_index(const Build_Model *model, String_View name) {
    if (!model) return -1;
    for (size_t i = 0; i < model->target_count; i++) {
        if (nob_sv_eq(model->targets[i].name, name)) {
            return (int)i;
        }
    }
    return -1;
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
    // Verifica se já existe
    for (size_t i = 0; i < model->target_count; i++) {
        if (nob_sv_eq(model->targets[i].name, name)) {
            if (model->targets[i].type != type) {
                nob_log(NOB_ERROR, "Target '"SV_Fmt"' redefinido com tipo diferente", SV_Arg(name));
                return NULL;
            }
            return &model->targets[i];
        }
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
    
    target->name = name;
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
