#include "build_model_query.h"

#include "arena_dyn.h"
#include "stb_ds.h"

#include <string.h>

typedef struct {
    const Build_Model *model;
    Arena *arena;
    String_View *items;
    size_t count;
    size_t capacity;
    struct {
        char *key;
        int value;
    } *seen_targets;
    struct {
        char *key;
        int value;
    } *seen_libs;
} Bm_Query_Lib_Collect_Ctx;

static const String_View g_empty_sv = {0};

static bool bm_query_append_unique_lib(Bm_Query_Lib_Collect_Ctx *ctx, String_View lib) {
    if (!ctx || !ctx->arena || lib.count == 0 || !lib.data) return true;

    if (stbds_shgetp_null(ctx->seen_libs, nob_temp_sv_to_cstr(lib))) return true;
    stbds_shput(ctx->seen_libs, nob_temp_sv_to_cstr(lib), 1);

    if (!arena_da_reserve(ctx->arena,
                          (void**)&ctx->items,
                          &ctx->capacity,
                          sizeof(ctx->items[0]),
                          ctx->count + 1)) {
        return false;
    }

    ctx->items[ctx->count++] = lib;
    return true;
}

static bool bm_query_collect_target_libs(Bm_Query_Lib_Collect_Ctx *ctx, const Build_Target *target);

static bool bm_query_collect_dep_by_name(Bm_Query_Lib_Collect_Ctx *ctx, String_View dep_name) {
    if (!ctx || dep_name.count == 0) return true;
    const Build_Target *dep = bm_get_target(ctx->model, dep_name);
    if (!dep) return true;
    return bm_query_collect_target_libs(ctx, dep);
}

static bool bm_query_collect_list_items(Bm_Query_Lib_Collect_Ctx *ctx, const String_List *list, bool item_may_be_target) {
    if (!ctx || !list) return true;
    for (size_t i = 0; i < list->count; i++) {
        String_View item = list->items[i];
        if (item.count == 0) continue;
        if (item_may_be_target) {
            const Build_Target *dep = bm_get_target(ctx->model, item);
            if (dep) {
                if (!bm_query_collect_target_libs(ctx, dep)) return false;
                continue;
            }
        }
        if (!bm_query_append_unique_lib(ctx, item)) return false;
    }
    return true;
}

static bool bm_query_collect_target_libs(Bm_Query_Lib_Collect_Ctx *ctx, const Build_Target *target) {
    if (!ctx || !target || target->name.count == 0) return true;

    if (stbds_shgetp_null(ctx->seen_targets, nob_temp_sv_to_cstr(target->name))) return true;
    stbds_shput(ctx->seen_targets, nob_temp_sv_to_cstr(target->name), 1);

    if (!bm_query_collect_list_items(ctx, &target->link_libraries, true)) return false;
    if (!bm_query_collect_list_items(ctx, &target->interface_libs, true)) return false;

    for (size_t i = 0; i < target->dependencies.count; i++) {
        if (!bm_query_collect_dep_by_name(ctx, target->dependencies.items[i])) return false;
    }
    for (size_t i = 0; i < target->interface_dependencies.count; i++) {
        if (!bm_query_collect_dep_by_name(ctx, target->interface_dependencies.items[i])) return false;
    }
    for (size_t i = 0; i < target->object_dependencies.count; i++) {
        if (!bm_query_collect_dep_by_name(ctx, target->object_dependencies.items[i])) return false;
    }

    return true;
}

const Build_Target *bm_get_target(const Build_Model *m, String_View name) {
    return bm_query_target_by_name(m, name);
}

const Build_Target *bm_query_target_by_name(const Build_Model *model, String_View name) {
    if (!model || name.count == 0) return NULL;

    if (model->target_index_by_name) {
        Build_Target_Index_Entry *index_map = model->target_index_by_name;
        Build_Target_Index_Entry *entry = stbds_shgetp_null(index_map, nob_temp_sv_to_cstr(name));
        if (entry && entry->value >= 0 && (size_t)entry->value < model->target_count) {
            Build_Target *candidate = model->targets[entry->value];
            if (candidate && nob_sv_eq(candidate->name, name)) return candidate;
        }
    }

    for (size_t i = 0; i < model->target_count; i++) {
        Build_Target *t = model->targets[i];
        if (t && nob_sv_eq(t->name, name)) return t;
    }
    return NULL;
}

