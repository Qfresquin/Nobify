#include "build_model_v2.h"
#include "arena_dyn.h"

Build_Model_v2 *build_model_v2_create(Arena *arena) {
    if (!arena) return NULL;
    return arena_alloc_zero(arena, sizeof(Build_Model_v2));
}

void build_model_v2_set_project(Build_Model_v2 *model, String_View name, String_View version) {
    if (!model) return;
    model->project.name = name;
    model->project.version = version;
}

Build_Target *build_model_v2_add_target(Build_Model_v2 *model,
                                        Arena *arena,
                                        String_View name,
                                        Target_Type type) {
    if (!model || !arena || name.count == 0) return NULL;
    if (!arena_da_reserve(arena, (void**)&model->targets, &model->target_capacity,
            sizeof(*model->targets), model->target_count + 1)) {
        return NULL;
    }

    Build_Target *target = arena_alloc_zero(arena, sizeof(Build_Target));
    if (!target) return NULL;
    target->name = sv_from_cstr(arena_strndup(arena, name.data, name.count));
    target->type = type;
    model->targets[model->target_count++] = target;
    return target;
}

void build_model_v2_register_find_package_result(Build_Model_v2 *model,
                                                 Arena *arena,
                                                 Build_Model_v2_Find_Package_Result result) {
    (void)model;
    (void)arena;
    (void)result;
    // Placeholder: detailed package registry lands in semantic phase for v2.
}
