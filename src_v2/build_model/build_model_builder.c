#include "build_model_internal.h"

static void bm_diag_sink_default_emit(void *userdata,
                                      Diag_Severity severity,
                                      const char *component,
                                      String_View file_path,
                                      uint32_t line,
                                      uint32_t col,
                                      const char *command,
                                      const char *cause,
                                      const char *hint) {
    (void)userdata;
    diag_log(severity,
             component ? component : "build_model",
             file_path.count > 0 ? nob_temp_sv_to_cstr(file_path) : "<build_model>",
             line,
             col,
             command ? command : "",
             cause ? cause : "",
             hint ? hint : "");
}

static void bm_diag_emit(Diag_Sink *sink,
                         Diag_Severity severity,
                         BM_Provenance provenance,
                         const char *component,
                         const char *command,
                         const char *cause,
                         const char *hint) {
    if (!sink || !sink->emit) return;
    sink->emit(sink->userdata,
               severity,
               component ? component : "build_model",
               provenance.file_path,
               provenance.line,
               provenance.col,
               command ? command : "",
               cause ? cause : "",
               hint ? hint : "");
}

static BM_Provenance bm_empty_provenance(void) {
    BM_Provenance provenance = {0};
    provenance.event_kind = EVENT_KIND_COUNT;
    return provenance;
}

static bool bm_is_supported_build_event(Event_Kind kind) {
    switch (kind) {
        case EVENT_PROJECT_DECLARE:
        case EVENT_PROJECT_MINIMUM_REQUIRED:
        case EVENT_DIRECTORY_ENTER:
        case EVENT_DIRECTORY_LEAVE:
        case EVENT_DIRECTORY_PROPERTY_MUTATE:
        case EVENT_GLOBAL_PROPERTY_MUTATE:
        case EVENT_TARGET_DECLARE:
        case EVENT_TARGET_ADD_SOURCE:
        case EVENT_TARGET_FILE_SET_DECLARE:
        case EVENT_TARGET_FILE_SET_ADD_BASE_DIR:
        case EVENT_SOURCE_MARK_GENERATED:
        case EVENT_SOURCE_PROPERTY_MUTATE:
        case EVENT_TARGET_ADD_DEPENDENCY:
        case EVENT_BUILD_STEP_DECLARE:
        case EVENT_BUILD_STEP_ADD_OUTPUT:
        case EVENT_BUILD_STEP_ADD_BYPRODUCT:
        case EVENT_BUILD_STEP_ADD_DEPENDENCY:
        case EVENT_BUILD_STEP_ADD_COMMAND:
        case EVENT_REPLAY_ACTION_DECLARE:
        case EVENT_REPLAY_ACTION_ADD_INPUT:
        case EVENT_REPLAY_ACTION_ADD_OUTPUT:
        case EVENT_REPLAY_ACTION_ADD_ARGV:
        case EVENT_REPLAY_ACTION_ADD_ENV:
        case EVENT_TARGET_PROP_SET:
        case EVENT_TARGET_LINK_LIBRARIES:
        case EVENT_TARGET_LINK_OPTIONS:
        case EVENT_TARGET_LINK_DIRECTORIES:
        case EVENT_TARGET_INCLUDE_DIRECTORIES:
        case EVENT_TARGET_COMPILE_DEFINITIONS:
        case EVENT_TARGET_COMPILE_OPTIONS:
        case EVENT_TEST_ENABLE:
        case EVENT_TEST_ADD:
        case EVENT_INSTALL_RULE_ADD:
        case EVENT_EXPORT_INSTALL:
        case EVENT_EXPORT_BUILD_DECLARE:
        case EVENT_EXPORT_BUILD_ADD_TARGET:
        case EVENT_EXPORT_PACKAGE_REGISTRY:
        case EVENT_CPACK_ADD_INSTALL_TYPE:
        case EVENT_CPACK_ADD_COMPONENT_GROUP:
        case EVENT_CPACK_ADD_COMPONENT:
        case EVENT_CPACK_PACKAGE_DECLARE:
        case EVENT_CPACK_PACKAGE_ADD_GENERATOR:
        case EVENT_PACKAGE_FIND_RESULT:
            return true;
        case EVENT_KIND_COUNT:
            return false;
        default:
            return false;
    }
}

