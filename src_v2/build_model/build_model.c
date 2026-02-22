#include "build_model.h"
#include "arena_dyn.h"
#include "ds_adapter.h"
#include "genex.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const String_List g_empty_string_list = {0};
static String_View bm_sv_copy_to_arena(Arena *arena, String_View sv);
String_View build_path_join(Arena *arena, String_View base, String_View rel);
static bool bm_sv_eq_ci(String_View a, String_View b);

struct Bm_Genex_Warn_Entry {
    char *key;
    int value;
};

void build_model_clear_genex_warn_cache(Build_Model *model) {
    if (!model || !model->genex_warn_cache) return;
    for (size_t i = 0; i < (size_t)ds_shlen(model->genex_warn_cache); i++) {
        free(model->genex_warn_cache[i].key);
    }
    ds_shfree(model->genex_warn_cache);
    model->genex_warn_cache = NULL;
}

static void build_model_heap_cleanup(void *userdata) {
    Build_Model *model = (Build_Model*)userdata;
    if (!model) return;
    for (size_t i = 0; i < model->target_count; i++) {
        if (model->targets[i]) {
            ds_shfree(model->targets[i]->custom_property_index);
        }
    }
    ds_shfree(model->target_index_by_name);
    ds_shfree(model->cache_variable_index);
    ds_shfree(model->environment_variable_index);
    build_model_clear_genex_warn_cache(model);
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

static int build_model_lookup_property_index(const Property_List *list, Build_Property_Index_Entry *index_map, String_View key) {
    if (!list || !index_map) return -1;
    Build_Property_Index_Entry *entry = ds_shgetp_null(index_map, nob_temp_sv_to_cstr(key));
    if (!entry) return -1;
    if (entry->value < 0 || (size_t)entry->value >= list->count) return -1;
    if (!nob_sv_eq(list->items[entry->value].name, key)) return -1;
    return entry->value;
}

static int build_model_lookup_property_index_linear(const Property_List *list, String_View key) {
    if (!list) return -1;
    for (size_t i = 0; i < list->count; i++) {
        if (nob_sv_eq(list->items[i].name, key)) return (int)i;
    }
    return -1;
}

static bool build_model_put_property_index(Build_Property_Index_Entry **index_map, char *key, int value) {
    if (!index_map || !key || key[0] == '\0' || value < 0) return false;
    Build_Property_Index_Entry *map = *index_map;
    ds_shput(map, key, value);
    *index_map = map;
    return true;
}

static char *build_model_copy_sv_cstr(Arena *arena, String_View sv) {
    if (!arena || !sv.data || sv.count == 0) return NULL;
    return arena_strndup(arena, sv.data, sv.count);
}

static void build_model_rebuild_property_index(Arena *arena, Property_List *list, Build_Property_Index_Entry **index_map) {
    if (!arena || !list || !index_map) return;
    ds_shfree(*index_map);
    *index_map = NULL;
    for (size_t i = 0; i < list->count; i++) {
        char *stable_key = build_model_copy_sv_cstr(arena, list->items[i].name);
        if (!stable_key) continue;
        list->items[i].name = sv_from_cstr(stable_key);
        if (!build_model_put_property_index(index_map, stable_key, (int)i)) {
            nob_log(NOB_WARNING, "failed to rebuild property index for key '%s'", stable_key);
        }
    }
}

static Build_Target* build_model_find_target_const(const Build_Model *model, String_View name) {
    if (!model) return NULL;
    int idx = build_model_lookup_target_index(model, name);
    if (idx >= 0 && (size_t)idx < model->target_count) return model->targets[idx];

    if (model->is_windows) {
        // Keep target-name resolution aligned with Windows case-insensitive behavior.
        for (size_t i = 0; i < model->target_count; i++) {
            Build_Target *t = model->targets[i];
            if (t && bm_sv_eq_ci(t->name, name)) return t;
        }
    }
    return NULL;
}

int build_model_find_target_index(const Build_Model *model, String_View name) {
    return build_model_lookup_target_index(model, name);
}

static bool build_model_has_cycle_dfs(const Build_Model *model, size_t idx, uint8_t *state) {
    if (state[idx] == 1) return true;
    if (state[idx] == 2) return false;

    state[idx] = 1;
    const Build_Target *target = model->targets[idx];

    for (size_t j = 0; j < target->dependencies.count; j++) {
        int dep_idx = build_model_find_target_index(model, target->dependencies.items[j]);
        if (dep_idx < 0) continue;
        if (build_model_has_cycle_dfs(model, (size_t)dep_idx, state)) {
            return true;
        }
    }
    for (size_t j = 0; j < target->object_dependencies.count; j++) {
        int dep_idx = build_model_find_target_index(model, target->object_dependencies.items[j]);
        if (dep_idx < 0) continue;
        if (build_model_has_cycle_dfs(model, (size_t)dep_idx, state)) {
            return true;
        }
    }

    state[idx] = 2;
    return false;
}

// ============================================================================
// FUNCOES AUXILIARES
// ============================================================================

void string_list_init(String_List *list) {
    if (!list) return;
    memset(list, 0, sizeof(*list));
}

void string_list_add(String_List *list, Arena *arena, String_View item) {
    if (!list || !arena) return;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity, sizeof(*list->items), list->count + 1)) return;
    list->items[list->count++] = bm_sv_copy_to_arena(arena, item);
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
    list->items[list->count].name = bm_sv_copy_to_arena(arena, key);
    list->items[list->count].value = bm_sv_copy_to_arena(arena, value);
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

static void conditional_property_list_init(Conditional_Property_List *list) {
    if (!list) return;
    memset(list, 0, sizeof(*list));
}

static bool conditional_property_list_add(Conditional_Property_List *list,
                                          Arena *arena,
                                          String_View value,
                                          Logic_Node *condition) {
    if (!list || !arena || value.count == 0 || !value.data) return false;
    if (!arena_da_reserve(arena, (void**)&list->items, &list->capacity,
                          sizeof(*list->items), list->count + 1)) {
        return false;
    }
    list->items[list->count].value = value;
    list->items[list->count].condition = condition;
    list->count++;
    return true;
}

static String_View bm_config_to_cmake_build_type(Build_Config config) {
    switch (config) {
        case CONFIG_DEBUG: return sv_from_cstr("Debug");
        case CONFIG_RELEASE: return sv_from_cstr("Release");
        case CONFIG_RELWITHDEBINFO: return sv_from_cstr("RelWithDebInfo");
        case CONFIG_MINSIZEREL: return sv_from_cstr("MinSizeRel");
        default: return sv_from_cstr("");
    }
}

static Logic_Node *bm_condition_for_config(Arena *arena, Build_Config config) {
    if (!arena || config == CONFIG_ALL) return NULL;
    String_View cfg_value = bm_config_to_cmake_build_type(config);
    if (cfg_value.count == 0) return NULL;
    Logic_Operand lhs = {
        .token = sv_from_cstr("CMAKE_BUILD_TYPE"),
        .quoted = false,
    };
    Logic_Operand rhs = {
        .token = cfg_value,
        .quoted = true,
    };
    return logic_compare(arena, LOGIC_CMP_STREQUAL, lhs, rhs);
}

static String_View bm_trim_ws_simple(String_View sv) {
    size_t b = 0;
    size_t e = sv.count;
    while (b < e && isspace((unsigned char)sv.data[b])) b++;
    while (e > b && isspace((unsigned char)sv.data[e - 1])) e--;
    return nob_sv_from_parts(sv.data + b, e - b);
}

static bool bm_condition_extract_config(const Logic_Node *condition, Build_Config *out_cfg) {
    if (!condition || !out_cfg) return false;
    if (condition->type != LOGIC_OP_COMPARE) return false;
    if (condition->as.cmp.op != LOGIC_CMP_STREQUAL) return false;

    Logic_Operand lhs = condition->as.cmp.lhs;
    Logic_Operand rhs = condition->as.cmp.rhs;
    if (lhs.quoted) return false;
    if (!nob_sv_eq(lhs.token, sv_from_cstr("CMAKE_BUILD_TYPE"))) return false;

    Build_Config cfg = build_model_config_from_string(rhs.token);
    if (cfg == CONFIG_ALL) return false;
    *out_cfg = cfg;
    return true;
}

