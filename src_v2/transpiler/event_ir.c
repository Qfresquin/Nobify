#include "event_ir.h"
#include "arena_dyn.h"

#include <string.h>
#include <stdio.h>

static bool ev_copy_sv_inplace(Arena *arena, String_View *sv) {
    if (!arena || !sv) return false;
    if (!sv->data || sv->count == 0) return true;
    char *copy = arena_alloc(arena, sv->count + 1);
    if (!copy) return false;
    memcpy(copy, sv->data, sv->count);
    copy[sv->count] = '\0';
    *sv = nob_sv_from_parts(copy, sv->count);
    return true;
}

static bool ev_deep_copy_payload(Arena *arena, Cmake_Event *ev) {
    if (!arena || !ev) return false;

    if (!ev_copy_sv_inplace(arena, &ev->origin.file_path)) return false;

    switch (ev->kind) {
        case EV_DIAGNOSTIC:
            if (!ev_copy_sv_inplace(arena, &ev->as.diag.component)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.diag.command)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.diag.cause)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.diag.hint)) return false;
            break;

        case EV_PROJECT_DECLARE:
            if (!ev_copy_sv_inplace(arena, &ev->as.project_declare.name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.project_declare.version)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.project_declare.description)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.project_declare.languages)) return false;
            break;

        case EV_VAR_SET:
            if (!ev_copy_sv_inplace(arena, &ev->as.var_set.key)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.var_set.value)) return false;
            break;

        case EV_SET_CACHE_ENTRY:
            if (!ev_copy_sv_inplace(arena, &ev->as.cache_entry.key)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cache_entry.value)) return false;
            break;

        case EV_TARGET_DECLARE:
            if (!ev_copy_sv_inplace(arena, &ev->as.target_declare.name)) return false;
            break;

        case EV_TARGET_ADD_SOURCE:
            if (!ev_copy_sv_inplace(arena, &ev->as.target_add_source.target_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.target_add_source.path)) return false;
            break;

        case EV_TARGET_PROP_SET:
            if (!ev_copy_sv_inplace(arena, &ev->as.target_prop_set.target_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.target_prop_set.key)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.target_prop_set.value)) return false;
            break;

        case EV_TARGET_INCLUDE_DIRECTORIES:
            if (!ev_copy_sv_inplace(arena, &ev->as.target_include_directories.target_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.target_include_directories.path)) return false;
            break;

        case EV_TARGET_COMPILE_DEFINITIONS:
            if (!ev_copy_sv_inplace(arena, &ev->as.target_compile_definitions.target_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.target_compile_definitions.item)) return false;
            break;

        case EV_TARGET_COMPILE_OPTIONS:
            if (!ev_copy_sv_inplace(arena, &ev->as.target_compile_options.target_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.target_compile_options.item)) return false;
            break;

        case EV_TARGET_LINK_LIBRARIES:
            if (!ev_copy_sv_inplace(arena, &ev->as.target_link_libraries.target_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.target_link_libraries.item)) return false;
            break;

        case EV_GLOBAL_COMPILE_DEFINITIONS:
            if (!ev_copy_sv_inplace(arena, &ev->as.global_compile_definitions.item)) return false;
            break;

        case EV_GLOBAL_COMPILE_OPTIONS:
            if (!ev_copy_sv_inplace(arena, &ev->as.global_compile_options.item)) return false;
            break;

        case EV_FIND_PACKAGE:
            if (!ev_copy_sv_inplace(arena, &ev->as.find_package.package_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.find_package.mode)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.find_package.location)) return false;
            break;
    }

    return true;
}

bool event_stream_push(Arena *event_arena, Cmake_Event_Stream *stream, Cmake_Event ev) {
    if (!event_arena || !stream) return false;
    if (!ev_deep_copy_payload(event_arena, &ev)) return false;

    if (!arena_da_reserve(event_arena,
                          (void**)&stream->items,
                          &stream->capacity,
                          sizeof(stream->items[0]),
                          stream->count + 1)) return false;

    stream->items[stream->count++] = ev;
    return true;
}

Cmake_Event_Stream *event_stream_create(Arena *arena) {
    if (!arena) return NULL;
    return arena_alloc_zero(arena, sizeof(Cmake_Event_Stream));
}

