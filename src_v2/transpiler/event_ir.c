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

static bool event_deep_copy_payload(Arena *arena, Event *ev) {
    if (!arena || !ev) return false;
    if (!event_copy_sv_inplace(arena, &ev->h.origin.file_path)) return false;

    switch ((Event_Kind) ev->h.kind) {
        case EVENT_TRACE_COMMAND_BEGIN:
            if (!event_copy_sv_inplace(arena, &ev->as.trace_command_begin.command_name)) return false;
            if (!event_copy_sv_array_inplace(arena,
                                             &ev->as.trace_command_begin.resolved_args,
                                             ev->as.trace_command_begin.resolved_arg_count)) return false;
            break;

        case EVENT_TRACE_COMMAND_END:
            if (!event_copy_sv_inplace(arena, &ev->as.trace_command_end.command_name)) return false;
            break;

        case EVENT_DIAG:
            if (!event_copy_sv_inplace(arena, &ev->as.diag.component)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.command)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.code)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.error_class)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.cause)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.diag.hint)) return false;
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
            break;

        case EVENT_POLICY_SET:
            if (!event_copy_sv_inplace(arena, &ev->as.policy_set.policy_id)) return false;
            break;

        case EVENT_FLOW_RETURN:
            if (!event_copy_sv_array_inplace(arena,
                                             &ev->as.flow_return.propagate_vars,
                                             ev->as.flow_return.propagate_count)) return false;
            break;
        case EVENT_FLOW_IF_EVAL:
        case EVENT_FLOW_BREAK:
        case EVENT_FLOW_CONTINUE:
        case EVENT_FLOW_DEFER_FLUSH:
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

        case EVENT_TEST_ENABLE:
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

        case EVENT_DIR_PUSH:
            if (!event_copy_sv_inplace(arena, &ev->as.dir_push.source_dir)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.dir_push.binary_dir)) return false;
            break;

        case EVENT_DIR_POP:
            if (!event_copy_sv_inplace(arena, &ev->as.dir_pop.source_dir)) return false;
            if (!event_copy_sv_inplace(arena, &ev->as.dir_pop.binary_dir)) return false;
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
    }

    return true;
}

Event_Stream *event_stream_create(Arena *arena) {
    if (!arena) return NULL;
    return arena_alloc_zero(arena, sizeof(Event_Stream));
}

bool event_stream_push(Arena *event_arena, Event_Stream *stream, Event ev) {
    if (!event_arena || !stream) return false;

    ev.h.family = event_kind_family((Event_Kind) ev.h.kind);
    if (ev.h.version == 0) ev.h.version = 1;

    if (!event_deep_copy_payload(event_arena, &ev)) return false;
    return arena_arr_push(event_arena, stream->items, ev);
}

Event_Stream_Iterator event_stream_iter(const Event_Stream *stream) {
    Event_Stream_Iterator it = {0};
    it.stream = stream;
    return it;
}

bool event_stream_next(Event_Stream_Iterator *it) {
    if (!it || !it->stream) return false;
    if (it->index >= arena_arr_len(it->stream->items)) {
        it->current = NULL;
        return false;
    }

    it->current = &it->stream->items[it->index++];
    return true;
}

Event_Family event_kind_family(Event_Kind kind) {
    switch (kind) {
#define EVENT_KIND_FAMILY_CASE(kind, family, label) case kind: return family;
        EVENT_KIND_LIST(EVENT_KIND_FAMILY_CASE)
#undef EVENT_KIND_FAMILY_CASE
    }

    return EVENT_FAMILY_TRACE;
}

const char *event_family_name(Event_Family family) {
    switch (family) {
#define EVENT_FAMILY_CASE(kind, label) case kind: return label;
        EVENT_FAMILY_LIST(EVENT_FAMILY_CASE)
#undef EVENT_FAMILY_CASE
    }

    return "unknown_family";
}