bool bm_copy_string(Arena *arena, String_View input, String_View *out) {
    if (!out) return false;
    out->data = NULL;
    out->count = 0;
    if (!input.data || input.count == 0) return true;

    char *copy = arena_alloc(arena, input.count + 1);
    if (!copy) return false;
    memcpy(copy, input.data, input.count);
    copy[input.count] = '\0';
    *out = nob_sv_from_parts(copy, input.count);
    return true;
}

BM_Provenance bm_provenance_from_event(Arena *arena, const Event *ev) {
    BM_Provenance provenance = {0};
    if (!ev) {
        provenance.event_kind = EVENT_KIND_COUNT;
        return provenance;
    }

    provenance.event_seq = ev->h.seq;
    provenance.event_kind = ev->h.kind;
    provenance.line = (uint32_t)ev->h.origin.line;
    provenance.col = (uint32_t)ev->h.origin.col;
    if (!bm_copy_string(arena, ev->h.origin.file_path, &provenance.file_path)) {
        provenance.file_path = nob_sv_from_parts(NULL, 0);
    }
    return provenance;
}

static String_View bm_trim_whitespace(String_View input) {
    return nob_sv_trim(input);
}

bool bm_split_cmake_list(Arena *arena, String_View raw, String_View **out_items) {
    if (!out_items) return false;
    *out_items = NULL;
    if (!raw.data || raw.count == 0) return true;

    size_t item_begin = 0;
    for (size_t i = 0; i <= raw.count; ++i) {
        bool at_separator = (i == raw.count) || (raw.data[i] == ';');
        if (!at_separator) continue;

        String_View item = bm_trim_whitespace(nob_sv_from_parts(raw.data + item_begin, i - item_begin));
        if (item.count > 0) {
            String_View owned = {0};
            if (!bm_copy_string(arena, item, &owned)) return false;
            if (!arena_arr_push(arena, *out_items, owned)) return false;
        }
        item_begin = i + 1;
    }
    return true;
}

bool bm_add_name_index(Arena *arena, BM_Name_Index_Entry **index, String_View name, uint32_t id) {
    if (!arena || !index) return false;
    BM_Name_Index_Entry entry = {0};
    entry.name = name;
    entry.id = id;
    return arena_arr_push(arena, *index, entry);
}

bool bm_sv_eq_ci_lit(String_View sv, const char *lit) {
    if (!lit) return false;
    size_t lit_len = strlen(lit);
    if (sv.count != lit_len) return false;
    for (size_t i = 0; i < lit_len; ++i) {
        if (tolower((unsigned char)sv.data[i]) != tolower((unsigned char)lit[i])) return false;
    }
    return true;
}

bool bm_sv_truthy(String_View sv) {
    if (!sv.data || sv.count == 0) return false;
    return bm_sv_eq_ci_lit(sv, "1") ||
           bm_sv_eq_ci_lit(sv, "on") ||
           bm_sv_eq_ci_lit(sv, "yes") ||
           bm_sv_eq_ci_lit(sv, "true") ||
           bm_sv_eq_ci_lit(sv, "y");
}

bool bm_string_view_is_empty(String_View sv) {
    return !sv.data || sv.count == 0;
}

bool bm_path_is_abs(String_View path) {
    return path.count > 0 && path.data && path.data[0] == '/';
}

bool bm_normalize_path(Arena *arena, String_View path, String_View *out) {
    String_View *segments = NULL;
    bool absolute = bm_path_is_abs(path);
    size_t i = 0;
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!arena || !out) return false;

    while (i < path.count) {
        while (i < path.count && path.data[i] == '/') i++;
        size_t start = i;
        while (i < path.count && path.data[i] != '/') i++;
        if (i == start) continue;

        String_View seg = nob_sv_from_parts(path.data + start, i - start);
        if (nob_sv_eq(seg, nob_sv_from_cstr("."))) continue;
        if (nob_sv_eq(seg, nob_sv_from_cstr(".."))) {
            if (!arena_arr_empty(segments) && !nob_sv_eq(arena_arr_last(segments), nob_sv_from_cstr(".."))) {
                arena_arr_set_len(segments, arena_arr_len(segments) - 1);
            } else if (!absolute) {
                if (!arena_arr_push(arena, segments, seg)) {
                    nob_sb_free(sb);
                    return false;
                }
            }
            continue;
        }
        if (!arena_arr_push(arena, segments, seg)) {
            nob_sb_free(sb);
            return false;
        }
    }

    if (absolute) nob_sb_append(&sb, '/');
    for (size_t idx = 0; idx < arena_arr_len(segments); ++idx) {
        if (idx > 0) nob_sb_append(&sb, '/');
        nob_sb_append_buf(&sb, segments[idx].data, segments[idx].count);
    }
    if (sb.count == 0) {
        if (absolute) nob_sb_append(&sb, '/');
        else nob_sb_append(&sb, '.');
    }

    copy = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    *out = nob_sv_from_cstr(copy);
    return true;
}