Event_Stream_Iterator event_stream_iter(const Cmake_Event_Stream *stream) {
    Event_Stream_Iterator it = {0};
    it.stream = stream;
    it.index = 0;
    it.current = NULL;
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

static const char *ev_kind_name(Cmake_Event_Kind kind) {
    switch (kind) {
        case EV_DIAGNOSTIC: return "EV_DIAGNOSTIC";
        case EV_PROJECT_DECLARE: return "EV_PROJECT_DECLARE";
        case EV_VAR_SET: return "EV_VAR_SET";
        case EV_SET_CACHE_ENTRY: return "EV_SET_CACHE_ENTRY";
        case EV_TARGET_DECLARE: return "EV_TARGET_DECLARE";
        case EV_TARGET_ADD_SOURCE: return "EV_TARGET_ADD_SOURCE";
        case EV_TARGET_PROP_SET: return "EV_TARGET_PROP_SET";
        case EV_TARGET_INCLUDE_DIRECTORIES: return "EV_TARGET_INCLUDE_DIRECTORIES";
        case EV_TARGET_COMPILE_DEFINITIONS: return "EV_TARGET_COMPILE_DEFINITIONS";
        case EV_TARGET_COMPILE_OPTIONS: return "EV_TARGET_COMPILE_OPTIONS";
        case EV_TARGET_LINK_LIBRARIES: return "EV_TARGET_LINK_LIBRARIES";
        case EV_GLOBAL_COMPILE_DEFINITIONS: return "EV_GLOBAL_COMPILE_DEFINITIONS";
        case EV_GLOBAL_COMPILE_OPTIONS: return "EV_GLOBAL_COMPILE_OPTIONS";
        case EV_FIND_PACKAGE: return "EV_FIND_PACKAGE";
    }
    return "EV_UNKNOWN";
}

static void ev_print_sv(const char *label, String_View sv) {
    printf(" %s=\"%.*s\"", label, (int)sv.count, sv.data ? sv.data : "");
}

void event_stream_dump(const Cmake_Event_Stream *stream) {
    if (!stream) {
        printf("EventStream(NULL)\n");
        return;
    }

    printf("EventStream(count=%zu)\n", stream->count);
    for (size_t i = 0; i < stream->count; i++) {
        const Cmake_Event *ev = &stream->items[i];
        printf("[%zu] %s", i, ev_kind_name(ev->kind));
        ev_print_sv("file", ev->origin.file_path);
        printf(" line=%zu col=%zu", ev->origin.line, ev->origin.col);

        switch (ev->kind) {
            case EV_DIAGNOSTIC:
                ev_print_sv("component", ev->as.diag.component);
                ev_print_sv("command", ev->as.diag.command);
                ev_print_sv("cause", ev->as.diag.cause);
                ev_print_sv("hint", ev->as.diag.hint);
                break;
            case EV_PROJECT_DECLARE:
                ev_print_sv("name", ev->as.project_declare.name);
                ev_print_sv("version", ev->as.project_declare.version);
                ev_print_sv("description", ev->as.project_declare.description);
                ev_print_sv("languages", ev->as.project_declare.languages);
                break;
            case EV_VAR_SET:
                ev_print_sv("key", ev->as.var_set.key);
                ev_print_sv("value", ev->as.var_set.value);
                break;
            case EV_SET_CACHE_ENTRY:
                ev_print_sv("key", ev->as.cache_entry.key);
                ev_print_sv("value", ev->as.cache_entry.value);
                break;
            case EV_TARGET_DECLARE:
                ev_print_sv("name", ev->as.target_declare.name);
                printf(" type=%d", (int)ev->as.target_declare.type);
                break;
            case EV_TARGET_ADD_SOURCE:
                ev_print_sv("target", ev->as.target_add_source.target_name);
                ev_print_sv("path", ev->as.target_add_source.path);
                break;
            case EV_TARGET_PROP_SET:
                ev_print_sv("target", ev->as.target_prop_set.target_name);
                ev_print_sv("key", ev->as.target_prop_set.key);
                ev_print_sv("value", ev->as.target_prop_set.value);
                printf(" op=%d", (int)ev->as.target_prop_set.op);
                break;
            case EV_TARGET_INCLUDE_DIRECTORIES:
                ev_print_sv("target", ev->as.target_include_directories.target_name);
                ev_print_sv("path", ev->as.target_include_directories.path);
                printf(" vis=%d", (int)ev->as.target_include_directories.visibility);
                printf(" is_system=%d is_before=%d",
                       ev->as.target_include_directories.is_system ? 1 : 0,
                       ev->as.target_include_directories.is_before ? 1 : 0);
                break;
            case EV_TARGET_COMPILE_DEFINITIONS:
                ev_print_sv("target", ev->as.target_compile_definitions.target_name);
                ev_print_sv("item", ev->as.target_compile_definitions.item);
                printf(" vis=%d", (int)ev->as.target_compile_definitions.visibility);
                break;
            case EV_TARGET_COMPILE_OPTIONS:
                ev_print_sv("target", ev->as.target_compile_options.target_name);
                ev_print_sv("item", ev->as.target_compile_options.item);
                printf(" vis=%d", (int)ev->as.target_compile_options.visibility);
                break;
            case EV_TARGET_LINK_LIBRARIES:
                ev_print_sv("target", ev->as.target_link_libraries.target_name);
                ev_print_sv("item", ev->as.target_link_libraries.item);
                printf(" vis=%d", (int)ev->as.target_link_libraries.visibility);
                break;
            case EV_GLOBAL_COMPILE_DEFINITIONS:
                ev_print_sv("item", ev->as.global_compile_definitions.item);
                break;
            case EV_GLOBAL_COMPILE_OPTIONS:
                ev_print_sv("item", ev->as.global_compile_options.item);
                break;
            case EV_FIND_PACKAGE:
                ev_print_sv("package", ev->as.find_package.package_name);
                ev_print_sv("mode", ev->as.find_package.mode);
                printf(" required=%d found=%d",
                       ev->as.find_package.required ? 1 : 0,
                       ev->as.find_package.found ? 1 : 0);
                ev_print_sv("location", ev->as.find_package.location);
                break;
        }
        printf("\n");
    }
}
