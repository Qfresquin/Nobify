#include "event_ir.h"

#include <stdio.h>
#include <string.h>

#include "arena_dyn.h"

static bool event_copy_sv_inplace(Arena *arena, String_View *sv) {
    if (!arena || !sv) return false;
    if (!sv->data || sv->count == 0) return true;

    char *copy = arena_alloc(arena, sv->count + 1);
    if (!copy) return false;

    memcpy(copy, sv->data, sv->count);
    copy[sv->count] = '\0';
    *sv = nob_sv_from_parts(copy, sv->count);
    return true;
}

static bool event_copy_sv_array_inplace(Arena *arena, String_View **items, size_t count) {
    if (!arena || !items) return false;
    if (!*items || count == 0) {
        *items = NULL;
        return true;
    }

    String_View *copy = arena_alloc_array(arena, String_View, count);
    if (!copy) return false;

    for (size_t i = 0; i < count; ++i) {
        copy[i] = (*items)[i];
        if (!event_copy_sv_inplace(arena, &copy[i])) return false;
    }

    *items = copy;
    return true;
}

static bool event_copy_property_mutate_inplace(Arena *arena, Event_Directory_Property_Mutate *mut) {
    if (!arena || !mut) return false;
    if (!event_copy_sv_inplace(arena, &mut->property_name)) return false;
    if (!event_copy_sv_array_inplace(arena, &mut->items, mut->item_count)) return false;
    return true;
}

static const char *event_var_target_name(Event_Var_Target_Kind target_kind) {
    switch (target_kind) {
        case EVENT_VAR_TARGET_CURRENT: return "current";
        case EVENT_VAR_TARGET_CACHE: return "cache";
        case EVENT_VAR_TARGET_ENV: return "env";
    }
    return "unknown";
}

static const char *event_command_dispatch_name(Event_Command_Dispatch_Kind kind) {
    switch (kind) {
        case EVENT_COMMAND_DISPATCH_BUILTIN: return "builtin";
        case EVENT_COMMAND_DISPATCH_FUNCTION: return "function";
        case EVENT_COMMAND_DISPATCH_MACRO: return "macro";
        case EVENT_COMMAND_DISPATCH_UNKNOWN: return "unknown";
    }
    return "unknown";
}

static const char *event_command_status_name(Event_Command_Status status) {
    switch (status) {
        case EVENT_COMMAND_STATUS_SUCCESS: return "success";
        case EVENT_COMMAND_STATUS_ERROR: return "error";
        case EVENT_COMMAND_STATUS_UNSUPPORTED: return "unsupported";
    }
    return "unknown";
}

static const char *event_property_mutate_op_name(Event_Property_Mutate_Op op) {
    switch (op) {
        case EVENT_PROPERTY_MUTATE_SET: return "set";
        case EVENT_PROPERTY_MUTATE_APPEND_LIST: return "append_list";
        case EVENT_PROPERTY_MUTATE_APPEND_STRING: return "append_string";
        case EVENT_PROPERTY_MUTATE_PREPEND_LIST: return "prepend_list";
    }
    return "unknown";
}