bool bm_path_join(Arena *arena, String_View lhs, String_View rhs, String_View *out) {
    Nob_String_Builder sb = {0};
    char *copy = NULL;
    if (!arena || !out) return false;
    nob_sb_append_buf(&sb, lhs.data ? lhs.data : "", lhs.count);
    if (sb.count > 0 && sb.items[sb.count - 1] != '/') nob_sb_append(&sb, '/');
    nob_sb_append_buf(&sb, rhs.data ? rhs.data : "", rhs.count);
    copy = arena_strndup(arena, sb.items ? sb.items : "", sb.count);
    nob_sb_free(sb);
    if (!copy) return false;
    return bm_normalize_path(arena, nob_sv_from_cstr(copy), out);
}

bool bm_path_rebase(Arena *arena, String_View base_dir, String_View path, String_View *out) {
    if (!arena || !out) return false;
    if (bm_path_is_abs(path)) return bm_normalize_path(arena, path, out);
    return bm_path_join(arena, base_dir, path, out);
}

BM_Target_Kind bm_target_kind_from_event(Cmake_Target_Type type) {
    switch (type) {
        case EV_TARGET_EXECUTABLE: return BM_TARGET_EXECUTABLE;
        case EV_TARGET_LIBRARY_STATIC: return BM_TARGET_STATIC_LIBRARY;
        case EV_TARGET_LIBRARY_SHARED: return BM_TARGET_SHARED_LIBRARY;
        case EV_TARGET_LIBRARY_MODULE: return BM_TARGET_MODULE_LIBRARY;
        case EV_TARGET_LIBRARY_INTERFACE: return BM_TARGET_INTERFACE_LIBRARY;
        case EV_TARGET_LIBRARY_OBJECT: return BM_TARGET_OBJECT_LIBRARY;
        case EV_TARGET_LIBRARY_UNKNOWN: return BM_TARGET_UNKNOWN_LIBRARY;
    }
    return BM_TARGET_UTILITY;
}

BM_Build_Step_Kind bm_build_step_kind_from_event(Event_Build_Step_Kind kind) {
    switch (kind) {
        case EVENT_BUILD_STEP_OUTPUT_RULE: return BM_BUILD_STEP_OUTPUT_RULE;
        case EVENT_BUILD_STEP_CUSTOM_TARGET: return BM_BUILD_STEP_CUSTOM_TARGET;
        case EVENT_BUILD_STEP_TARGET_PRE_BUILD: return BM_BUILD_STEP_TARGET_PRE_BUILD;
        case EVENT_BUILD_STEP_TARGET_PRE_LINK: return BM_BUILD_STEP_TARGET_PRE_LINK;
        case EVENT_BUILD_STEP_TARGET_POST_BUILD: return BM_BUILD_STEP_TARGET_POST_BUILD;
    }
    return BM_BUILD_STEP_OUTPUT_RULE;
}

BM_Replay_Phase bm_replay_phase_from_event(Event_Replay_Phase phase) {
    switch (phase) {
        case EVENT_REPLAY_PHASE_CONFIGURE: return BM_REPLAY_PHASE_CONFIGURE;
        case EVENT_REPLAY_PHASE_BUILD: return BM_REPLAY_PHASE_BUILD;
        case EVENT_REPLAY_PHASE_TEST: return BM_REPLAY_PHASE_TEST;
        case EVENT_REPLAY_PHASE_INSTALL: return BM_REPLAY_PHASE_INSTALL;
        case EVENT_REPLAY_PHASE_EXPORT: return BM_REPLAY_PHASE_EXPORT;
        case EVENT_REPLAY_PHASE_PACKAGE: return BM_REPLAY_PHASE_PACKAGE;
        case EVENT_REPLAY_PHASE_HOST_ONLY: return BM_REPLAY_PHASE_HOST_ONLY;
    }
    return BM_REPLAY_PHASE_CONFIGURE;
}