static void bm_conditional_list_clear_for_config(Conditional_Property_List *list, Build_Config cfg) {
    if (!list) return;
    size_t out = 0;
    for (size_t i = 0; i < list->count; i++) {
        Conditional_Property item = list->items[i];
        bool remove = false;
        if (cfg == CONFIG_ALL) {
            remove = (item.condition == NULL);
        } else {
            Build_Config item_cfg = CONFIG_ALL;
            if (bm_condition_extract_config(item.condition, &item_cfg) && item_cfg == cfg) {
                remove = true;
            }
        }
        if (!remove) {
            list->items[out++] = item;
        }
    }
    list->count = out;
}

static void bm_conditional_list_append_from_semicolon(Conditional_Property_List *list,
                                                      Arena *arena,
                                                      String_View value,
                                                      Build_Config cfg) {
    if (!list || !arena) return;
    Logic_Node *condition = bm_condition_for_config(arena, cfg);
    size_t start = 0;
    for (size_t i = 0; i <= value.count; i++) {
        bool sep = (i == value.count) || (value.data[i] == ';');
        if (!sep) continue;
        if (i > start) {
            String_View item = bm_trim_ws_simple(nob_sv_from_parts(value.data + start, i - start));
            if (item.count > 0) {
                conditional_property_list_add(list, arena, item, condition);
            }
        }
        start = i + 1;
    }
}

static String_View bm_join_conditional_list_by_config_temp(const Conditional_Property_List *list, Build_Config cfg) {
    if (!list || list->count == 0) return sv_from_cstr("");
    String_Builder sb = {0};
    bool first = true;
    for (size_t i = 0; i < list->count; i++) {
        Conditional_Property item = list->items[i];
        bool include = false;
        if (cfg == CONFIG_ALL) {
            include = (item.condition == NULL);
        } else {
            Build_Config item_cfg = CONFIG_ALL;
            include = bm_condition_extract_config(item.condition, &item_cfg) && (item_cfg == cfg);
        }
        if (!include) continue;
        if (!first) nob_sb_append(&sb, ';');
        first = false;
        nob_sb_append_buf(&sb, item.value.data, item.value.count);
    }
    String_View out = sv_from_cstr(nob_temp_sprintf("%.*s", (int)sb.count, sb.items ? sb.items : ""));
    nob_sb_free(sb);
    return out;
}

static void bm_collect_effective_conditional(const Conditional_Property_List *list,
                                             Arena *arena,
                                             const Logic_Eval_Context *logic_ctx,
                                             String_List *out) {
    if (!list || !arena || !out) return;
    for (size_t i = 0; i < list->count; i++) {
        Conditional_Property item = list->items[i];
        if (item.value.count == 0 || !item.value.data) continue;
        bool take = false;
        if (!item.condition) {
            take = true;
        } else if (logic_ctx) {
            take = logic_evaluate(item.condition, logic_ctx);
        } else {
            // Contexto ausente: comportamento conservador para condicoes explicitas.
            take = false;
        }
        if (take) {
            string_list_add_unique(out, arena, item.value);
        }
    }
}

// ============================================================================
// IMPLEMENTACAO PUBLICA
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
    
    // Configuracoes padrao
    model->default_config = sv_from_cstr("Debug");
    model->config_debug = true;
    model->config_release = true;
    
    // Padroes de linguagem
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

    Build_Directory_Node *root = build_model_add_directory_node(model,
                                                                 arena,
                                                                 sv_from_cstr(""),
                                                                 sv_from_cstr(""),
                                                                 -1);
    if (!root) return NULL;
    model->root_directory_index = root->index;

    return model;
}

Build_Target* build_model_add_target(Build_Model *model, 
                                     String_View name, 
                                     Target_Type type) {
    if (!model) return NULL;
    // Verifica se ja existe
    int existing_idx = build_model_lookup_target_index(model, name);
    if (existing_idx >= 0 && (size_t)existing_idx < model->target_count) {
        Build_Target *existing = model->targets[existing_idx];
        if (existing->type != type) {
            nob_log(NOB_ERROR, "Target '"SV_Fmt"' redefinido com tipo diferente", SV_Arg(name));
            return NULL;
        }
        return existing;
    }
    
    // Expande array se necessario
    if (model->target_count >= model->target_capacity) {
        if (!arena_da_reserve(model->arena, (void**)&model->targets, &model->target_capacity,
                sizeof(*model->targets), model->target_count + 1)) {
            return NULL;
        }
    }

    Build_Target *target = arena_alloc_zero(model->arena, sizeof(Build_Target));
    if (!target) return NULL;
    model->targets[model->target_count++] = target;
    
    char *name_copy = arena_strndup(model->arena, name.data, name.count);
    if (!name_copy) {
        model->target_count--;
        model->targets[model->target_count] = NULL;
        nob_log(NOB_ERROR, "Falha ao copiar nome do target '"SV_Fmt"'", SV_Arg(name));
        return NULL;
    }
    target->name = sv_from_cstr(name_copy);
    target->type = type;
    target->owner_model = model;
    target->owner_directory_index = model->root_directory_index;
    
    // Inicializa listas
    string_list_init(&target->sources);
    string_list_init(&target->source_groups);
    string_list_init(&target->dependencies);
    string_list_init(&target->object_dependencies);
    string_list_init(&target->interface_dependencies);
    string_list_init(&target->link_libraries);
    string_list_init(&target->interface_libs);
    string_list_init(&target->interface_compile_definitions);
    string_list_init(&target->interface_compile_options);
    string_list_init(&target->interface_include_directories);
    string_list_init(&target->interface_link_options);
    string_list_init(&target->interface_link_directories);
    property_list_init(&target->custom_properties);
    conditional_property_list_init(&target->conditional_compile_definitions);
    conditional_property_list_init(&target->conditional_compile_options);
    conditional_property_list_init(&target->conditional_include_directories);
    conditional_property_list_init(&target->conditional_link_libraries);
    conditional_property_list_init(&target->conditional_link_options);
    conditional_property_list_init(&target->conditional_link_directories);
    
    // Prefixos e sufixos padrao
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

Build_Directory_Node* build_model_add_directory_node(Build_Model *model,
                                                     Arena *arena,
                                                     String_View source_dir,
                                                     String_View binary_dir,
                                                     int parent_index) {
    if (!model || !arena) return NULL;

    for (size_t i = 0; i < model->directory_node_count; i++) {
        Build_Directory_Node *node = &model->directory_nodes[i];
        if (node->parent_index != parent_index) continue;
        if (!nob_sv_eq(node->source_dir, source_dir)) continue;
        if (!nob_sv_eq(node->binary_dir, binary_dir)) continue;
        return node;
    }

    if (!arena_da_reserve(arena,
                          (void**)&model->directory_nodes,
                          &model->directory_node_capacity,
                          sizeof(*model->directory_nodes),
                          model->directory_node_count + 1)) {
        return NULL;
    }

    Build_Directory_Node *node = &model->directory_nodes[model->directory_node_count];
    memset(node, 0, sizeof(*node));
    node->index = model->directory_node_count;
    node->parent_index = parent_index;
    node->source_dir = bm_sv_copy_to_arena(arena, source_dir);
    node->binary_dir = bm_sv_copy_to_arena(arena, binary_dir);
    string_list_init(&node->include_dirs);
    string_list_init(&node->system_include_dirs);
    string_list_init(&node->link_dirs);

    model->directory_node_count++;
    return node;
}

const Build_Directory_Node* build_model_get_directory_node(const Build_Model *model, size_t index) {
    if (!model || index >= model->directory_node_count) return NULL;
    return &model->directory_nodes[index];
}

size_t build_model_get_directory_count(const Build_Model *model) {
    return model ? model->directory_node_count : 0;
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

void build_target_add_object_dependency(Build_Target *target, Arena *arena, String_View dep_target_name) {
    if (!target || !arena) return;
    for (size_t i = 0; i < target->object_dependencies.count; i++) {
        if (nob_sv_eq(target->object_dependencies.items[i], dep_target_name)) {
            return;
        }
    }
    string_list_add(&target->object_dependencies, arena, dep_target_name);
}

void build_target_add_definition(Build_Target *target, 
                                 Arena *arena,
                                 String_View definition, 
                                 Visibility visibility,
                                 Build_Config config) {
    if (!target || !arena || config > CONFIG_ALL) return;
    Logic_Node *condition = bm_condition_for_config(arena, config);
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        build_target_add_conditional_compile_definition(target, arena, definition, condition);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_compile_definitions, arena, definition);
    }
}

void build_target_add_definition_private(Build_Target *target, Arena *arena, String_View definition) {
    build_target_add_definition(target, arena, definition, VISIBILITY_PRIVATE, CONFIG_ALL);
}