static bool event_deep_copy_payload(Arena *arena, Event *ev) {
    if (!arena || !ev) return false;
    if (!event_copy_sv_inplace(arena, &ev->h.origin.file_path)) return false;

    switch (ev->h.kind) {
        case EVENT_DIAG:
            if (!event_copy_sv_inplace(arena, &ev->as.diag.component)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.command)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.code)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.error_class)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.cause)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.hint)) return false;
            break;

        case EVENT_COMMAND_BEGIN:
            if (!event_copy_sv_inplace(arena, &ev->as.command_begin.command_name)) return false;
            break;
        case EVENT_COMMAND_END:
            if (!event_copy_sv_inplace(arena, &ev->as.command_end.command_name)) return false;
            break;

        case EVENT_INCLUDE_BEGIN:
            if (!event_copy_sv_inplace(arena, &ev->as.include_begin.path)) return false;
            break;
        case EVENT_INCLUDE_END:
            if (!event_copy_sv_inplace(arena, &ev->as.include_end.path)) return false;
            break;
        case EVENT_ADD_SUBDIRECTORY_BEGIN:
            if (!event_copy_sv_inplace(arena, &ev->as.add_subdirectory_begin.source_dir)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.add_subdirectory_begin.binary_dir)) return false;
            break;
        case EVENT_ADD_SUBDIRECTORY_END:
            if (!event_copy_sv_inplace(arena, &ev->as.add_subdirectory_end.source_dir)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.add_subdirectory_end.binary_dir)) return false;
            break;
        case EVENT_CMAKE_LANGUAGE_CALL:
            if (!event_copy_sv_inplace(arena, &ev->as.cmake_language_call.command_name)) return false;
            break;
        case EVENT_CMAKE_LANGUAGE_EVAL:
            if (!event_copy_sv_inplace(arena, &ev->as.cmake_language_eval.code)) return false;
            break;
        case EVENT_CMAKE_LANGUAGE_DEFER_QUEUE:
            if (!event_copy_sv_inplace(arena, &ev->as.cmake_language_defer_queue.defer_id)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cmake_language_defer_queue.command_name)) return false;
            break;

        case EVENT_DIRECTORY_ENTER:
            if (!event_copy_sv_inplace(arena, &ev->as.directory_enter.source_dir)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.directory_enter.binary_dir)) return false;
            break;
        case EVENT_DIRECTORY_LEAVE:
            if (!event_copy_sv_inplace(arena, &ev->as.directory_leave.source_dir)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.directory_leave.binary_dir)) return false;
            break;
        case EVENT_DIRECTORY_PROPERTY_MUTATE:
            if (!event_copy_property_mutate_inplace(arena, &ev->as.directory_property_mutate)) return false;
            break;
        case EVENT_GLOBAL_PROPERTY_MUTATE:
            if (!event_copy_property_mutate_inplace(arena, &ev->as.global_property_mutate)) return false;
            break;

        case EVENT_VAR_SET:
            if (!event_copy_sv_inplace(arena, &ev->as.var_set.key)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.var_set.value)) return false;
            break;
        case EVENT_VAR_UNSET:
            if (!event_copy_sv_inplace(arena, &ev->as.var_unset.key)) return false;
            break;

        case EVENT_SCOPE_PUSH:
        case EVENT_SCOPE_POP:
        case EVENT_POLICY_PUSH:
        case EVENT_POLICY_POP:
        case EVENT_FLOW_IF_EVAL:
        case EVENT_FLOW_BREAK:
        case EVENT_FLOW_CONTINUE:
        case EVENT_FLOW_DEFER_FLUSH:
        case EVENT_FLOW_BLOCK_BEGIN:
        case EVENT_FLOW_BLOCK_END:
        case EVENT_TEST_ENABLE:
            break;

        case EVENT_POLICY_SET:
            if (!event_copy_sv_inplace(arena, &ev->as.policy_set.policy_id)) return false;
            break;

        case EVENT_FLOW_RETURN:
            if (!event_copy_sv_array_inplace(arena,
                                             &ev->as.flow_return.propagate_vars,
                                             ev->as.flow_return.propagate_count)) return false;
            break;
        case EVENT_FLOW_BRANCH_TAKEN:
            if (!event_copy_sv_inplace(arena, &ev->as.flow_branch_taken.branch_kind)) return false;
            break;
        case EVENT_FLOW_LOOP_BEGIN:
            if (!event_copy_sv_inplace(arena, &ev->as.flow_loop_begin.loop_kind)) return false;
            break;
        case EVENT_FLOW_LOOP_END:
            if (!event_copy_sv_inplace(arena, &ev->as.flow_loop_end.loop_kind)) return false;
            break;
        case EVENT_FLOW_DEFER_QUEUE:
            if (!event_copy_sv_inplace(arena, &ev->as.flow_defer_queue.defer_id)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.flow_defer_queue.command_name)) return false;
            break;
        case EVENT_FLOW_FUNCTION_BEGIN:
            if (!event_copy_sv_inplace(arena, &ev->as.flow_function_begin.name)) return false;
            break;
        case EVENT_FLOW_FUNCTION_END:
            if (!event_copy_sv_inplace(arena, &ev->as.flow_function_end.name)) return false;
            break;
        case EVENT_FLOW_MACRO_BEGIN:
            if (!event_copy_sv_inplace(arena, &ev->as.flow_macro_begin.name)) return false;
            break;
        case EVENT_FLOW_MACRO_END:
            if (!event_copy_sv_inplace(arena, &ev->as.flow_macro_end.name)) return false;
            break;

        case EVENT_FS_WRITE_FILE:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_write_file.path)) return false;
            break;
        case EVENT_FS_APPEND_FILE:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_append_file.path)) return false;
            break;
        case EVENT_FS_READ_FILE:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_read_file.path)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.fs_read_file.out_var)) return false;
            break;
        case EVENT_FS_GLOB:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_glob.out_var)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.fs_glob.base_dir)) return false;
            break;
        case EVENT_FS_MKDIR:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_mkdir.path)) return false;
            break;
        case EVENT_FS_REMOVE:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_remove.path)) return false;
            break;
        case EVENT_FS_COPY:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_copy.source)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.fs_copy.destination)) return false;
            break;
        case EVENT_FS_RENAME:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_rename.source)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.fs_rename.destination)) return false;
            break;
        case EVENT_FS_CREATE_LINK:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_create_link.source)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.fs_create_link.destination)) return false;
            break;
        case EVENT_FS_CHMOD:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_chmod.path)) return false;
            break;
        case EVENT_FS_ARCHIVE_CREATE:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_archive_create.path)) return false;
            break;
        case EVENT_FS_ARCHIVE_EXTRACT:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_archive_extract.path)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.fs_archive_extract.destination)) return false;
            break;
        case EVENT_FS_TRANSFER_DOWNLOAD:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_transfer_download.source)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.fs_transfer_download.destination)) return false;
            break;
        case EVENT_FS_TRANSFER_UPLOAD:
            if (!event_copy_sv_inplace(arena, &ev->as.fs_transfer_upload.source)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.fs_transfer_upload.destination)) return false;
            break;

        case EVENT_PROC_EXEC_REQUEST:
            if (!event_copy_sv_inplace(arena, &ev->as.proc_exec_request.command)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.proc_exec_request.working_directory)) return false;
            break;
        case EVENT_PROC_EXEC_RESULT:
            if (!event_copy_sv_inplace(arena, &ev->as.proc_exec_result.command)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.proc_exec_result.result_code)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.proc_exec_result.stdout_text)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.proc_exec_result.stderr_text)) return false;
            break;

        case EVENT_STRING_REPLACE:
            if (!event_copy_sv_inplace(arena, &ev->as.string_replace.out_var)) return false;
            break;
        case EVENT_STRING_CONFIGURE:
            if (!event_copy_sv_inplace(arena, &ev->as.string_configure.out_var)) return false;
            break;
        case EVENT_STRING_REGEX:
            if (!event_copy_sv_inplace(arena, &ev->as.string_regex.mode)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.string_regex.out_var)) return false;
            break;
        case EVENT_STRING_HASH:
            if (!event_copy_sv_inplace(arena, &ev->as.string_hash.algorithm)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.string_hash.out_var)) return false;
            break;
        case EVENT_STRING_TIMESTAMP:
            if (!event_copy_sv_inplace(arena, &ev->as.string_timestamp.out_var)) return false;
            break;

        case EVENT_LIST_APPEND:
            if (!event_copy_sv_inplace(arena, &ev->as.list_append.list_var)) return false;
            break;
        case EVENT_LIST_PREPEND:
            if (!event_copy_sv_inplace(arena, &ev->as.list_prepend.list_var)) return false;
            break;
        case EVENT_LIST_INSERT:
            if (!event_copy_sv_inplace(arena, &ev->as.list_insert.list_var)) return false;
            break;
        case EVENT_LIST_REMOVE:
            if (!event_copy_sv_inplace(arena, &ev->as.list_remove.list_var)) return false;
            break;
        case EVENT_LIST_TRANSFORM:
            if (!event_copy_sv_inplace(arena, &ev->as.list_transform.list_var)) return false;
            break;
        case EVENT_LIST_SORT:
            if (!event_copy_sv_inplace(arena, &ev->as.list_sort.list_var)) return false;
            break;

        case EVENT_MATH_EXPR:
            if (!event_copy_sv_inplace(arena, &ev->as.math_expr.out_var)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.math_expr.format)) return false;
            break;
        case EVENT_PATH_NORMALIZE:
            if (!event_copy_sv_inplace(arena, &ev->as.path_normalize.out_var)) return false;
            break;
        case EVENT_PATH_COMPARE:
            if (!event_copy_sv_inplace(arena, &ev->as.path_compare.out_var)) return false;
            break;
        case EVENT_PATH_CONVERT:
            if (!event_copy_sv_inplace(arena, &ev->as.path_convert.out_var)) return false;
            break;

        case EVENT_TEST_ADD:
            if (!event_copy_sv_inplace(arena, &ev->as.test_add.name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.test_add.command)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.test_add.working_dir)) return false;
            break;
        case EVENT_INSTALL_RULE_ADD:
            if (!event_copy_sv_inplace(arena, &ev->as.install_rule_add.item)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.install_rule_add.destination)) return false;
            break;
        case EVENT_CPACK_ADD_INSTALL_TYPE:
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_install_type.name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_install_type.display_name)) return false;
            break;
        case EVENT_CPACK_ADD_COMPONENT_GROUP:
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component_group.name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component_group.display_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component_group.description)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component_group.parent_group)) return false;
            break;
        case EVENT_CPACK_ADD_COMPONENT:
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component.name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component.display_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component.description)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component.group)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component.depends)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component.install_types)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component.archive_file)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.cpack_add_component.plist)) return false;
            break;
        case EVENT_PACKAGE_FIND_RESULT:
            if (!event_copy_sv_inplace(arena, &ev->as.package_find_result.package_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.package_find_result.mode)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.package_find_result.found_path)) return false;
            break;
        case EVENT_PROJECT_DECLARE:
            if (!event_copy_sv_inplace(arena, &ev->as.project_declare.name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.project_declare.version)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.project_declare.description)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.project_declare.homepage_url)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.project_declare.languages)) return false;
            break;
        case EVENT_PROJECT_MINIMUM_REQUIRED:
            if (!event_copy_sv_inplace(arena, &ev->as.project_minimum_required.version)) return false;
            break;
        case EVENT_TARGET_DECLARE:
            if (!event_copy_sv_inplace(arena, &ev->as.target_declare.name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_declare.alias_of)) return false;
            break;
        case EVENT_TARGET_ADD_SOURCE:
            if (!event_copy_sv_inplace(arena, &ev->as.target_add_source.target_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_add_source.path)) return false;
            break;
        case EVENT_TARGET_ADD_DEPENDENCY:
            if (!event_copy_sv_inplace(arena, &ev->as.target_add_dependency.target_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_add_dependency.dependency_name)) return false;
            break;
        case EVENT_TARGET_PROP_SET:
            if (!event_copy_sv_inplace(arena, &ev->as.target_prop_set.target_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_prop_set.key)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_prop_set.value)) return false;
            break;
        case EVENT_TARGET_LINK_LIBRARIES:
            if (!event_copy_sv_inplace(arena, &ev->as.target_link_libraries.target_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_link_libraries.item)) return false;
            break;
        case EVENT_TARGET_LINK_OPTIONS:
            if (!event_copy_sv_inplace(arena, &ev->as.target_link_options.target_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_link_options.item)) return false;
            break;
        case EVENT_TARGET_LINK_DIRECTORIES:
            if (!event_copy_sv_inplace(arena, &ev->as.target_link_directories.target_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_link_directories.path)) return false;
            break;
        case EVENT_TARGET_INCLUDE_DIRECTORIES:
            if (!event_copy_sv_inplace(arena, &ev->as.target_include_directories.target_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_include_directories.path)) return false;
            break;
        case EVENT_TARGET_COMPILE_DEFINITIONS:
            if (!event_copy_sv_inplace(arena, &ev->as.target_compile_definitions.target_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_compile_definitions.item)) return false;
            break;
        case EVENT_TARGET_COMPILE_OPTIONS:
            if (!event_copy_sv_inplace(arena, &ev->as.target_compile_options.target_name)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.target_compile_options.item)) return false;
            break;
        case EVENT_KIND_COUNT:
            return false;
    }

    return true;
}