BM_Replay_Action_Kind bm_replay_action_kind_from_event(Event_Replay_Action_Kind kind) {
    switch (kind) {
        case EVENT_REPLAY_ACTION_FILESYSTEM: return BM_REPLAY_ACTION_FILESYSTEM;
        case EVENT_REPLAY_ACTION_PROCESS: return BM_REPLAY_ACTION_PROCESS;
        case EVENT_REPLAY_ACTION_PROBE: return BM_REPLAY_ACTION_PROBE;
        case EVENT_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION: return BM_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION;
        case EVENT_REPLAY_ACTION_TEST_DRIVER: return BM_REPLAY_ACTION_TEST_DRIVER;
        case EVENT_REPLAY_ACTION_HOST_EFFECT: return BM_REPLAY_ACTION_HOST_EFFECT;
    }
    return BM_REPLAY_ACTION_FILESYSTEM;
}

BM_Replay_Opcode bm_replay_opcode_from_event(Event_Replay_Opcode opcode) {
    switch (opcode) {
        case EVENT_REPLAY_OPCODE_NONE: return BM_REPLAY_OPCODE_NONE;
        case EVENT_REPLAY_OPCODE_FS_MKDIR: return BM_REPLAY_OPCODE_FS_MKDIR;
        case EVENT_REPLAY_OPCODE_FS_WRITE_TEXT: return BM_REPLAY_OPCODE_FS_WRITE_TEXT;
        case EVENT_REPLAY_OPCODE_FS_APPEND_TEXT: return BM_REPLAY_OPCODE_FS_APPEND_TEXT;
        case EVENT_REPLAY_OPCODE_FS_COPY_FILE: return BM_REPLAY_OPCODE_FS_COPY_FILE;
        case EVENT_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL: return BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL;
        case EVENT_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR: return BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR;
        case EVENT_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR: return BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR;
        case EVENT_REPLAY_OPCODE_HOST_LOCK_ACQUIRE: return BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE;
        case EVENT_REPLAY_OPCODE_HOST_LOCK_RELEASE: return BM_REPLAY_OPCODE_HOST_LOCK_RELEASE;
        case EVENT_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE: return BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE;
        case EVENT_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT: return BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT;
        case EVENT_REPLAY_OPCODE_PROBE_TRY_RUN: return BM_REPLAY_OPCODE_PROBE_TRY_RUN;
        case EVENT_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR: return BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR;
        case EVENT_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE: return BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE;
        case EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY: return BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY;
        case EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL: return BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL;
        case EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF: return BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF;
        case EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF: return BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF;
        case EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST: return BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST;
        case EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP: return BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP;
        case EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL:
            return BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL;
        case EVENT_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL:
            return BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL;
    }
    return BM_REPLAY_OPCODE_NONE;
}

BM_Visibility bm_visibility_from_event(Cmake_Visibility visibility) {
    switch (visibility) {
        case EV_VISIBILITY_PUBLIC: return BM_VISIBILITY_PUBLIC;
        case EV_VISIBILITY_INTERFACE: return BM_VISIBILITY_INTERFACE;
        case EV_VISIBILITY_PRIVATE:
        case EV_VISIBILITY_UNSPECIFIED:
            return BM_VISIBILITY_PRIVATE;
    }
    return BM_VISIBILITY_PRIVATE;
}

BM_Install_Rule_Kind bm_install_rule_kind_from_event(Cmake_Install_Rule_Type kind) {
    switch (kind) {
        case EV_INSTALL_RULE_TARGET: return BM_INSTALL_RULE_TARGET;
        case EV_INSTALL_RULE_FILE: return BM_INSTALL_RULE_FILE;
        case EV_INSTALL_RULE_PROGRAM: return BM_INSTALL_RULE_PROGRAM;
        case EV_INSTALL_RULE_DIRECTORY: return BM_INSTALL_RULE_DIRECTORY;
    }
    return BM_INSTALL_RULE_FILE;
}

BM_Directory_Id bm_builder_current_directory_id(const BM_Builder *builder) {
    if (!builder || arena_arr_len(builder->directory_stack) == 0) return BM_DIRECTORY_ID_INVALID;
    return arena_arr_last(builder->directory_stack);
}