void build_target_add_definition_public(Build_Target *target, Arena *arena, String_View definition) {
    build_target_add_definition(target, arena, definition, VISIBILITY_PUBLIC, CONFIG_ALL);
}

void build_target_add_definition_interface(Build_Target *target, Arena *arena, String_View definition) {
    build_target_add_definition(target, arena, definition, VISIBILITY_INTERFACE, CONFIG_ALL);
}

void build_target_add_include_directory(Build_Target *target,
                                        Arena *arena,
                                        String_View directory,
                                        Visibility visibility,
                                        Build_Config config) {
    if (!target || !arena || config > CONFIG_ALL) return;
    Logic_Node *condition = bm_condition_for_config(arena, config);
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        build_target_add_conditional_include_directory(target, arena, directory, condition);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_include_directories, arena, directory);
    }
}

void build_target_add_include_directory_private(Build_Target *target, Arena *arena, String_View directory) {
    build_target_add_include_directory(target, arena, directory, VISIBILITY_PRIVATE, CONFIG_ALL);
}

void build_target_add_include_directory_public(Build_Target *target, Arena *arena, String_View directory) {
    build_target_add_include_directory(target, arena, directory, VISIBILITY_PUBLIC, CONFIG_ALL);
}

void build_target_add_include_directory_interface(Build_Target *target, Arena *arena, String_View directory) {
    build_target_add_include_directory(target, arena, directory, VISIBILITY_INTERFACE, CONFIG_ALL);
}


void build_target_add_library(Build_Target *target,
                              Arena *arena,
                              String_View library,
                              Visibility visibility) {
    if (!target || !arena) return;
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->link_libraries, arena, library);
        build_target_add_conditional_link_library(target, arena, library, NULL);
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
    Logic_Node *condition = bm_condition_for_config(arena, config);
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        build_target_add_conditional_compile_option(target, arena, option, condition);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_compile_options, arena, option);
    }
}

void build_target_add_compile_option_private(Build_Target *target, Arena *arena, String_View option) {
    build_target_add_compile_option(target, arena, option, VISIBILITY_PRIVATE, CONFIG_ALL);
}

void build_target_add_compile_option_public(Build_Target *target, Arena *arena, String_View option) {
    build_target_add_compile_option(target, arena, option, VISIBILITY_PUBLIC, CONFIG_ALL);
}

void build_target_add_compile_option_interface(Build_Target *target, Arena *arena, String_View option) {
    build_target_add_compile_option(target, arena, option, VISIBILITY_INTERFACE, CONFIG_ALL);
}

void build_target_add_conditional_compile_definition(Build_Target *target,
                                                     Arena *arena,
                                                     String_View definition,
                                                     Logic_Node *condition) {
    if (!target || !arena) return;
    conditional_property_list_add(&target->conditional_compile_definitions, arena, definition, condition);
}

void build_target_add_conditional_compile_option(Build_Target *target,
                                                 Arena *arena,
                                                 String_View option,
                                                 Logic_Node *condition) {
    if (!target || !arena) return;
    conditional_property_list_add(&target->conditional_compile_options, arena, option, condition);
}

void build_target_add_conditional_include_directory(Build_Target *target,
                                                    Arena *arena,
                                                    String_View directory,
                                                    Logic_Node *condition) {
    if (!target || !arena) return;
    conditional_property_list_add(&target->conditional_include_directories, arena, directory, condition);
}

void build_target_add_conditional_link_library(Build_Target *target,
                                               Arena *arena,
                                               String_View library,
                                               Logic_Node *condition) {
    if (!target || !arena) return;
    conditional_property_list_add(&target->conditional_link_libraries, arena, library, condition);
}

void build_target_add_conditional_link_option(Build_Target *target,
                                              Arena *arena,
                                              String_View option,
                                              Logic_Node *condition) {
    if (!target || !arena) return;
    conditional_property_list_add(&target->conditional_link_options, arena, option, condition);
}

void build_target_add_conditional_link_directory(Build_Target *target,
                                                 Arena *arena,
                                                 String_View directory,
                                                 Logic_Node *condition) {
    if (!target || !arena) return;
    conditional_property_list_add(&target->conditional_link_directories, arena, directory, condition);
}

void build_target_collect_effective_compile_definitions(Build_Target *target,
                                                        Arena *arena,
                                                        const Logic_Eval_Context *logic_ctx,
                                                        String_List *out) {
    if (!target) return;
    bm_collect_effective_conditional(&target->conditional_compile_definitions, arena, logic_ctx, out);
}

void build_target_collect_effective_compile_options(Build_Target *target,
                                                    Arena *arena,
                                                    const Logic_Eval_Context *logic_ctx,
                                                    String_List *out) {
    if (!target) return;
    bm_collect_effective_conditional(&target->conditional_compile_options, arena, logic_ctx, out);
}

void build_target_collect_effective_include_directories(Build_Target *target,
                                                        Arena *arena,
                                                        const Logic_Eval_Context *logic_ctx,
                                                        String_List *out) {
    if (!target) return;
    bm_collect_effective_conditional(&target->conditional_include_directories, arena, logic_ctx, out);
}

void build_target_collect_effective_link_libraries(Build_Target *target,
                                                   Arena *arena,
                                                   const Logic_Eval_Context *logic_ctx,
                                                   String_List *out) {
    if (!target) return;
    bm_collect_effective_conditional(&target->conditional_link_libraries, arena, logic_ctx, out);
}

void build_target_collect_effective_link_options(Build_Target *target,
                                                 Arena *arena,
                                                 const Logic_Eval_Context *logic_ctx,
                                                 String_List *out) {
    if (!target) return;
    bm_collect_effective_conditional(&target->conditional_link_options, arena, logic_ctx, out);
}

void build_target_collect_effective_link_directories(Build_Target *target,
                                                     Arena *arena,
                                                     const Logic_Eval_Context *logic_ctx,
                                                     String_List *out) {
    if (!target) return;
    bm_collect_effective_conditional(&target->conditional_link_directories, arena, logic_ctx, out);
}

void build_target_add_link_option(Build_Target *target,
                                  Arena *arena,
                                  String_View option,
                                  Visibility visibility,
                                  Build_Config config) {
    if (!target || !arena || config > CONFIG_ALL) return;
    Logic_Node *condition = bm_condition_for_config(arena, config);
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        build_target_add_conditional_link_option(target, arena, option, condition);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_link_options, arena, option);
    }
}

void build_target_add_link_option_private(Build_Target *target, Arena *arena, String_View option) {
    build_target_add_link_option(target, arena, option, VISIBILITY_PRIVATE, CONFIG_ALL);
}

void build_target_add_link_option_public(Build_Target *target, Arena *arena, String_View option) {
    build_target_add_link_option(target, arena, option, VISIBILITY_PUBLIC, CONFIG_ALL);
}

void build_target_add_link_option_interface(Build_Target *target, Arena *arena, String_View option) {
    build_target_add_link_option(target, arena, option, VISIBILITY_INTERFACE, CONFIG_ALL);
}

void build_target_add_link_directory(Build_Target *target,
                                     Arena *arena,
                                     String_View directory,
                                     Visibility visibility,
                                     Build_Config config) {
    if (!target || !arena || config > CONFIG_ALL) return;
    Logic_Node *condition = bm_condition_for_config(arena, config);
    if (visibility == VISIBILITY_PRIVATE || visibility == VISIBILITY_PUBLIC) {
        build_target_add_conditional_link_directory(target, arena, directory, condition);
    }
    if (visibility == VISIBILITY_INTERFACE || visibility == VISIBILITY_PUBLIC) {
        string_list_add(&target->interface_link_directories, arena, directory);
    }
}

void build_target_add_link_directory_private(Build_Target *target, Arena *arena, String_View directory) {
    build_target_add_link_directory(target, arena, directory, VISIBILITY_PRIVATE, CONFIG_ALL);
}

void build_target_add_link_directory_public(Build_Target *target, Arena *arena, String_View directory) {
    build_target_add_link_directory(target, arena, directory, VISIBILITY_PUBLIC, CONFIG_ALL);
}