static const char *const k_event_family_names[EVENT_FAMILY_COUNT] = {
#define DEFINE_EVENT_FAMILY_NAME(kind, label) [kind] = label,
    EVENT_FAMILY_LIST(DEFINE_EVENT_FAMILY_NAME)
#undef DEFINE_EVENT_FAMILY_NAME
};

static const Event_Kind_Meta k_event_kind_meta[] = {
#define DEFINE_EVENT_KIND_META(kind, family, label, roles) \
    [kind] = {kind, family, label, (uint32_t)(roles), 1},
    EVENT_KIND_LIST(DEFINE_EVENT_KIND_META)
#undef DEFINE_EVENT_KIND_META
};

_Static_assert(sizeof(k_event_family_names) / sizeof(k_event_family_names[0]) == EVENT_FAMILY_COUNT,
               "event family table must match Event_Family");
_Static_assert(sizeof(k_event_kind_meta) / sizeof(k_event_kind_meta[0]) == EVENT_KIND_COUNT,
               "event kind metadata must match Event_Kind");

Event_Stream *event_stream_create(Arena *arena) {
    if (!arena) return NULL;
    Event_Stream *stream = arena_alloc_zero(arena, sizeof(Event_Stream));
    if (!stream) return NULL;
    stream->arena = arena;
    stream->count = 0;
    stream->next_seq = 1;
    return stream;
}