BM_Directory_Record *bm_draft_get_directory(Build_Model_Draft *draft, BM_Directory_Id id) {
    if (!draft || id == BM_DIRECTORY_ID_INVALID || (size_t)id >= arena_arr_len(draft->directories)) return NULL;
    return &draft->directories[id];
}

const BM_Directory_Record *bm_draft_get_directory_const(const Build_Model_Draft *draft, BM_Directory_Id id) {
    if (!draft || id == BM_DIRECTORY_ID_INVALID || (size_t)id >= arena_arr_len(draft->directories)) return NULL;
    return &draft->directories[id];
}

BM_Build_Step_Record *bm_draft_find_build_step(Build_Model_Draft *draft, String_View step_key) {
    if (!draft) return NULL;
    for (size_t i = 0; i < arena_arr_len(draft->build_steps); ++i) {
        if (nob_sv_eq(draft->build_steps[i].step_key, step_key)) return &draft->build_steps[i];
    }
    return NULL;
}

const BM_Build_Step_Record *bm_draft_find_build_step_const(const Build_Model_Draft *draft, String_View step_key) {
    if (!draft) return NULL;
    for (size_t i = 0; i < arena_arr_len(draft->build_steps); ++i) {
        if (nob_sv_eq(draft->build_steps[i].step_key, step_key)) return &draft->build_steps[i];
    }
    return NULL;
}

BM_Replay_Action_Record *bm_draft_find_replay_action(Build_Model_Draft *draft, String_View action_key) {
    if (!draft) return NULL;
    for (size_t i = 0; i < arena_arr_len(draft->replay_actions); ++i) {
        if (nob_sv_eq(draft->replay_actions[i].action_key, action_key)) return &draft->replay_actions[i];
    }
    return NULL;
}

const BM_Replay_Action_Record *bm_draft_find_replay_action_const(const Build_Model_Draft *draft,
                                                                 String_View action_key) {
    if (!draft) return NULL;
    for (size_t i = 0; i < arena_arr_len(draft->replay_actions); ++i) {
        if (nob_sv_eq(draft->replay_actions[i].action_key, action_key)) return &draft->replay_actions[i];
    }
    return NULL;
}

BM_Target_Id bm_draft_find_target_id(const Build_Model_Draft *draft, String_View name) {
    if (!draft) return BM_TARGET_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(draft->target_name_index); ++i) {
        if (nob_sv_eq(draft->target_name_index[i].name, name)) {
            return (BM_Target_Id)draft->target_name_index[i].id;
        }
    }
    return BM_TARGET_ID_INVALID;
}

BM_Target_Record *bm_draft_find_target(Build_Model_Draft *draft, String_View name) {
    BM_Target_Id id = bm_draft_find_target_id(draft, name);
    if (id == BM_TARGET_ID_INVALID) return NULL;
    return &draft->targets[id];
}

const BM_Target_Record *bm_draft_find_target_const(const Build_Model_Draft *draft, String_View name) {
    BM_Target_Id id = bm_draft_find_target_id(draft, name);
    if (id == BM_TARGET_ID_INVALID) return NULL;
    return &draft->targets[id];
}

BM_Test_Id bm_draft_find_test_id(const Build_Model_Draft *draft, String_View name) {
    if (!draft) return BM_TEST_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(draft->test_name_index); ++i) {
        if (nob_sv_eq(draft->test_name_index[i].name, name)) return (BM_Test_Id)draft->test_name_index[i].id;
    }
    return BM_TEST_ID_INVALID;
}

BM_Package_Id bm_draft_find_package_id(const Build_Model_Draft *draft, String_View name) {
    if (!draft) return BM_PACKAGE_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(draft->package_name_index); ++i) {
        if (nob_sv_eq(draft->package_name_index[i].name, name)) return (BM_Package_Id)draft->package_name_index[i].id;
    }
    return BM_PACKAGE_ID_INVALID;
}

BM_CPack_Install_Type_Id bm_draft_find_install_type_id(const Build_Model_Draft *draft, String_View name) {
    if (!draft) return BM_CPACK_INSTALL_TYPE_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(draft->cpack_install_types); ++i) {
        if (nob_sv_eq(draft->cpack_install_types[i].name, name)) return draft->cpack_install_types[i].id;
    }
    return BM_CPACK_INSTALL_TYPE_ID_INVALID;
}