void build_target_add_link_directory_interface(Build_Target *target, Arena *arena, String_View directory) {
    build_target_add_link_directory(target, arena, directory, VISIBILITY_INTERFACE, CONFIG_ALL);
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
    int idx = build_model_lookup_property_index(&target->custom_properties, target->custom_property_index, key);
    if (idx < 0) {
        idx = build_model_lookup_property_index_linear(&target->custom_properties, key);
        if (idx >= 0) {
            build_model_rebuild_property_index(arena, &target->custom_properties, &target->custom_property_index);
        }
    }

    if (idx >= 0) {
        target->custom_properties.items[idx].value = bm_sv_copy_to_arena(arena, value);
        return;
    }

    char *stable_key = build_model_copy_sv_cstr(arena, key);
    if (!stable_key) return;
    String_View stable_key_sv = sv_from_cstr(stable_key);
    size_t before = target->custom_properties.count;
    property_list_add(&target->custom_properties, arena, stable_key_sv, value);
    if (target->custom_properties.count == before + 1) {
        if (!build_model_put_property_index(&target->custom_property_index, stable_key, (int)before)) {
            nob_log(NOB_WARNING, "failed to index target property '%s'", stable_key);
        }
    }
}

String_View build_target_get_property(Build_Target *target, String_View key) {
    if (!target) return sv_from_cstr("");
    int idx = build_model_lookup_property_index(&target->custom_properties, target->custom_property_index, key);
    if (idx >= 0) return target->custom_properties.items[idx].value;

    idx = build_model_lookup_property_index_linear(&target->custom_properties, key);
    if (idx >= 0) return target->custom_properties.items[idx].value;
    return sv_from_cstr("");
}

Found_Package* build_model_add_package(Build_Model *model,
                                       String_View name,
                                       bool found) {
    if (!model) return NULL;
    // Verifica se ja existe
    for (size_t i = 0; i < model->package_count; i++) {
        if (nob_sv_eq(model->found_packages[i].name, name)) {
            return &model->found_packages[i];
        }
    }
    
    // Expande array se necessario
    if (model->package_count >= model->package_capacity) {
        if (!arena_da_reserve(model->arena, (void**)&model->found_packages, &model->package_capacity,
                sizeof(*model->found_packages), model->package_count + 1)) {
            return NULL;
        }
    }
    
    Found_Package *pkg = &model->found_packages[model->package_count++];
    memset(pkg, 0, sizeof(Found_Package));
    
    pkg->name = bm_sv_copy_to_arena(model->arena, name);
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
            model->tests[i].command = bm_sv_copy_to_arena(model->arena, command);
            model->tests[i].working_directory = bm_sv_copy_to_arena(model->arena, working_directory);
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
    test->name = bm_sv_copy_to_arena(model->arena, name);
    test->command = bm_sv_copy_to_arena(model->arena, command);
    test->working_directory = bm_sv_copy_to_arena(model->arena, working_directory);
    test->command_expand_lists = command_expand_lists;
    return test;
}

Build_Test* build_model_find_test_by_name(Build_Model *model, String_View test_name) {
    if (!model || test_name.count == 0) return NULL;
    for (size_t i = 0; i < model->test_count; i++) {
        if (nob_sv_eq(model->tests[i].name, test_name)) return &model->tests[i];
    }
    return NULL;
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
    group->name = bm_sv_copy_to_arena(model->arena, name);
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
    install_type->name = bm_sv_copy_to_arena(model->arena, name);
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
    component->name = bm_sv_copy_to_arena(model->arena, name);
    string_list_init(&component->depends);
    string_list_init(&component->install_types);
    return component;
}

void build_model_set_cache_variable(Build_Model *model,
                                    String_View key,
                                    String_View value,
                                    String_View type,
                                    String_View docstring) {
    if (!model || !model->arena) return;
    (void)type;
    (void)docstring;

    int idx = build_model_lookup_property_index(&model->cache_variables, model->cache_variable_index, key);
    if (idx < 0) {
        idx = build_model_lookup_property_index_linear(&model->cache_variables, key);
        if (idx >= 0) {
            build_model_rebuild_property_index(model->arena, &model->cache_variables, &model->cache_variable_index);
        }
    }
    if (idx >= 0) {
        model->cache_variables.items[idx].value = bm_sv_copy_to_arena(model->arena, value);
        return;
    }

    char *stable_key = build_model_copy_sv_cstr(model->arena, key);
    if (!stable_key) return;
    String_View stable_key_sv = sv_from_cstr(stable_key);

    size_t before = model->cache_variables.count;
    property_list_add(&model->cache_variables, model->arena, stable_key_sv, value);
    if (model->cache_variables.count == before + 1) {
        if (!build_model_put_property_index(&model->cache_variable_index, stable_key, (int)before)) {
            nob_log(NOB_WARNING, "failed to index cache variable '%s'", stable_key);
        }
    }
}

String_View build_model_get_cache_variable(Build_Model *model, String_View key) {
    if (!model) return sv_from_cstr("");

    int idx = build_model_lookup_property_index(&model->cache_variables, model->cache_variable_index, key);
    if (idx >= 0) return model->cache_variables.items[idx].value;

    idx = build_model_lookup_property_index_linear(&model->cache_variables, key);
    if (idx >= 0) return model->cache_variables.items[idx].value;
    return sv_from_cstr("");
}

bool build_model_has_cache_variable(const Build_Model *model, String_View key) {
    if (!model) return false;

    int idx = build_model_lookup_property_index(&model->cache_variables, model->cache_variable_index, key);
    if (idx >= 0) return true;

    return build_model_lookup_property_index_linear(&model->cache_variables, key) >= 0;
}

bool build_model_unset_cache_variable(Build_Model *model, String_View key) {
    if (!model) return false;

    int idx = build_model_lookup_property_index(&model->cache_variables, model->cache_variable_index, key);
    if (idx < 0) {
        idx = build_model_lookup_property_index_linear(&model->cache_variables, key);
    }
    if (idx < 0) return false;

    for (size_t i = (size_t)idx + 1; i < model->cache_variables.count; i++) {
        model->cache_variables.items[i - 1] = model->cache_variables.items[i];
    }
    model->cache_variables.count--;
    build_model_rebuild_property_index(model->arena, &model->cache_variables, &model->cache_variable_index);
    return true;
}

String_View build_model_get_env_var(const Build_Model *model, String_View key) {
    if (!model) return sv_from_cstr("");

    int idx = build_model_lookup_property_index(&model->environment_variables, model->environment_variable_index, key);
    if (idx >= 0) return model->environment_variables.items[idx].value;

    idx = build_model_lookup_property_index_linear(&model->environment_variables, key);
    if (idx >= 0) return model->environment_variables.items[idx].value;
    return sv_from_cstr("");
}

bool build_model_has_env_var(const Build_Model *model, String_View key) {
    if (!model) return false;

    int idx = build_model_lookup_property_index(&model->environment_variables, model->environment_variable_index, key);
    if (idx >= 0) return true;

    return build_model_lookup_property_index_linear(&model->environment_variables, key) >= 0;
}

bool build_model_unset_env_var(Build_Model *model, String_View key) {
    if (!model) return false;

    int idx = build_model_lookup_property_index(&model->environment_variables, model->environment_variable_index, key);
    if (idx < 0) {
        idx = build_model_lookup_property_index_linear(&model->environment_variables, key);
    }
    if (idx < 0) return false;

    for (size_t i = (size_t)idx + 1; i < model->environment_variables.count; i++) {
        model->environment_variables.items[i - 1] = model->environment_variables.items[i];
    }
    model->environment_variables.count--;
    build_model_rebuild_property_index(model->arena, &model->environment_variables, &model->environment_variable_index);
    return true;
}