const char *event_kind_name(Event_Kind kind) {
    switch (kind) {
#define EVENT_KIND_NAME_CASE(kind, family, label) case kind: return label;
        EVENT_KIND_LIST(EVENT_KIND_NAME_CASE)
#undef EVENT_KIND_NAME_CASE
    }

    return "unknown_event";
}

static void event_dump_one(const Event *ev) {
    if (!ev) return;

    printf("[%llu] %s/%s @ %.*s:%zu:%zu",
           (unsigned long long) ev->h.seq,
           event_family_name(ev->h.family),
           event_kind_name((Event_Kind) ev->h.kind),
           (int) ev->h.origin.file_path.count,
           ev->h.origin.file_path.data ? ev->h.origin.file_path.data : "",
           ev->h.origin.line,
           ev->h.origin.col);

    switch ((Event_Kind) ev->h.kind) {
        case EVENT_TRACE_COMMAND_BEGIN:
            printf(" cmd=%.*s argc=%zu",
                   (int) ev->as.trace_command_begin.command_name.count,
                   ev->as.trace_command_begin.command_name.data ? ev->as.trace_command_begin.command_name.data : "",
                   ev->as.trace_command_begin.resolved_arg_count);
            break;

        case EVENT_TRACE_COMMAND_END:
            printf(" cmd=%.*s completed=%s failed=%s",
                   (int) ev->as.trace_command_end.command_name.count,
                   ev->as.trace_command_end.command_name.data ? ev->as.trace_command_end.command_name.data : "",
                   ev->as.trace_command_end.completed ? "true" : "false",
                   ev->as.trace_command_end.failed ? "true" : "false");
            break;

        case EVENT_DIAG:
            printf(" severity=%d code=%.*s",
                   (int) ev->as.diag.severity,
                   (int) ev->as.diag.code.count,
                   ev->as.diag.code.data ? ev->as.diag.code.data : "");
            break;

        case EVENT_VAR_SET:
            printf(" key=%.*s",
                   (int) ev->as.var_set.key.count,
                   ev->as.var_set.key.data ? ev->as.var_set.key.data : "");
            break;

        case EVENT_VAR_UNSET:
            printf(" key=%.*s",
                   (int) ev->as.var_unset.key.count,
                   ev->as.var_unset.key.data ? ev->as.var_unset.key.data : "");
            break;

        case EVENT_SCOPE_PUSH:
        case EVENT_SCOPE_POP:
        case EVENT_POLICY_PUSH:
        case EVENT_POLICY_POP:
            break;

        case EVENT_POLICY_SET:
            printf(" policy=%.*s",
                   (int) ev->as.policy_set.policy_id.count,
                   ev->as.policy_set.policy_id.data ? ev->as.policy_set.policy_id.data : "");
            break;

        case EVENT_FLOW_RETURN:
            printf(" propagate=%zu", ev->as.flow_return.propagate_count);
            break;
        case EVENT_FLOW_IF_EVAL:
            printf(" result=%s", ev->as.flow_if_eval.result ? "true" : "false");
            break;
        case EVENT_FLOW_BRANCH_TAKEN:
            printf(" branch=%.*s",
                   (int) ev->as.flow_branch_taken.branch_kind.count,
                   ev->as.flow_branch_taken.branch_kind.data ? ev->as.flow_branch_taken.branch_kind.data : "");
            break;
        case EVENT_FLOW_LOOP_BEGIN:
            printf(" loop=%.*s",
                   (int) ev->as.flow_loop_begin.loop_kind.count,
                   ev->as.flow_loop_begin.loop_kind.data ? ev->as.flow_loop_begin.loop_kind.data : "");
            break;
        case EVENT_FLOW_LOOP_END:
            printf(" iterations=%u", (unsigned) ev->as.flow_loop_end.iterations);
            break;
        case EVENT_FLOW_BREAK:
            printf(" depth=%u", (unsigned) ev->as.flow_break.loop_depth);
            break;
        case EVENT_FLOW_CONTINUE:
            printf(" depth=%u", (unsigned) ev->as.flow_continue.loop_depth);
            break;
        case EVENT_FLOW_DEFER_QUEUE:
            printf(" command=%.*s",
                   (int) ev->as.flow_defer_queue.command_name.count,
                   ev->as.flow_defer_queue.command_name.data ? ev->as.flow_defer_queue.command_name.data : "");
            break;
        case EVENT_FLOW_DEFER_FLUSH:
            printf(" count=%u", (unsigned) ev->as.flow_defer_flush.call_count);
            break;

        case EVENT_FS_WRITE_FILE:
            printf(" path=%.*s",
                   (int) ev->as.fs_write_file.path.count,
                   ev->as.fs_write_file.path.data ? ev->as.fs_write_file.path.data : "");
            break;
        case EVENT_FS_APPEND_FILE:
            printf(" path=%.*s",
                   (int) ev->as.fs_append_file.path.count,
                   ev->as.fs_append_file.path.data ? ev->as.fs_append_file.path.data : "");
            break;
        case EVENT_FS_READ_FILE:
            printf(" path=%.*s",
                   (int) ev->as.fs_read_file.path.count,
                   ev->as.fs_read_file.path.data ? ev->as.fs_read_file.path.data : "");
            break;
        case EVENT_FS_GLOB:
            printf(" base=%.*s",
                   (int) ev->as.fs_glob.base_dir.count,
                   ev->as.fs_glob.base_dir.data ? ev->as.fs_glob.base_dir.data : "");
            break;
        case EVENT_FS_MKDIR:
            printf(" path=%.*s",
                   (int) ev->as.fs_mkdir.path.count,
                   ev->as.fs_mkdir.path.data ? ev->as.fs_mkdir.path.data : "");
            break;
        case EVENT_FS_REMOVE:
            printf(" path=%.*s",
                   (int) ev->as.fs_remove.path.count,
                   ev->as.fs_remove.path.data ? ev->as.fs_remove.path.data : "");
            break;
        case EVENT_FS_COPY:
            printf(" src=%.*s",
                   (int) ev->as.fs_copy.source.count,
                   ev->as.fs_copy.source.data ? ev->as.fs_copy.source.data : "");
            break;
        case EVENT_FS_RENAME:
            printf(" src=%.*s",
                   (int) ev->as.fs_rename.source.count,
                   ev->as.fs_rename.source.data ? ev->as.fs_rename.source.data : "");
            break;
        case EVENT_FS_CREATE_LINK:
            printf(" src=%.*s",
                   (int) ev->as.fs_create_link.source.count,
                   ev->as.fs_create_link.source.data ? ev->as.fs_create_link.source.data : "");
            break;
        case EVENT_FS_CHMOD:
            printf(" path=%.*s",
                   (int) ev->as.fs_chmod.path.count,
                   ev->as.fs_chmod.path.data ? ev->as.fs_chmod.path.data : "");
            break;
        case EVENT_FS_ARCHIVE_CREATE:
            printf(" path=%.*s",
                   (int) ev->as.fs_archive_create.path.count,
                   ev->as.fs_archive_create.path.data ? ev->as.fs_archive_create.path.data : "");
            break;
        case EVENT_FS_ARCHIVE_EXTRACT:
            printf(" path=%.*s",
                   (int) ev->as.fs_archive_extract.path.count,
                   ev->as.fs_archive_extract.path.data ? ev->as.fs_archive_extract.path.data : "");
            break;
        case EVENT_FS_TRANSFER_DOWNLOAD:
            printf(" src=%.*s",
                   (int) ev->as.fs_transfer_download.source.count,
                   ev->as.fs_transfer_download.source.data ? ev->as.fs_transfer_download.source.data : "");
            break;
        case EVENT_FS_TRANSFER_UPLOAD:
            printf(" src=%.*s",
                   (int) ev->as.fs_transfer_upload.source.count,
                   ev->as.fs_transfer_upload.source.data ? ev->as.fs_transfer_upload.source.data : "");
            break;
        case EVENT_PROC_EXEC_REQUEST:
            printf(" cmd=%.*s",
                   (int) ev->as.proc_exec_request.command.count,
                   ev->as.proc_exec_request.command.data ? ev->as.proc_exec_request.command.data : "");
            break;
        case EVENT_PROC_EXEC_RESULT:
            printf(" cmd=%.*s error=%s",
                   (int) ev->as.proc_exec_result.command.count,
                   ev->as.proc_exec_result.command.data ? ev->as.proc_exec_result.command.data : "",
                   ev->as.proc_exec_result.had_error ? "true" : "false");
            break;
        case EVENT_STRING_REPLACE:
            printf(" out=%.*s",
                   (int) ev->as.string_replace.out_var.count,
                   ev->as.string_replace.out_var.data ? ev->as.string_replace.out_var.data : "");
            break;
        case EVENT_STRING_CONFIGURE:
            printf(" out=%.*s",
                   (int) ev->as.string_configure.out_var.count,
                   ev->as.string_configure.out_var.data ? ev->as.string_configure.out_var.data : "");
            break;
        case EVENT_STRING_REGEX:
            printf(" mode=%.*s",
                   (int) ev->as.string_regex.mode.count,
                   ev->as.string_regex.mode.data ? ev->as.string_regex.mode.data : "");
            break;
        case EVENT_STRING_HASH:
            printf(" algo=%.*s",
                   (int) ev->as.string_hash.algorithm.count,
                   ev->as.string_hash.algorithm.data ? ev->as.string_hash.algorithm.data : "");
            break;
        case EVENT_STRING_TIMESTAMP:
            printf(" out=%.*s",
                   (int) ev->as.string_timestamp.out_var.count,
                   ev->as.string_timestamp.out_var.data ? ev->as.string_timestamp.out_var.data : "");
            break;
        case EVENT_LIST_APPEND:
            printf(" list=%.*s",
                   (int) ev->as.list_append.list_var.count,
                   ev->as.list_append.list_var.data ? ev->as.list_append.list_var.data : "");
            break;
        case EVENT_LIST_PREPEND:
            printf(" list=%.*s",
                   (int) ev->as.list_prepend.list_var.count,
                   ev->as.list_prepend.list_var.data ? ev->as.list_prepend.list_var.data : "");
            break;
        case EVENT_LIST_INSERT:
            printf(" list=%.*s",
                   (int) ev->as.list_insert.list_var.count,
                   ev->as.list_insert.list_var.data ? ev->as.list_insert.list_var.data : "");
            break;
        case EVENT_LIST_REMOVE:
            printf(" list=%.*s",
                   (int) ev->as.list_remove.list_var.count,
                   ev->as.list_remove.list_var.data ? ev->as.list_remove.list_var.data : "");
            break;
        case EVENT_LIST_TRANSFORM:
            printf(" list=%.*s",
                   (int) ev->as.list_transform.list_var.count,
                   ev->as.list_transform.list_var.data ? ev->as.list_transform.list_var.data : "");
            break;
        case EVENT_LIST_SORT:
            printf(" list=%.*s",
                   (int) ev->as.list_sort.list_var.count,
                   ev->as.list_sort.list_var.data ? ev->as.list_sort.list_var.data : "");
            break;
        case EVENT_MATH_EXPR:
            printf(" out=%.*s",
                   (int) ev->as.math_expr.out_var.count,
                   ev->as.math_expr.out_var.data ? ev->as.math_expr.out_var.data : "");
            break;
        case EVENT_PATH_NORMALIZE:
            printf(" out=%.*s",
                   (int) ev->as.path_normalize.out_var.count,
                   ev->as.path_normalize.out_var.data ? ev->as.path_normalize.out_var.data : "");
            break;
        case EVENT_PATH_COMPARE:
            printf(" out=%.*s",
                   (int) ev->as.path_compare.out_var.count,
                   ev->as.path_compare.out_var.data ? ev->as.path_compare.out_var.data : "");
            break;
        case EVENT_PATH_CONVERT:
            printf(" out=%.*s",
                   (int) ev->as.path_convert.out_var.count,
                   ev->as.path_convert.out_var.data ? ev->as.path_convert.out_var.data : "");
            break;

        case EVENT_TEST_ENABLE:
            printf(" enabled=%s", ev->as.test_enable.enabled ? "true" : "false");
            break;

        case EVENT_TEST_ADD:
            printf(" name=%.*s",
                   (int) ev->as.test_add.name.count,
                   ev->as.test_add.name.data ? ev->as.test_add.name.data : "");
            break;

        case EVENT_INSTALL_RULE_ADD:
            printf(" rule=%d", (int) ev->as.install_rule_add.rule_type);
            break;

        case EVENT_CPACK_ADD_INSTALL_TYPE:
            printf(" name=%.*s",
                   (int) ev->as.cpack_add_install_type.name.count,
                   ev->as.cpack_add_install_type.name.data ? ev->as.cpack_add_install_type.name.data : "");
            break;

        case EVENT_CPACK_ADD_COMPONENT_GROUP:
            printf(" name=%.*s",
                   (int) ev->as.cpack_add_component_group.name.count,
                   ev->as.cpack_add_component_group.name.data ? ev->as.cpack_add_component_group.name.data : "");
            break;

        case EVENT_CPACK_ADD_COMPONENT:
            printf(" name=%.*s",
                   (int) ev->as.cpack_add_component.name.count,
                   ev->as.cpack_add_component.name.data ? ev->as.cpack_add_component.name.data : "");
            break;

        case EVENT_PACKAGE_FIND_RESULT:
            printf(" pkg=%.*s found=%s",
                   (int) ev->as.package_find_result.package_name.count,
                   ev->as.package_find_result.package_name.data ? ev->as.package_find_result.package_name.data : "",
                   ev->as.package_find_result.found ? "true" : "false");
            break;

        case EVENT_PROJECT_DECLARE:
            printf(" name=%.*s",
                   (int) ev->as.project_declare.name.count,
                   ev->as.project_declare.name.data ? ev->as.project_declare.name.data : "");
            break;

        case EVENT_PROJECT_MINIMUM_REQUIRED:
            printf(" version=%.*s",
                   (int) ev->as.project_minimum_required.version.count,
                   ev->as.project_minimum_required.version.data ? ev->as.project_minimum_required.version.data : "");
            break;

        case EVENT_TARGET_DECLARE:
            printf(" name=%.*s",
                   (int) ev->as.target_declare.name.count,
                   ev->as.target_declare.name.data ? ev->as.target_declare.name.data : "");
            break;

        case EVENT_TARGET_ADD_SOURCE:
            printf(" target=%.*s",
                   (int) ev->as.target_add_source.target_name.count,
                   ev->as.target_add_source.target_name.data ? ev->as.target_add_source.target_name.data : "");
            break;

        case EVENT_TARGET_ADD_DEPENDENCY:
            printf(" target=%.*s",
                   (int) ev->as.target_add_dependency.target_name.count,
                   ev->as.target_add_dependency.target_name.data ? ev->as.target_add_dependency.target_name.data : "");
            break;

        case EVENT_TARGET_PROP_SET:
            printf(" target=%.*s",
                   (int) ev->as.target_prop_set.target_name.count,
                   ev->as.target_prop_set.target_name.data ? ev->as.target_prop_set.target_name.data : "");
            break;

        case EVENT_TARGET_LINK_LIBRARIES:
            printf(" target=%.*s",
                   (int) ev->as.target_link_libraries.target_name.count,
                   ev->as.target_link_libraries.target_name.data ? ev->as.target_link_libraries.target_name.data : "");
            break;

        case EVENT_TARGET_LINK_OPTIONS:
            printf(" target=%.*s",
                   (int) ev->as.target_link_options.target_name.count,
                   ev->as.target_link_options.target_name.data ? ev->as.target_link_options.target_name.data : "");
            break;

        case EVENT_TARGET_LINK_DIRECTORIES:
            printf(" target=%.*s",
                   (int) ev->as.target_link_directories.target_name.count,
                   ev->as.target_link_directories.target_name.data ? ev->as.target_link_directories.target_name.data : "");
            break;

        case EVENT_TARGET_INCLUDE_DIRECTORIES:
            printf(" target=%.*s",
                   (int) ev->as.target_include_directories.target_name.count,
                   ev->as.target_include_directories.target_name.data ? ev->as.target_include_directories.target_name.data : "");
            break;

        case EVENT_TARGET_COMPILE_DEFINITIONS:
            printf(" target=%.*s",
                   (int) ev->as.target_compile_definitions.target_name.count,
                   ev->as.target_compile_definitions.target_name.data ? ev->as.target_compile_definitions.target_name.data : "");
            break;

        case EVENT_TARGET_COMPILE_OPTIONS:
            printf(" target=%.*s",
                   (int) ev->as.target_compile_options.target_name.count,
                   ev->as.target_compile_options.target_name.data ? ev->as.target_compile_options.target_name.data : "");
            break;

        case EVENT_INCLUDE_BEGIN:
            printf(" path=%.*s",
                   (int) ev->as.include_begin.path.count,
                   ev->as.include_begin.path.data ? ev->as.include_begin.path.data : "");
            break;

        case EVENT_INCLUDE_END:
            printf(" path=%.*s success=%s",
                   (int) ev->as.include_end.path.count,
                   ev->as.include_end.path.data ? ev->as.include_end.path.data : "",
                   ev->as.include_end.success ? "true" : "false");
            break;

        case EVENT_ADD_SUBDIRECTORY_BEGIN:
            printf(" src=%.*s",
                   (int) ev->as.add_subdirectory_begin.source_dir.count,
                   ev->as.add_subdirectory_begin.source_dir.data ? ev->as.add_subdirectory_begin.source_dir.data : "");
            break;

        case EVENT_ADD_SUBDIRECTORY_END:
            printf(" src=%.*s success=%s",
                   (int) ev->as.add_subdirectory_end.source_dir.count,
                   ev->as.add_subdirectory_end.source_dir.data ? ev->as.add_subdirectory_end.source_dir.data : "",
                   ev->as.add_subdirectory_end.success ? "true" : "false");
            break;

        case EVENT_DIR_PUSH:
            printf(" src=%.*s",
                   (int) ev->as.dir_push.source_dir.count,
                   ev->as.dir_push.source_dir.data ? ev->as.dir_push.source_dir.data : "");
            break;

        case EVENT_DIR_POP:
            printf(" src=%.*s",
                   (int) ev->as.dir_pop.source_dir.count,
                   ev->as.dir_pop.source_dir.data ? ev->as.dir_pop.source_dir.data : "");
            break;

        case EVENT_CMAKE_LANGUAGE_CALL:
            printf(" command=%.*s",
                   (int) ev->as.cmake_language_call.command_name.count,
                   ev->as.cmake_language_call.command_name.data ? ev->as.cmake_language_call.command_name.data : "");
            break;

        case EVENT_CMAKE_LANGUAGE_EVAL:
            printf(" code_len=%zu", ev->as.cmake_language_eval.code.count);
            break;

        case EVENT_CMAKE_LANGUAGE_DEFER_QUEUE:
            printf(" command=%.*s",
                   (int) ev->as.cmake_language_defer_queue.command_name.count,
                   ev->as.cmake_language_defer_queue.command_name.data ? ev->as.cmake_language_defer_queue.command_name.data : "");
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
