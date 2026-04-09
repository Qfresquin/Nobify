#include "build_model_internal.h"

static bool bm_replay_append_string(BM_Builder *builder,
                                    const Event *ev,
                                    String_View **dest,
                                    String_View value) {
    String_View owned = {0};
    if (!bm_copy_string(builder->arena, value, &owned) ||
        !arena_arr_push(builder->arena, *dest, owned)) {
        return bm_builder_error(builder, ev, "failed to append replay action item", "increase arena capacity");
    }
    return true;
}

static bool bm_replay_append_env(BM_Builder *builder,
                                 const Event *ev,
                                 BM_Replay_Action_Record *action,
                                 String_View key,
                                 String_View value) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!builder || !ev || !action) return false;
    if (bm_string_view_is_empty(key)) {
        return bm_builder_error(builder, ev, "replay action environment entry is missing a key", "emit KEY before VALUE for replay env entries");
    }

    nob_sb_append_buf(&sb, key.data ? key.data : "", key.count);
    nob_sb_append(&sb, '=');
    nob_sb_append_buf(&sb, value.data ? value.data : "", value.count);
    copy = arena_strndup(builder->arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy || !arena_arr_push(builder->arena, action->environment, nob_sv_from_parts(copy, strlen(copy)))) {
        return bm_builder_error(builder, ev, "failed to append replay action environment entry", "increase arena capacity");
    }
    return true;
}

bool bm_builder_handle_replay_event(BM_Builder *builder, const Event *ev) {
    Build_Model_Draft *draft = builder ? builder->draft : NULL;
    if (!builder || !draft || !ev) return false;

    switch (ev->h.kind) {
        case EVENT_REPLAY_ACTION_DECLARE: {
            BM_Replay_Action_Record action = {0};
            BM_Directory_Id owner_directory_id = bm_builder_current_directory_id(builder);
            if (owner_directory_id == BM_DIRECTORY_ID_INVALID) {
                return bm_builder_error(builder,
                                        ev,
                                        "replay action declaration without an active directory",
                                        "emit directory enter before replay actions");
            }
            if (bm_string_view_is_empty(ev->as.replay_action_declare.action_key)) {
                return bm_builder_error(builder, ev, "replay action key is empty", "emit a stable replay action key before replay items");
            }
            if (bm_draft_find_replay_action_const(draft, ev->as.replay_action_declare.action_key)) {
                return bm_builder_error(builder, ev, "duplicate replay action key", "replay action keys must be unique");
            }

            action.id = (BM_Replay_Action_Id)arena_arr_len(draft->replay_actions);
            action.owner_directory_id = owner_directory_id;
            action.provenance = bm_provenance_from_event(builder->arena, ev);
            action.kind = bm_replay_action_kind_from_event(ev->as.replay_action_declare.action_kind);
            action.phase = bm_replay_phase_from_event(ev->as.replay_action_declare.phase);
            if (!bm_copy_string(builder->arena, ev->as.replay_action_declare.action_key, &action.action_key) ||
                !bm_copy_string(builder->arena, ev->as.replay_action_declare.working_directory, &action.working_directory) ||
                !arena_arr_push(builder->arena, draft->replay_actions, action)) {
                return bm_builder_error(builder, ev, "failed to append replay action", "increase arena capacity");
            }
            return true;
        }

        case EVENT_REPLAY_ACTION_ADD_INPUT:
        case EVENT_REPLAY_ACTION_ADD_OUTPUT:
        case EVENT_REPLAY_ACTION_ADD_ARGV:
        case EVENT_REPLAY_ACTION_ADD_ENV: {
            BM_Replay_Action_Record *action = NULL;
            String_View action_key = nob_sv_from_cstr("");
            if (ev->h.kind == EVENT_REPLAY_ACTION_ADD_INPUT) action_key = ev->as.replay_action_add_input.action_key;
            if (ev->h.kind == EVENT_REPLAY_ACTION_ADD_OUTPUT) action_key = ev->as.replay_action_add_output.action_key;
            if (ev->h.kind == EVENT_REPLAY_ACTION_ADD_ARGV) action_key = ev->as.replay_action_add_argv.action_key;
            if (ev->h.kind == EVENT_REPLAY_ACTION_ADD_ENV) action_key = ev->as.replay_action_add_env.action_key;
            action = bm_draft_find_replay_action(draft, action_key);
            if (!action) {
                return bm_builder_error(builder,
                                        ev,
                                        "replay action item references an unknown action key",
                                        "emit replay action declaration before replay items");
            }

            if (ev->h.kind == EVENT_REPLAY_ACTION_ADD_INPUT) {
                return bm_replay_append_string(builder, ev, &action->inputs, ev->as.replay_action_add_input.path);
            }
            if (ev->h.kind == EVENT_REPLAY_ACTION_ADD_OUTPUT) {
                return bm_replay_append_string(builder, ev, &action->outputs, ev->as.replay_action_add_output.path);
            }
            if (ev->h.kind == EVENT_REPLAY_ACTION_ADD_ARGV) {
                if (ev->as.replay_action_add_argv.arg_index != arena_arr_len(action->argv)) {
                    return bm_builder_error(builder,
                                            ev,
                                            "replay action argv must be emitted in append order",
                                            "emit replay argv entries with contiguous zero-based arg_index");
                }
                return bm_replay_append_string(builder, ev, &action->argv, ev->as.replay_action_add_argv.value);
            }
            return bm_replay_append_env(builder,
                                        ev,
                                        action,
                                        ev->as.replay_action_add_env.key,
                                        ev->as.replay_action_add_env.value);
        }

        case EVENT_KIND_COUNT:
        default:
            return bm_builder_error(builder, ev, "unexpected replay handler event", "fix replay event dispatch");
    }
}