bool build_model_validate_dependencies(Build_Model *model) {
    if (!model) return false;

    for (size_t i = 0; i < model->target_count; i++) {
        Build_Target *target = model->targets[i];
        for (size_t j = 0; j < target->dependencies.count; j++) {
            String_View dep_name = target->dependencies.items[j];
            if (!build_model_find_target(model, dep_name)) {
                return false;
            }
        }
        for (size_t j = 0; j < target->object_dependencies.count; j++) {
            String_View dep_name = target->object_dependencies.items[j];
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
        Build_Target *target = model->targets[i];
        for (size_t j = 0; j < target->dependencies.count; j++) {
            int dep_idx = build_model_find_target_index(model, target->dependencies.items[j]);
            if (dep_idx >= 0) {
                in_degree[i]++;
            }
        }
        for (size_t j = 0; j < target->object_dependencies.count; j++) {
            int dep_idx = build_model_find_target_index(model, target->object_dependencies.items[j]);
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
        Build_Target *current = model->targets[current_idx];
        sorted[sorted_count++] = current;

        for (size_t i = 0; i < n; i++) {
            if (in_degree[i] == 0) continue;

            Build_Target *other = model->targets[i];
            for (size_t j = 0; j < other->dependencies.count; j++) {
                if (nob_sv_eq(other->dependencies.items[j], current->name)) {
                    in_degree[i]--;
                    if (in_degree[i] == 0) {
                        queue[q_back++] = i;
                    }
                    break;
                }
            }
            if (in_degree[i] == 0) continue;
            for (size_t j = 0; j < other->object_dependencies.count; j++) {
                if (nob_sv_eq(other->object_dependencies.items[j], current->name)) {
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
        Build_Target *t = model->targets[i];
        
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
        fprintf(output, "    Object dependencies: %zu\n", t->object_dependencies.count);
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
    model->install_rules.prefix = bm_sv_copy_to_arena(model->arena, prefix);
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
    if (!sv.data || sv.count == 0) return sv_from_cstr("");
    const char *c = arena_strndup(arena, sv.data, sv.count);
    return sv_from_cstr(c);
}

String_View build_model_copy_string(Arena *arena, String_View value) {
    return bm_sv_copy_to_arena(arena, value);
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

static bool bm_sv_contains_genex(String_View sv) {
    for (size_t i = 0; i + 1 < sv.count; i++) {
        if (sv.data[i] == '$' && sv.data[i + 1] == '<') return true;
    }
    return false;
}

static bool bm_genex_warn_should_emit(Build_Model *model,
                                      String_View target_name,
                                      String_View property_name,
                                      String_View raw_value) {
    String_Builder sb = {0};
    nob_sb_append_buf(&sb, target_name.data, target_name.count);
    nob_sb_append(&sb, '\x1f');
    nob_sb_append_buf(&sb, property_name.data, property_name.count);
    nob_sb_append(&sb, '\x1f');
    nob_sb_append_buf(&sb, raw_value.data, raw_value.count);

    if (sb.count == 0) {
        nob_sb_free(sb);
        return false;
    }

    char *probe = (char*)malloc(sb.count + 1);
    if (!probe) {
        nob_sb_free(sb);
        return true;
    }
    memcpy(probe, sb.items, sb.count);
    probe[sb.count] = '\0';
    nob_sb_free(sb);

    if (!model) {
        free(probe);
        return true;
    }

    Bm_Genex_Warn_Entry *entry = ds_shgetp_null(model->genex_warn_cache, probe);
    if (entry) {
        free(probe);
        return false;
    }
    ds_shput(model->genex_warn_cache, probe, 1);
    return true;
}

static void bm_genex_warn_keep_raw(Build_Target *target,
                                   String_View key,
                                   String_View raw_value,
                                   Genex_Status status,
                                   String_View diag_message) {
    if (!target || key.count == 0 || raw_value.count == 0) return;
    Build_Model *model = (Build_Model*)target->owner_model;
    if (!bm_genex_warn_should_emit(model, target->name, key, raw_value)) return;

    const char *status_text = "ERROR";
    if (status == GENEX_UNSUPPORTED) status_text = "UNSUPPORTED";
    else if (status == GENEX_CYCLE_GUARD_HIT) status_text = "CYCLE_GUARD_HIT";
    else if (status == GENEX_OK) status_text = "OK";

    nob_log(NOB_WARNING,
            "genex: target '"SV_Fmt"', property '"SV_Fmt"' -> %s, preserving raw value '"SV_Fmt"' (%.*s)",
            SV_Arg(target->name),
            SV_Arg(key),
            status_text,
            SV_Arg(raw_value),
            (int)diag_message.count,
            diag_message.data ? diag_message.data : "");
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

static bool bm_parse_target_conditional_property_key(String_View key,
                                                     Build_Config *out_cfg,
                                                     Conditional_Property_List **out_list,
                                                     Build_Target *target) {
    if (!out_cfg || !out_list || !target) return false;

    if (nob_sv_eq(key, sv_from_cstr("COMPILE_DEFINITIONS"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->conditional_compile_definitions;
        return true;
    }
    if (nob_sv_eq(key, sv_from_cstr("COMPILE_OPTIONS"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->conditional_compile_options;
        return true;
    }
    if (nob_sv_eq(key, sv_from_cstr("INCLUDE_DIRECTORIES"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->conditional_include_directories;
        return true;
    }
    if (nob_sv_eq(key, sv_from_cstr("LINK_OPTIONS"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->conditional_link_options;
        return true;
    }
    if (nob_sv_eq(key, sv_from_cstr("LINK_DIRECTORIES"))) {
        *out_cfg = CONFIG_ALL;
        *out_list = &target->conditional_link_directories;
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
        *out_list = &target->conditional_compile_definitions;
        return true;
    }
    if (nob_sv_starts_with(key, sv_from_cstr("COMPILE_OPTIONS_"))) {
        *out_cfg = cfg;
        *out_list = &target->conditional_compile_options;
        return true;
    }
    if (nob_sv_starts_with(key, sv_from_cstr("INCLUDE_DIRECTORIES_"))) {
        *out_cfg = cfg;
        *out_list = &target->conditional_include_directories;
        return true;
    }
    if (nob_sv_starts_with(key, sv_from_cstr("LINK_OPTIONS_"))) {
        *out_cfg = cfg;
        *out_list = &target->conditional_link_options;
        return true;
    }
    if (nob_sv_starts_with(key, sv_from_cstr("LINK_DIRECTORIES_"))) {
        *out_cfg = cfg;
        *out_list = &target->conditional_link_directories;
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

typedef struct {
    Build_Model *model;
    Build_Config config;
} Bm_Genex_Target_Property_Ctx;

static String_View bm_target_get_property_raw_for_config(Build_Target *target, String_View key, Build_Config active_cfg);

static String_View bm_genex_target_property_read(void *userdata,
                                                 String_View target_name,
                                                 String_View property_name) {
    if (!userdata || target_name.count == 0 || property_name.count == 0) return sv_from_cstr("");
    Bm_Genex_Target_Property_Ctx *ctx = (Bm_Genex_Target_Property_Ctx*)userdata;
    if (!ctx->model) return sv_from_cstr("");

    Build_Target *target = build_model_find_target(ctx->model, target_name);
    if (!target) return sv_from_cstr("");
    return bm_target_get_property_raw_for_config(target, property_name, ctx->config);
}

static String_View bm_genex_target_file_read(void *userdata, String_View target_name) {
    if (!userdata || target_name.count == 0) return sv_from_cstr("");
    Bm_Genex_Target_Property_Ctx *ctx = (Bm_Genex_Target_Property_Ctx*)userdata;
    if (!ctx->model || !ctx->model->arena) return sv_from_cstr("");

    Build_Target *target = build_model_find_target(ctx->model, target_name);
    if (!target) return sv_from_cstr("");

    String_View out_name = bm_target_property_for_config(
        target, ctx->config, "OUTPUT_NAME",
        target->output_name.count > 0 ? target->output_name : target->name);
    String_View prefix = bm_target_property_for_config(target, ctx->config, "PREFIX", target->prefix);
    String_View suffix = bm_target_property_for_config(target, ctx->config, "SUFFIX", target->suffix);

    String_View out_dir = sv_from_cstr("");
    if (target->type == TARGET_EXECUTABLE || target->type == TARGET_SHARED_LIB) {
        out_dir = bm_target_property_for_config(target, ctx->config, "RUNTIME_OUTPUT_DIRECTORY",
                                                target->runtime_output_directory.count > 0
                                                    ? target->runtime_output_directory
                                                    : target->output_directory);
    } else {
        out_dir = bm_target_property_for_config(target, ctx->config, "ARCHIVE_OUTPUT_DIRECTORY",
                                                target->archive_output_directory.count > 0
                                                    ? target->archive_output_directory
                                                    : target->output_directory);
    }
    if (out_dir.count == 0) out_dir = sv_from_cstr("build");

    String_Builder name_sb = {0};
    if (prefix.count > 0) nob_sb_append_buf(&name_sb, prefix.data, prefix.count);
    if (out_name.count > 0) nob_sb_append_buf(&name_sb, out_name.data, out_name.count);
    if (suffix.count > 0) nob_sb_append_buf(&name_sb, suffix.data, suffix.count);
    String_View file_name = name_sb.count > 0
        ? sv_from_cstr(arena_strndup(ctx->model->arena, name_sb.items, name_sb.count))
        : sv_from_cstr("");
    nob_sb_free(name_sb);

    if (out_dir.count == 0) return file_name;
    return build_path_join(ctx->model->arena, out_dir, file_name);
}

static String_View bm_genex_target_linker_file_read(void *userdata, String_View target_name) {
    if (!userdata || target_name.count == 0) return sv_from_cstr("");
    Bm_Genex_Target_Property_Ctx *ctx = (Bm_Genex_Target_Property_Ctx*)userdata;
    if (!ctx->model || !ctx->model->arena) return sv_from_cstr("");

    Build_Target *target = build_model_find_target(ctx->model, target_name);
    if (!target) return sv_from_cstr("");

    String_View out_name = bm_target_property_for_config(
        target, ctx->config, "OUTPUT_NAME",
        target->output_name.count > 0 ? target->output_name : target->name);
    String_View prefix = bm_target_property_for_config(target, ctx->config, "PREFIX", target->prefix);
    String_View suffix = bm_target_property_for_config(target, ctx->config, "SUFFIX", target->suffix);

    String_View out_dir = bm_target_property_for_config(
        target, ctx->config, "ARCHIVE_OUTPUT_DIRECTORY",
        target->archive_output_directory.count > 0 ? target->archive_output_directory : target->output_directory);
    if (out_dir.count == 0) out_dir = sv_from_cstr("build");

    String_Builder name_sb = {0};
    if (prefix.count > 0) nob_sb_append_buf(&name_sb, prefix.data, prefix.count);
    if (out_name.count > 0) nob_sb_append_buf(&name_sb, out_name.data, out_name.count);
    if (suffix.count > 0) nob_sb_append_buf(&name_sb, suffix.data, suffix.count);
    String_View file_name = name_sb.count > 0
        ? sv_from_cstr(arena_strndup(ctx->model->arena, name_sb.items, name_sb.count))
        : sv_from_cstr("");
    nob_sb_free(name_sb);

    if (out_dir.count == 0) return file_name;
    return build_path_join(ctx->model->arena, out_dir, file_name);
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
    model->project_name = bm_sv_copy_to_arena(model->arena, name);
    if (version.count > 0) model->project_version = bm_sv_copy_to_arena(model->arena, version);
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
    else model->default_config = bm_sv_copy_to_arena(model->arena, config);
}

String_View build_model_get_default_config(const Build_Model *model) {
    if (!model) return sv_from_cstr("");
    return model->default_config;
}

Arena* build_model_get_arena(Build_Model *model) {
    if (!model) return NULL;
    return model->arena;
}

bool build_model_is_windows(const Build_Model *model) {
    return model ? model->is_windows : false;
}

bool build_model_is_unix(const Build_Model *model) {
    return model ? model->is_unix : false;
}

bool build_model_is_apple(const Build_Model *model) {
    return model ? model->is_apple : false;
}

bool build_model_is_linux(const Build_Model *model) {
    return model ? model->is_linux : false;
}

String_View build_model_get_system_name(const Build_Model *model) {
    if (!model) return sv_from_cstr("");
    if (model->is_windows) return sv_from_cstr("Windows");
    if (model->is_apple) return sv_from_cstr("Darwin");
    if (model->is_linux) return sv_from_cstr("Linux");
    if (model->is_unix) return sv_from_cstr("Unix");
    return sv_from_cstr("");
}

String_View build_model_get_project_name(const Build_Model *model) {
    if (!model) return sv_from_cstr("");
    return model->project_name;
}

String_View build_model_get_project_version(const Build_Model *model) {
    if (!model) return sv_from_cstr("");
    return model->project_version;
}

const String_List* build_model_get_string_list(const Build_Model *model, Build_Model_List_Kind kind) {
    if (!model) return &g_empty_string_list;
    switch (kind) {
        case BUILD_MODEL_LIST_INCLUDE_DIRS: return &model->directories.include_dirs;
        case BUILD_MODEL_LIST_SYSTEM_INCLUDE_DIRS: return &model->directories.system_include_dirs;
        case BUILD_MODEL_LIST_LINK_DIRS: return &model->directories.link_dirs;
        case BUILD_MODEL_LIST_GLOBAL_DEFINITIONS: return &model->global_definitions;
        case BUILD_MODEL_LIST_GLOBAL_COMPILE_OPTIONS: return &model->global_compile_options;
        case BUILD_MODEL_LIST_GLOBAL_LINK_OPTIONS: return &model->global_link_options;
        case BUILD_MODEL_LIST_GLOBAL_LINK_LIBRARIES: return &model->global_link_libraries;
        default: return &g_empty_string_list;
    }
}

const String_List* build_model_get_install_rule_list(const Build_Model *model, Install_Rule_Type type) {
    if (!model) return &g_empty_string_list;
    switch (type) {
        case INSTALL_RULE_TARGET: return &model->install_rules.targets;
        case INSTALL_RULE_FILE: return &model->install_rules.files;
        case INSTALL_RULE_PROGRAM: return &model->install_rules.programs;
        case INSTALL_RULE_DIRECTORY: return &model->install_rules.directories;
        default: return &g_empty_string_list;
    }
}

size_t build_model_get_cache_variable_count(const Build_Model *model) {
    return model ? model->cache_variables.count : 0;
}

String_View build_model_get_cache_variable_name_at(const Build_Model *model, size_t index) {
    if (!model || index >= model->cache_variables.count) return sv_from_cstr("");
    return model->cache_variables.items[index].name;
}

size_t build_model_get_target_count(const Build_Model *model) {
    return model ? model->target_count : 0;
}

Build_Target* build_model_get_target_at(Build_Model *model, size_t index) {
    if (!model || index >= model->target_count) return NULL;
    return model->targets[index];
}

String_View build_model_get_install_prefix(const Build_Model *model) {
    if (!model) return sv_from_cstr("");
    return model->install_rules.prefix;
}

bool build_model_has_install_prefix(const Build_Model *model) {
    return model && model->install_rules.prefix.count > 0;
}

bool build_model_is_testing_enabled(const Build_Model *model) {
    return model ? model->enable_testing : false;
}

size_t build_model_get_test_count(const Build_Model *model) {
    return model ? model->test_count : 0;
}

Build_Test* build_model_get_test_at(Build_Model *model, size_t index) {
    if (!model || index >= model->test_count) return NULL;
    return &model->tests[index];
}

size_t build_model_get_cpack_install_type_count(const Build_Model *model) {
    return model ? model->cpack_install_type_count : 0;
}

CPack_Install_Type* build_model_get_cpack_install_type_at(Build_Model *model, size_t index) {
    if (!model || index >= model->cpack_install_type_count) return NULL;
    return &model->cpack_install_types[index];
}

size_t build_model_get_cpack_component_group_count(const Build_Model *model) {
    return model ? model->cpack_component_group_count : 0;
}

CPack_Component_Group* build_model_get_cpack_component_group_at(Build_Model *model, size_t index) {
    if (!model || index >= model->cpack_component_group_count) return NULL;
    return &model->cpack_component_groups[index];
}

size_t build_model_get_cpack_component_count(const Build_Model *model) {
    return model ? model->cpack_component_count : 0;
}

CPack_Component* build_model_get_cpack_component_at(Build_Model *model, size_t index) {
    if (!model || index >= model->cpack_component_count) return NULL;
    return &model->cpack_components[index];
}

const Custom_Command* build_model_get_output_custom_commands(const Build_Model *model, size_t *out_count) {
    if (out_count) *out_count = 0;
    if (!model || !model->output_custom_commands) return NULL;
    if (out_count) *out_count = model->output_custom_command_count;
    return model->output_custom_commands;
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

    int idx = build_model_lookup_property_index(&model->environment_variables, model->environment_variable_index, safe_key);
    if (idx < 0) {
        idx = build_model_lookup_property_index_linear(&model->environment_variables, safe_key);
        if (idx >= 0) {
            build_model_rebuild_property_index(arena, &model->environment_variables, &model->environment_variable_index);
        }
    }
    if (idx >= 0) {
        model->environment_variables.items[idx].value = safe_val;
        return;
    }

    size_t before = model->environment_variables.count;
    property_list_add(&model->environment_variables, arena, safe_key, safe_val);
    if (model->environment_variables.count == before + 1) {
        if (!build_model_put_property_index(&model->environment_variable_index, (char*)safe_key.data, (int)before)) {
            nob_log(NOB_WARNING, "failed to index environment variable '"SV_Fmt"'", SV_Arg(safe_key));
        }
    }
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
    size_t directory_index = model ? model->root_directory_index : 0;
    build_model_add_include_directory_scoped(model, arena, directory_index, dir, is_system);
}

void build_model_add_include_directory_scoped(Build_Model *model,
                                              Arena *arena,
                                              size_t directory_index,
                                              String_View dir,
                                              bool is_system) {
    if (!model || !arena) return;
    dir = bm_sv_trim_ws(dir);
    if (dir.count == 0) return;
    if (is_system) string_list_add_unique(&model->directories.system_include_dirs, arena, dir);
    else           string_list_add_unique(&model->directories.include_dirs, arena, dir);

    if (directory_index >= model->directory_node_count) return;
    Build_Directory_Node *node = &model->directory_nodes[directory_index];
    if (is_system) string_list_add_unique(&node->system_include_dirs, arena, dir);
    else           string_list_add_unique(&node->include_dirs, arena, dir);
}

void build_model_add_link_directory(Build_Model *model, Arena *arena, String_View dir) {
    size_t directory_index = model ? model->root_directory_index : 0;
    build_model_add_link_directory_scoped(model, arena, directory_index, dir);
}

void build_model_add_link_directory_scoped(Build_Model *model,
                                           Arena *arena,
                                           size_t directory_index,
                                           String_View dir) {
    if (!model || !arena) return;
    dir = bm_sv_trim_ws(dir);
    if (dir.count == 0) return;
    string_list_add_unique(&model->directories.link_dirs, arena, dir);

    if (directory_index >= model->directory_node_count) return;
    Build_Directory_Node *node = &model->directory_nodes[directory_index];
    string_list_add_unique(&node->link_dirs, arena, dir);
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
        target->output_name = bm_sv_copy_to_arena(arena, value);
    } else if (nob_sv_eq(key, sv_from_cstr("PREFIX"))) {
        target->prefix = bm_sv_copy_to_arena(arena, value);
    } else if (nob_sv_eq(key, sv_from_cstr("SUFFIX"))) {
        target->suffix = bm_sv_copy_to_arena(arena, value);
    } else if (nob_sv_eq(key, sv_from_cstr("RUNTIME_OUTPUT_DIRECTORY"))) {
        target->runtime_output_directory = bm_sv_copy_to_arena(arena, value);
    } else if (nob_sv_eq(key, sv_from_cstr("ARCHIVE_OUTPUT_DIRECTORY"))) {
        target->archive_output_directory = bm_sv_copy_to_arena(arena, value);
    } else if (nob_sv_eq(key, sv_from_cstr("OUTPUT_DIRECTORY"))) {
        target->output_directory = bm_sv_copy_to_arena(arena, value);
    }

    Build_Config cfg = CONFIG_ALL;
    Conditional_Property_List *list = NULL;
    if (bm_parse_target_conditional_property_key(key, &cfg, &list, target) && list) {
        bm_conditional_list_clear_for_config(list, cfg);
        bm_conditional_list_append_from_semicolon(list, arena, value, cfg);
    }
}

static String_View bm_target_get_property_raw_for_config(Build_Target *target,
                                                         String_View key,
                                                         Build_Config active_cfg) {
    if (!target) return sv_from_cstr("");
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
    Conditional_Property_List *list = NULL;
    if (bm_parse_target_conditional_property_key(key, &cfg, &list, target) && list) {
        return bm_join_conditional_list_by_config_temp(list, cfg);
    }

    return bm_target_property_for_config(target, active_cfg, nob_temp_sv_to_cstr(key), sv_from_cstr(""));
}

String_View build_target_get_property_computed(Build_Target *target,
                                               String_View key,
                                               String_View default_config) {
    if (!target) return sv_from_cstr("");
    Build_Config active_cfg = build_model_config_from_string(default_config);
    String_View raw = bm_target_get_property_raw_for_config(target, key, active_cfg);
    if (raw.count == 0) return raw;
    if (!bm_sv_contains_genex(raw)) return raw;

    Build_Model *model = (Build_Model*)target->owner_model;
    if (!model) return raw;

    String_View config_name = bm_config_to_cmake_build_type(active_cfg);
    if (config_name.count == 0) config_name = default_config;

    Bm_Genex_Target_Property_Ctx cb = {
        .model = model,
        .config = active_cfg,
    };
    Genex_Context gx = {
        .arena = model->arena,
        .config = config_name,
        .current_target_name = target->name,
        .platform_id = build_model_get_system_name(model),
        .compile_language = sv_from_cstr(""),
        .read_target_property = bm_genex_target_property_read,
        .read_target_file = bm_genex_target_file_read,
        .read_target_linker_file = bm_genex_target_linker_file_read,
        .userdata = &cb,
        .link_only_active = true,
        .build_interface_active = true,
        .install_interface_active = false,
        .target_name_case_insensitive = model->is_windows,
        .max_callback_value_len = 1024 * 1024,
        .max_depth = 64,
        .max_target_property_depth = 32,
    };

    Genex_Result evaluated = genex_eval(&gx, raw);
    if (evaluated.status == GENEX_OK) {
        return evaluated.value;
    }

    bm_genex_warn_keep_raw(target, key, raw, evaluated.status, evaluated.diag_message);
    return raw;
}

String_View build_target_get_name(const Build_Target *target) {
    if (!target) return sv_from_cstr("");
    return target->name;
}

Target_Type build_target_get_type(const Build_Target *target) {
    if (!target) return (Target_Type)0;
    return target->type;
}

bool build_target_has_source(const Build_Target *target, String_View source) {
    if (!target || source.count == 0) return false;
    for (size_t i = 0; i < target->sources.count; i++) {
        if (nob_sv_eq(target->sources.items[i], source)) return true;
    }
    return false;
}

const String_List* build_target_get_string_list(const Build_Target *target, Build_Target_List_Kind kind) {
    if (!target) return &g_empty_string_list;
    switch (kind) {
        case BUILD_TARGET_LIST_SOURCES: return &target->sources;
        case BUILD_TARGET_LIST_DEPENDENCIES: return &target->dependencies;
        case BUILD_TARGET_LIST_OBJECT_DEPENDENCIES: return &target->object_dependencies;
        case BUILD_TARGET_LIST_INTERFACE_DEPENDENCIES: return &target->interface_dependencies;
        case BUILD_TARGET_LIST_INTERFACE_LIBS: return &target->interface_libs;
        case BUILD_TARGET_LIST_INTERFACE_COMPILE_DEFINITIONS: return &target->interface_compile_definitions;
        case BUILD_TARGET_LIST_INTERFACE_COMPILE_OPTIONS: return &target->interface_compile_options;
        case BUILD_TARGET_LIST_INTERFACE_INCLUDE_DIRECTORIES: return &target->interface_include_directories;
        case BUILD_TARGET_LIST_INTERFACE_LINK_OPTIONS: return &target->interface_link_options;
        case BUILD_TARGET_LIST_INTERFACE_LINK_DIRECTORIES: return &target->interface_link_directories;
        default: return &g_empty_string_list;
    }
}

void build_target_reset_derived_property(Build_Target *target, Build_Target_Derived_Property_Kind kind) {
    if (!target) return;
    switch (kind) {
        case BUILD_TARGET_DERIVED_INTERFACE_COMPILE_DEFINITIONS:
            target->interface_compile_definitions.count = 0;
            break;
        case BUILD_TARGET_DERIVED_INTERFACE_COMPILE_OPTIONS:
            target->interface_compile_options.count = 0;
            break;
        case BUILD_TARGET_DERIVED_INTERFACE_INCLUDE_DIRECTORIES:
            target->interface_include_directories.count = 0;
            break;
        case BUILD_TARGET_DERIVED_INTERFACE_LINK_OPTIONS:
            target->interface_link_options.count = 0;
            break;
        case BUILD_TARGET_DERIVED_INTERFACE_LINK_DIRECTORIES:
            target->interface_link_directories.count = 0;
            break;
        case BUILD_TARGET_DERIVED_INTERFACE_LINK_LIBRARIES:
            target->interface_dependencies.count = 0;
            target->interface_libs.count = 0;
            break;
        default:
            break;
    }
}

bool build_target_is_exclude_from_all(const Build_Target *target) {
    return target ? target->exclude_from_all : false;
}

const Custom_Command* build_target_get_custom_commands(const Build_Target *target, bool pre_build, size_t *out_count) {
    if (out_count) *out_count = 0;
    if (!target) return NULL;
    if (pre_build) {
        if (out_count) *out_count = target->pre_build_count;
        return target->pre_build_commands;
    }
    if (out_count) *out_count = target->post_build_count;
    return target->post_build_commands;
}

String_View build_test_get_name(const Build_Test *test) {
    if (!test) return sv_from_cstr("");
    return test->name;
}

String_View build_test_get_command(const Build_Test *test) {
    if (!test) return sv_from_cstr("");
    return test->command;
}

String_View build_test_get_working_directory(const Build_Test *test) {
    if (!test) return sv_from_cstr("");
    return test->working_directory;
}

bool build_test_get_command_expand_lists(const Build_Test *test) {
    return test ? test->command_expand_lists : false;
}

void build_test_set_command_expand_lists(Build_Test *test, bool value) {
    if (!test) return;
    test->command_expand_lists = value;
}

String_View build_cpack_install_type_get_name(const CPack_Install_Type *install_type) {
    if (!install_type) return sv_from_cstr("");
    return install_type->name;
}

String_View build_cpack_install_type_get_display_name(const CPack_Install_Type *install_type) {
    if (!install_type) return sv_from_cstr("");
    return install_type->display_name;
}

String_View build_cpack_group_get_name(const CPack_Component_Group *group) {
    if (!group) return sv_from_cstr("");
    return group->name;
}

String_View build_cpack_group_get_display_name(const CPack_Component_Group *group) {
    if (!group) return sv_from_cstr("");
    return group->display_name;
}

String_View build_cpack_group_get_description(const CPack_Component_Group *group) {
    if (!group) return sv_from_cstr("");
    return group->description;
}

String_View build_cpack_group_get_parent_group(const CPack_Component_Group *group) {
    if (!group) return sv_from_cstr("");
    return group->parent_group;
}

bool build_cpack_group_get_expanded(const CPack_Component_Group *group) {
    return group ? group->expanded : false;
}

bool build_cpack_group_get_bold_title(const CPack_Component_Group *group) {
    return group ? group->bold_title : false;
}

String_View build_cpack_component_get_name(const CPack_Component *component) {
    if (!component) return sv_from_cstr("");
    return component->name;
}

String_View build_cpack_component_get_display_name(const CPack_Component *component) {
    if (!component) return sv_from_cstr("");
    return component->display_name;
}

String_View build_cpack_component_get_description(const CPack_Component *component) {
    if (!component) return sv_from_cstr("");
    return component->description;
}

String_View build_cpack_component_get_group(const CPack_Component *component) {
    if (!component) return sv_from_cstr("");
    return component->group;
}

const String_List* build_cpack_component_get_depends(const CPack_Component *component) {
    if (!component) return &g_empty_string_list;
    return &component->depends;
}

const String_List* build_cpack_component_get_install_types(const CPack_Component *component) {
    if (!component) return &g_empty_string_list;
    return &component->install_types;
}

bool build_cpack_component_get_required(const CPack_Component *component) {
    return component ? component->required : false;
}

bool build_cpack_component_get_hidden(const CPack_Component *component) {
    return component ? component->hidden : false;
}

bool build_cpack_component_get_disabled(const CPack_Component *component) {
    return component ? component->disabled : false;
}

bool build_cpack_component_get_downloaded(const CPack_Component *component) {
    return component ? component->downloaded : false;
}

Build_Test* build_model_add_test_ex(Build_Model *model,
                                    Arena *arena,
                                    String_View name,
                                    String_View command,
                                    String_View working_dir) {
    (void)arena;
    return build_model_add_test(model, name, command, working_dir, false);
}

CPack_Component_Group* build_model_ensure_cpack_group(Build_Model *model, String_View name) {
    return build_model_add_cpack_component_group(model, name);
}

CPack_Component* build_model_ensure_cpack_component(Build_Model *model, String_View name) {
    return build_model_add_cpack_component(model, name);
}

CPack_Install_Type* build_model_ensure_cpack_install_type(Build_Model *model, String_View name) {
    return build_model_add_cpack_install_type(model, name);
}

CPack_Component_Group* build_model_get_or_create_cpack_group(Build_Model *model,
                                                             Arena *arena,
                                                             String_View name) {
    (void)arena;
    return build_model_ensure_cpack_group(model, name);
}

CPack_Component* build_model_get_or_create_cpack_component(Build_Model *model,
                                                           Arena *arena,
                                                           String_View name) {
    (void)arena;
    return build_model_ensure_cpack_component(model, name);
}

CPack_Install_Type* build_model_get_or_create_cpack_install_type(Build_Model *model,
                                                                  Arena *arena,
                                                                  String_View name) {
    (void)arena;
    return build_model_ensure_cpack_install_type(model, name);
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
    cmd->command = bm_sv_copy_to_arena(arena, command);
    cmd->working_dir = bm_sv_copy_to_arena(arena, working_dir);
    cmd->comment = bm_sv_copy_to_arena(arena, comment);
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
    cmd->command = bm_sv_copy_to_arena(arena, command);
    cmd->working_dir = bm_sv_copy_to_arena(arena, working_dir);
    cmd->comment = bm_sv_copy_to_arena(arena, comment);
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

void build_custom_command_add_outputs(Custom_Command *cmd, Arena *arena, const String_List *items) {
    if (!cmd || !arena || !items) return;
    for (size_t i = 0; i < items->count; i++) {
        string_list_add(&cmd->outputs, arena, items->items[i]);
    }
}

void build_custom_command_add_byproducts(Custom_Command *cmd, Arena *arena, const String_List *items) {
    if (!cmd || !arena || !items) return;
    for (size_t i = 0; i < items->count; i++) {
        string_list_add(&cmd->byproducts, arena, items->items[i]);
    }
}

void build_custom_command_add_depends(Custom_Command *cmd, Arena *arena, const String_List *items) {
    if (!cmd || !arena || !items) return;
    for (size_t i = 0; i < items->count; i++) {
        string_list_add(&cmd->depends, arena, items->items[i]);
    }
}

void build_custom_command_set_main_dependency(Custom_Command *cmd, String_View value) {
    if (!cmd) return;
    cmd->main_dependency = value;
}

void build_custom_command_set_main_dependency_if_empty(Custom_Command *cmd, String_View value) {
    if (!cmd) return;
    if (cmd->main_dependency.count == 0) {
        cmd->main_dependency = value;
    }
}

void build_custom_command_set_depfile(Custom_Command *cmd, String_View value) {
    if (!cmd) return;
    cmd->depfile = value;
}

void build_custom_command_set_depfile_if_empty(Custom_Command *cmd, String_View value) {
    if (!cmd) return;
    if (cmd->depfile.count == 0) {
        cmd->depfile = value;
    }
}

void build_custom_command_set_flags(Custom_Command *cmd,
                                    bool append,
                                    bool verbatim,
                                    bool uses_terminal,
                                    bool command_expand_lists,
                                    bool depends_explicit_only,
                                    bool codegen) {
    if (!cmd) return;
    cmd->append = append;
    cmd->verbatim = verbatim;
    cmd->uses_terminal = uses_terminal;
    cmd->command_expand_lists = command_expand_lists;
    cmd->depends_explicit_only = depends_explicit_only;
    cmd->codegen = codegen;
}

void build_custom_command_merge_flags(Custom_Command *cmd,
                                      bool append,
                                      bool verbatim,
                                      bool uses_terminal,
                                      bool command_expand_lists,
                                      bool depends_explicit_only,
                                      bool codegen) {
    if (!cmd) return;
    cmd->append = cmd->append || append;
    cmd->verbatim = cmd->verbatim || verbatim;
    cmd->uses_terminal = cmd->uses_terminal || uses_terminal;
    cmd->command_expand_lists = cmd->command_expand_lists || command_expand_lists;
    cmd->depends_explicit_only = cmd->depends_explicit_only || depends_explicit_only;
    cmd->codegen = cmd->codegen || codegen;
}

void build_custom_command_append_command(Custom_Command *cmd, Arena *arena, String_View extra) {
    if (!cmd || !arena || extra.count == 0) return;
    if (cmd->command.count == 0) {
        cmd->command = bm_sv_copy_to_arena(arena, extra);
        return;
    }
    String_Builder sb = {0};
    sb_append_buf(&sb, cmd->command.data, cmd->command.count);
    sb_append_cstr(&sb, " && ");
    sb_append_buf(&sb, extra.data, extra.count);
    cmd->command = sb.count > 0 ? sv_from_cstr(arena_strndup(arena, sb.items, sb.count)) : sv_from_cstr("");
    nob_sb_free(sb);
}

Custom_Command* build_model_find_output_custom_command_by_output(Build_Model *model, String_View output) {
    if (!model || output.count == 0) return NULL;
    for (size_t i = 0; i < model->output_custom_command_count; i++) {
        Custom_Command *cmd = &model->output_custom_commands[i];
        for (size_t j = 0; j < cmd->outputs.count; j++) {
            if (nob_sv_eq(cmd->outputs.items[j], output)) return cmd;
        }
    }
    return NULL;
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