String_View bm_target_name(const Build_Target *t) {
    return t ? t->name : g_empty_sv;
}

Target_Type bm_target_type(const Build_Target *t) {
    return t ? t->type : TARGET_UTILITY;
}

Target_Type bm_query_target_type(const Build_Target *target) {
    return bm_target_type(target);
}

size_t bm_target_source_count(const Build_Target *t) {
    return t ? t->sources.count : 0;
}

String_View bm_target_source_at(const Build_Target *t, size_t index) {
    if (!t || index >= t->sources.count) return g_empty_sv;
    return t->sources.items[index];
}

size_t bm_target_include_count(const Build_Target *t) {
    return t ? t->conditional_include_directories.count : 0;
}

String_View bm_target_include_at(const Build_Target *t, size_t index) {
    if (!t || index >= t->conditional_include_directories.count) return g_empty_sv;
    return t->conditional_include_directories.items[index].value;
}

void bm_query_target_sources(const Build_Target *target, const String_View **out_items, size_t *out_count) {
    if (out_items) *out_items = NULL;
    if (out_count) *out_count = 0;
    if (!target) return;
    if (out_items) *out_items = target->sources.items;
    if (out_count) *out_count = target->sources.count;
}

void bm_query_target_includes(const Build_Target *target, const String_View **out_items, size_t *out_count) {
    if (out_items) *out_items = NULL;
    if (out_count) *out_count = 0;
    if (!target) return;
    if (out_items) *out_items = target->interface_include_directories.items;
    if (out_count) *out_count = target->interface_include_directories.count;
}

void bm_query_target_deps(const Build_Target *target, const String_View **out_items, size_t *out_count) {
    if (out_items) *out_items = NULL;
    if (out_count) *out_count = 0;
    if (!target) return;
    if (out_items) *out_items = target->dependencies.items;
    if (out_count) *out_count = target->dependencies.count;
}

void bm_query_target_link_libraries(const Build_Target *target, const String_View **out_items, size_t *out_count) {
    if (out_items) *out_items = NULL;
    if (out_count) *out_count = 0;
    if (!target) return;
    if (out_items) *out_items = target->link_libraries.items;
    if (out_count) *out_count = target->link_libraries.count;
}

bool bm_query_target_effective_link_libraries(const Build_Target *target,
                                              Arena *scratch_arena,
                                              const Logic_Eval_Context *logic_ctx,
                                              const String_View **out_items,
                                              size_t *out_count) {
    if (out_items) *out_items = NULL;
    if (out_count) *out_count = 0;
    if (!target || !scratch_arena) return false;

    String_List list = {0};
    build_target_collect_effective_link_libraries((Build_Target*)target, scratch_arena, logic_ctx, &list);
    if (out_items) *out_items = list.items;
    if (out_count) *out_count = list.count;
    return true;
}

String_View bm_query_project_name(const Build_Model *model) {
    return model ? model->project_name : g_empty_sv;
}

String_View bm_query_project_version(const Build_Model *model) {
    return model ? model->project_version : g_empty_sv;
}

bool bm_is_windows(const Build_Model *m) {
    return m ? m->is_windows : false;
}

bool bm_is_linux(const Build_Model *m) {
    return m ? m->is_linux : false;
}

bool bm_is_unix(const Build_Model *m) {
    return m ? m->is_unix : false;
}

bool bm_is_apple(const Build_Model *m) {
    return m ? m->is_apple : false;
}

String_View *bm_query_transitive_libs(const Build_Model *model,
                                      const Build_Target *target,
                                      Arena *scratch_arena,
                                      size_t *out_count) {
    if (out_count) *out_count = 0;
    if (!model || !target || !scratch_arena) return NULL;

    Bm_Query_Lib_Collect_Ctx ctx = {0};
    ctx.model = model;
    ctx.arena = scratch_arena;

    if (!bm_query_collect_target_libs(&ctx, target)) {
        stbds_shfree(ctx.seen_targets);
        stbds_shfree(ctx.seen_libs);
        return NULL;
    }

    stbds_shfree(ctx.seen_targets);
    stbds_shfree(ctx.seen_libs);
    if (out_count) *out_count = ctx.count;
    return ctx.items;
}