BM_CPack_Component_Group_Id bm_draft_find_component_group_id(const Build_Model_Draft *draft, String_View name) {
    if (!draft) return BM_CPACK_COMPONENT_GROUP_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(draft->cpack_component_groups); ++i) {
        if (nob_sv_eq(draft->cpack_component_groups[i].name, name)) return draft->cpack_component_groups[i].id;
    }
    return BM_CPACK_COMPONENT_GROUP_ID_INVALID;
}

BM_CPack_Component_Id bm_draft_find_component_id(const Build_Model_Draft *draft, String_View name) {
    if (!draft) return BM_CPACK_COMPONENT_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(draft->cpack_components); ++i) {
        if (nob_sv_eq(draft->cpack_components[i].name, name)) return draft->cpack_components[i].id;
    }
    return BM_CPACK_COMPONENT_ID_INVALID;
}

BM_Export_Id bm_draft_find_export_id(const Build_Model_Draft *draft, String_View name) {
    if (!draft) return BM_EXPORT_ID_INVALID;
    for (size_t i = 0; i < arena_arr_len(draft->exports); ++i) {
        if (nob_sv_eq(draft->exports[i].name, name)) return draft->exports[i].id;
    }
    return BM_EXPORT_ID_INVALID;
}

bool bm_diag_error(Diag_Sink *sink,
                   BM_Provenance provenance,
                   const char *component,
                   const char *command,
                   const char *cause,
                   const char *hint) {
    bm_diag_emit(sink, DIAG_SEV_ERROR, provenance, component, command, cause, hint);
    return false;
}

void bm_diag_warn(Diag_Sink *sink,
                  BM_Provenance provenance,
                  const char *component,
                  const char *command,
                  const char *cause,
                  const char *hint) {
    bm_diag_emit(sink, DIAG_SEV_WARNING, provenance, component, command, cause, hint);
}

Diag_Sink *bm_diag_sink_create(Arena *arena, Diag_Sink_Emit_Fn emit, void *userdata) {
    Diag_Sink *sink = NULL;
    if (!arena || !emit) return NULL;
    sink = arena_alloc_zero(arena, sizeof(*sink));
    if (!sink) return NULL;
    sink->emit = emit;
    sink->userdata = userdata;
    return sink;
}

Diag_Sink *bm_diag_sink_create_default(Arena *arena) {
    return bm_diag_sink_create(arena, bm_diag_sink_default_emit, NULL);
}

bool bm_builder_error(BM_Builder *builder, const Event *ev, const char *cause, const char *hint) {
    if (builder) builder->has_fatal_error = true;
    return bm_diag_error(builder ? builder->sink : NULL,
                         ev ? bm_provenance_from_event(builder ? builder->arena : NULL, ev) : bm_empty_provenance(),
                         "build_model_builder",
                         ev ? event_kind_name(ev->h.kind) : "build_model",
                         cause,
                         hint);
}

void bm_builder_warn(BM_Builder *builder, const Event *ev, const char *cause, const char *hint) {
    bm_diag_warn(builder ? builder->sink : NULL,
                 ev ? bm_provenance_from_event(builder ? builder->arena : NULL, ev) : bm_empty_provenance(),
                 "build_model_builder",
                 ev ? event_kind_name(ev->h.kind) : "build_model",
                 cause,
                 hint);
}

bool bm_append_string(Arena *arena, String_View **items, String_View item) {
    if (!arena || !items) return false;
    return arena_arr_push(arena, *items, item);
}

bool bm_append_item(Arena *arena, BM_String_Item_View **items, BM_String_Item_View item) {
    if (!arena || !items) return false;
    return arena_arr_push(arena, *items, item);
}