bool event_stream_push(Event_Stream *stream, const Event *src) {
    if (!stream || !stream->arena || !src) return false;

    Event ev = *src;
    const Event_Kind_Meta *meta = event_kind_meta(ev.h.kind);
    if (!meta) return false;
    if (ev.h.version == 0) {
        ev.h.version = meta->default_version;
    }
    if (ev.h.seq == 0) {
        ev.h.seq = stream->next_seq;
    }

    if (!event_deep_copy_payload(stream->arena, &ev)) return false;
    if (!arena_arr_push(stream->arena, stream->items, ev)) return false;

    stream->count = arena_arr_len(stream->items);
    if (stream->next_seq <= ev.h.seq) {
        stream->next_seq = ev.h.seq + 1;
    }
    return true;
}

Event_Stream_Iterator event_stream_iter(const Event_Stream *stream) {
    Event_Stream_Iterator it = {0};
    it.stream = stream;
    return it;
}

bool event_stream_next(Event_Stream_Iterator *it) {
    if (!it || !it->stream) return false;
    if (it->index >= it->stream->count) {
        it->current = NULL;
        return false;
    }

    it->current = &it->stream->items[it->index++];
    return true;
}

const Event_Kind_Meta *event_kind_meta(Event_Kind kind) {
    size_t count = sizeof(k_event_kind_meta) / sizeof(k_event_kind_meta[0]);
    if ((size_t)kind >= count) return NULL;
    if (k_event_kind_meta[kind].label == NULL) return NULL;
    return &k_event_kind_meta[kind];
}

