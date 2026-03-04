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
    }

    putchar('\n');
}

void event_stream_dump(const Event_Stream *stream) {
    if (!stream) return;

    for (size_t i = 0; i < arena_arr_len(stream->items); ++i) {
        event_dump_one(&stream->items[i]);
    }
}