bool bm_apply_item_mutation(Arena *arena,
                            BM_String_Item_View **dest,
                            const BM_String_Item_View *items,
                            size_t count,
                            Event_Property_Mutate_Op op) {
    if (!arena || !dest) return false;

    switch (op) {
        case EVENT_PROPERTY_MUTATE_SET: {
            BM_String_Item_View *replaced = NULL;
            for (size_t i = 0; i < count; ++i) {
                if (!arena_arr_push(arena, replaced, items[i])) return false;
            }
            *dest = replaced;
            return true;
        }

        case EVENT_PROPERTY_MUTATE_PREPEND_LIST: {
            BM_String_Item_View *merged = NULL;
            for (size_t i = 0; i < count; ++i) {
                if (!arena_arr_push(arena, merged, items[i])) return false;
            }
            for (size_t i = 0; i < arena_arr_len(*dest); ++i) {
                if (!arena_arr_push(arena, merged, (*dest)[i])) return false;
            }
            *dest = merged;
            return true;
        }

        case EVENT_PROPERTY_MUTATE_APPEND_LIST:
        case EVENT_PROPERTY_MUTATE_APPEND_STRING:
            for (size_t i = 0; i < count; ++i) {
                if (!arena_arr_push(arena, *dest, items[i])) return false;
            }
            return true;
    }

    return false;
}

bool bm_record_raw_property(Arena *arena,
                            BM_Raw_Property_Record **records,
                            String_View name,
                            Event_Property_Mutate_Op op,
                            uint32_t flags,
                            const String_View *items,
                            size_t item_count,
                            BM_Provenance provenance) {
    if (!arena || !records) return false;

    BM_Raw_Property_Record record = {0};
    if (!bm_copy_string(arena, name, &record.name)) return false;
    record.op = op;
    record.flags = flags;
    record.provenance = provenance;
    for (size_t i = 0; i < item_count; ++i) {
        String_View owned = {0};
        if (!bm_copy_string(arena, items[i], &owned)) return false;
        if (!arena_arr_push(arena, record.items, owned)) return false;
    }
    return arena_arr_push(arena, *records, record);
}

BM_Builder *bm_builder_create(Arena *arena, Diag_Sink *sink) {
    if (!arena) return NULL;

    BM_Builder *builder = arena_alloc_zero(arena, sizeof(*builder));
    Build_Model_Draft *draft = arena_alloc_zero(arena, sizeof(*draft));
    if (!builder || !draft) return NULL;

    draft->arena = arena;
    draft->sink = sink;
    draft->root_directory_id = BM_DIRECTORY_ID_INVALID;

    builder->arena = arena;
    builder->sink = sink;
    builder->draft = draft;
    return builder;
}