bool event_kind_has_role(Event_Kind kind, Event_Role role) {
    const Event_Kind_Meta *meta = event_kind_meta(kind);
    if (!meta) return false;
    return (meta->role_mask & (uint32_t)role) != 0;
}

uint32_t event_kind_role_mask(Event_Kind kind) {
    const Event_Kind_Meta *meta = event_kind_meta(kind);
    return meta ? meta->role_mask : 0;
}

Event_Family event_kind_family(Event_Kind kind) {
    const Event_Kind_Meta *meta = event_kind_meta(kind);
    return meta ? meta->family : EVENT_FAMILY_COUNT;
}

const char *event_family_name(Event_Family family) {
    if ((size_t)family >= EVENT_FAMILY_COUNT) return "unknown_family";
    return k_event_family_names[family] ? k_event_family_names[family] : "unknown_family";
}

const char *event_kind_name(Event_Kind kind) {
    const Event_Kind_Meta *meta = event_kind_meta(kind);
    return meta ? meta->label : "unknown_event";
}

static void event_dump_one(const Event *ev) {
    if (!ev) return;

    Event_Family family = event_kind_family(ev->h.kind);
    printf("[%llu] %s/%s @ %.*s:%zu:%zu",
           (unsigned long long)ev->h.seq,
           event_family_name(family),
           event_kind_name(ev->h.kind),
           (int)ev->h.origin.file_path.count,
           ev->h.origin.file_path.data ? ev->h.origin.file_path.data : "",
           ev->h.origin.line,
           ev->h.origin.col);

    switch (ev->h.kind) {
        case EVENT_DIAG:
            printf(" severity=%d code=%.*s",
                   (int)ev->as.diag.severity,
                   (int)ev->as.diag.code.count,
                   ev->as.diag.code.data ? ev->as.diag.code.data : "");
            break;

        case EVENT_COMMAND_BEGIN:
            printf(" command=%.*s dispatch=%s argc=%u",
                   (int)ev->as.command_begin.command_name.count,
                   ev->as.command_begin.command_name.data ? ev->as.command_begin.command_name.data : "",
                   event_command_dispatch_name(ev->as.command_begin.dispatch_kind),
                   (unsigned)ev->as.command_begin.argc);
            break;
        case EVENT_COMMAND_END:
            printf(" command=%.*s dispatch=%s argc=%u status=%s",
                   (int)ev->as.command_end.command_name.count,
                   ev->as.command_end.command_name.data ? ev->as.command_end.command_name.data : "",
                   event_command_dispatch_name(ev->as.command_end.dispatch_kind),
                   (unsigned)ev->as.command_end.argc,
                   event_command_status_name(ev->as.command_end.status));
            break;

        case EVENT_DIRECTORY_ENTER:
            printf(" source_dir=%.*s binary_dir=%.*s",
                   (int)ev->as.directory_enter.source_dir.count,
                   ev->as.directory_enter.source_dir.data ? ev->as.directory_enter.source_dir.data : "",
                   (int)ev->as.directory_enter.binary_dir.count,
                   ev->as.directory_enter.binary_dir.data ? ev->as.directory_enter.binary_dir.data : "");
            break;
        case EVENT_DIRECTORY_LEAVE:
            printf(" source_dir=%.*s binary_dir=%.*s",
                   (int)ev->as.directory_leave.source_dir.count,
                   ev->as.directory_leave.source_dir.data ? ev->as.directory_leave.source_dir.data : "",
                   (int)ev->as.directory_leave.binary_dir.count,
                   ev->as.directory_leave.binary_dir.data ? ev->as.directory_leave.binary_dir.data : "");
            break;
        case EVENT_DIRECTORY_PROPERTY_MUTATE:
            printf(" property=%.*s op=%s items=%zu modifiers=0x%x",
                   (int)ev->as.directory_property_mutate.property_name.count,
                   ev->as.directory_property_mutate.property_name.data ? ev->as.directory_property_mutate.property_name.data : "",
                   event_property_mutate_op_name(ev->as.directory_property_mutate.op),
                   ev->as.directory_property_mutate.item_count,
                   (unsigned)ev->as.directory_property_mutate.modifier_flags);
            break;
        case EVENT_GLOBAL_PROPERTY_MUTATE:
            printf(" property=%.*s op=%s items=%zu modifiers=0x%x",
                   (int)ev->as.global_property_mutate.property_name.count,
                   ev->as.global_property_mutate.property_name.data ? ev->as.global_property_mutate.property_name.data : "",
                   event_property_mutate_op_name(ev->as.global_property_mutate.op),
                   ev->as.global_property_mutate.item_count,
                   (unsigned)ev->as.global_property_mutate.modifier_flags);
            break;

        case EVENT_VAR_SET:
            printf(" key=%.*s target=%s",
                   (int)ev->as.var_set.key.count,
                   ev->as.var_set.key.data ? ev->as.var_set.key.data : "",
                   event_var_target_name(ev->as.var_set.target_kind));
            break;
        case EVENT_VAR_UNSET:
            printf(" key=%.*s target=%s",
                   (int)ev->as.var_unset.key.count,
                   ev->as.var_unset.key.data ? ev->as.var_unset.key.data : "",
                   event_var_target_name(ev->as.var_unset.target_kind));
            break;

        case EVENT_PROJECT_DECLARE:
            printf(" name=%.*s",
                   (int)ev->as.project_declare.name.count,
                   ev->as.project_declare.name.data ? ev->as.project_declare.name.data : "");
            break;
        case EVENT_PROJECT_MINIMUM_REQUIRED:
            printf(" version=%.*s",
                   (int)ev->as.project_minimum_required.version.count,
                   ev->as.project_minimum_required.version.data ? ev->as.project_minimum_required.version.data : "");
            break;

        case EVENT_TARGET_DECLARE:
            printf(" name=%.*s",
                   (int)ev->as.target_declare.name.count,
                   ev->as.target_declare.name.data ? ev->as.target_declare.name.data : "");
            break;
        case EVENT_TARGET_ADD_SOURCE:
            printf(" target=%.*s path=%.*s",
                   (int)ev->as.target_add_source.target_name.count,
                   ev->as.target_add_source.target_name.data ? ev->as.target_add_source.target_name.data : "",
                   (int)ev->as.target_add_source.path.count,
                   ev->as.target_add_source.path.data ? ev->as.target_add_source.path.data : "");
            break;
        case EVENT_TARGET_ADD_DEPENDENCY:
            printf(" target=%.*s dep=%.*s",
                   (int)ev->as.target_add_dependency.target_name.count,
                   ev->as.target_add_dependency.target_name.data ? ev->as.target_add_dependency.target_name.data : "",
                   (int)ev->as.target_add_dependency.dependency_name.count,
                   ev->as.target_add_dependency.dependency_name.data ? ev->as.target_add_dependency.dependency_name.data : "");
            break;
        case EVENT_TARGET_PROP_SET:
            printf(" target=%.*s key=%.*s",
                   (int)ev->as.target_prop_set.target_name.count,
                   ev->as.target_prop_set.target_name.data ? ev->as.target_prop_set.target_name.data : "",
                   (int)ev->as.target_prop_set.key.count,
                   ev->as.target_prop_set.key.data ? ev->as.target_prop_set.key.data : "");
            break;
        case EVENT_TARGET_LINK_LIBRARIES:
            printf(" target=%.*s item=%.*s",
                   (int)ev->as.target_link_libraries.target_name.count,
                   ev->as.target_link_libraries.target_name.data ? ev->as.target_link_libraries.target_name.data : "",
                   (int)ev->as.target_link_libraries.item.count,
                   ev->as.target_link_libraries.item.data ? ev->as.target_link_libraries.item.data : "");
            break;
        case EVENT_TARGET_LINK_OPTIONS:
            printf(" target=%.*s item=%.*s",
                   (int)ev->as.target_link_options.target_name.count,
                   ev->as.target_link_options.target_name.data ? ev->as.target_link_options.target_name.data : "",
                   (int)ev->as.target_link_options.item.count,
                   ev->as.target_link_options.item.data ? ev->as.target_link_options.item.data : "");
            break;
        case EVENT_TARGET_LINK_DIRECTORIES:
            printf(" target=%.*s path=%.*s",
                   (int)ev->as.target_link_directories.target_name.count,
                   ev->as.target_link_directories.target_name.data ? ev->as.target_link_directories.target_name.data : "",
                   (int)ev->as.target_link_directories.path.count,
                   ev->as.target_link_directories.path.data ? ev->as.target_link_directories.path.data : "");
            break;
        case EVENT_TARGET_INCLUDE_DIRECTORIES:
            printf(" target=%.*s path=%.*s",
                   (int)ev->as.target_include_directories.target_name.count,
                   ev->as.target_include_directories.target_name.data ? ev->as.target_include_directories.target_name.data : "",
                   (int)ev->as.target_include_directories.path.count,
                   ev->as.target_include_directories.path.data ? ev->as.target_include_directories.path.data : "");
            break;
        case EVENT_TARGET_COMPILE_DEFINITIONS:
            printf(" target=%.*s item=%.*s",
                   (int)ev->as.target_compile_definitions.target_name.count,
                   ev->as.target_compile_definitions.target_name.data ? ev->as.target_compile_definitions.target_name.data : "",
                   (int)ev->as.target_compile_definitions.item.count,
                   ev->as.target_compile_definitions.item.data ? ev->as.target_compile_definitions.item.data : "");
            break;
        case EVENT_TARGET_COMPILE_OPTIONS:
            printf(" target=%.*s item=%.*s",
                   (int)ev->as.target_compile_options.target_name.count,
                   ev->as.target_compile_options.target_name.data ? ev->as.target_compile_options.target_name.data : "",
                   (int)ev->as.target_compile_options.item.count,
                   ev->as.target_compile_options.item.data ? ev->as.target_compile_options.item.data : "");
            break;

        default:
            break;
    }

    putchar('\n');
}

void event_stream_dump(const Event_Stream *stream) {
    if (!stream) return;
    for (size_t i = 0; i < arena_arr_len(stream->items); ++i) {
        event_dump_one(&stream->items[i]);
    }
}