bool bm_builder_apply_event(BM_Builder *builder, const Event *ev) {
    if (!builder || !ev || builder->has_fatal_error) return false;
    if (!event_kind_has_role(ev->h.kind, EVENT_ROLE_BUILD_SEMANTIC)) return true;
    if (!bm_is_supported_build_event(ev->h.kind)) {
        return bm_builder_error(builder,
                                ev,
                                "unsupported build-semantic event kind",
                                "extend src_v2/build_model handlers for this event");
    }

    builder->draft->has_semantic_entities = true;

    switch (ev->h.kind) {
        case EVENT_PROJECT_DECLARE:
        case EVENT_PROJECT_MINIMUM_REQUIRED:
            return bm_builder_handle_project_event(builder, ev);

        case EVENT_DIRECTORY_ENTER:
        case EVENT_DIRECTORY_LEAVE:
        case EVENT_DIRECTORY_PROPERTY_MUTATE:
        case EVENT_GLOBAL_PROPERTY_MUTATE:
            return bm_builder_handle_directory_event(builder, ev);

        case EVENT_TARGET_DECLARE:
        case EVENT_TARGET_ADD_SOURCE:
        case EVENT_TARGET_FILE_SET_DECLARE:
        case EVENT_TARGET_FILE_SET_ADD_BASE_DIR:
        case EVENT_SOURCE_MARK_GENERATED:
        case EVENT_SOURCE_PROPERTY_MUTATE:
        case EVENT_TARGET_ADD_DEPENDENCY:
        case EVENT_BUILD_STEP_DECLARE:
        case EVENT_BUILD_STEP_ADD_OUTPUT:
        case EVENT_BUILD_STEP_ADD_BYPRODUCT:
        case EVENT_BUILD_STEP_ADD_DEPENDENCY:
        case EVENT_BUILD_STEP_ADD_COMMAND:
        case EVENT_TARGET_PROP_SET:
        case EVENT_TARGET_LINK_LIBRARIES:
        case EVENT_TARGET_LINK_OPTIONS:
        case EVENT_TARGET_LINK_DIRECTORIES:
        case EVENT_TARGET_INCLUDE_DIRECTORIES:
        case EVENT_TARGET_COMPILE_DEFINITIONS:
        case EVENT_TARGET_COMPILE_OPTIONS:
            if (ev->h.kind == EVENT_SOURCE_MARK_GENERATED ||
                ev->h.kind == EVENT_SOURCE_PROPERTY_MUTATE ||
                ev->h.kind == EVENT_BUILD_STEP_DECLARE ||
                ev->h.kind == EVENT_BUILD_STEP_ADD_OUTPUT ||
                ev->h.kind == EVENT_BUILD_STEP_ADD_BYPRODUCT ||
                ev->h.kind == EVENT_BUILD_STEP_ADD_DEPENDENCY ||
                ev->h.kind == EVENT_BUILD_STEP_ADD_COMMAND) {
                return bm_builder_handle_build_graph_event(builder, ev);
            }
            return bm_builder_handle_target_event(builder, ev);

        case EVENT_REPLAY_ACTION_DECLARE:
        case EVENT_REPLAY_ACTION_ADD_INPUT:
        case EVENT_REPLAY_ACTION_ADD_OUTPUT:
        case EVENT_REPLAY_ACTION_ADD_ARGV:
        case EVENT_REPLAY_ACTION_ADD_ENV:
            return bm_builder_handle_replay_event(builder, ev);

        case EVENT_TEST_ENABLE:
        case EVENT_TEST_ADD:
            return bm_builder_handle_test_event(builder, ev);

        case EVENT_INSTALL_RULE_ADD:
            return bm_builder_handle_install_event(builder, ev);

        case EVENT_EXPORT_INSTALL:
        case EVENT_EXPORT_BUILD_DECLARE:
        case EVENT_EXPORT_BUILD_ADD_TARGET:
        case EVENT_EXPORT_PACKAGE_REGISTRY:
            return bm_builder_handle_export_event(builder, ev);

        case EVENT_PACKAGE_FIND_RESULT:
        case EVENT_CPACK_ADD_INSTALL_TYPE:
        case EVENT_CPACK_ADD_COMPONENT_GROUP:
        case EVENT_CPACK_ADD_COMPONENT:
        case EVENT_CPACK_PACKAGE_DECLARE:
        case EVENT_CPACK_PACKAGE_ADD_GENERATOR:
            return bm_builder_handle_package_event(builder, ev);

        case EVENT_KIND_COUNT:
            break;
        default:
            break;
    }

    return bm_builder_error(builder, ev, "unexpected builder dispatch fallthrough", "fix build model event dispatch");
}

bool bm_builder_apply_stream(BM_Builder *builder, const Event_Stream *stream) {
    if (!builder || !stream) return false;

    Event_Stream_Iterator it = event_stream_iter(stream);
    while (event_stream_next(&it)) {
        if (!bm_builder_apply_event(builder, it.current)) return false;
    }
    return true;
}

const Build_Model_Draft *bm_builder_finalize(BM_Builder *builder) {
    if (!builder || builder->has_fatal_error) return NULL;
    if (arena_arr_len(builder->directory_stack) != 0) {
        builder->has_fatal_error = true;
        bm_diag_error(builder->sink,
                      bm_empty_provenance(),
                      "build_model_builder",
                      "finalize",
                      "directory stack not empty at end of stream",
                      "ensure every EVENT_DIRECTORY_ENTER has a matching EVENT_DIRECTORY_LEAVE");
        return NULL;
    }
    return builder->draft;
}

bool bm_builder_has_fatal_error(const BM_Builder *builder) {
    return builder ? builder->has_fatal_error : true;
}

Build_Model_Builder *builder_create(Arena *arena, void *diagnostics) {
    return (Build_Model_Builder *)bm_builder_create(arena, (Diag_Sink *)diagnostics);
}

bool builder_apply_event(Build_Model_Builder *builder, const Cmake_Event *ev) {
    return bm_builder_apply_event((BM_Builder *)builder, (const Event *)ev);
}

bool builder_apply_stream(Build_Model_Builder *builder, const Cmake_Event_Stream *stream) {
    return bm_builder_apply_stream((BM_Builder *)builder, (const Event_Stream *)stream);
}

const Build_Model_Draft *builder_finalize(Build_Model_Builder *builder) {
    return bm_builder_finalize((BM_Builder *)builder);
}

bool builder_has_fatal_error(const Build_Model_Builder *builder) {
    return bm_builder_has_fatal_error((const BM_Builder *)builder);
}
