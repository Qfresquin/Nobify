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

static bool ev_copy_sv_array_inplace(Arena *arena, String_View **items, size_t count) {
    if (!arena || !items) return false;
    if (!*items || count == 0) {
        *items = NULL;
        return true;
    }

    String_View *copy = arena_alloc_array(arena, String_View, count);
    if (!copy) return false;

    for (size_t i = 0; i < count; i++) {
        copy[i] = (*items)[i];
        if (!ev_copy_sv_inplace(arena, &copy[i])) return false;
    }

    *items = copy;
    return true;
}

static bool ev_deep_copy_payload(Arena *arena, Cmake_Event *ev) {
    if (!arena || !ev) return false;

    if (!ev_copy_sv_inplace(arena, &ev->origin.file_path)) return false;

    switch (ev->kind) {
        case EV_DIAGNOSTIC:
            if (!ev_copy_sv_inplace(arena, &ev->as.diag.component)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.diag.command)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.diag.code)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.diag.error_class)) return false;
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

        case EV_TARGET_LINK_OPTIONS:
            if (!ev_copy_sv_inplace(arena, &ev->as.target_link_options.target_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.target_link_options.item)) return false;
            break;

        case EV_TARGET_LINK_DIRECTORIES:
            if (!ev_copy_sv_inplace(arena, &ev->as.target_link_directories.target_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.target_link_directories.path)) return false;
            break;

        case EV_CUSTOM_COMMAND_TARGET:
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_target.target_name)) return false;
            if (!ev_copy_sv_array_inplace(arena,
                                          &ev->as.custom_command_target.commands,
                                          ev->as.custom_command_target.command_count)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_target.working_dir)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_target.comment)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_target.outputs)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_target.byproducts)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_target.depends)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_target.main_dependency)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_target.depfile)) return false;
            break;

        case EV_CUSTOM_COMMAND_OUTPUT:
            if (!ev_copy_sv_array_inplace(arena,
                                          &ev->as.custom_command_output.commands,
                                          ev->as.custom_command_output.command_count)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_output.working_dir)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_output.comment)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_output.outputs)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_output.byproducts)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_output.depends)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_output.main_dependency)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.custom_command_output.depfile)) return false;
            break;

        case EV_DIR_PUSH:
            if (!ev_copy_sv_inplace(arena, &ev->as.dir_push.source_dir)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.dir_push.binary_dir)) return false;
            break;

        case EV_DIR_POP:
            break;

        case EV_DIRECTORY_INCLUDE_DIRECTORIES:
            if (!ev_copy_sv_inplace(arena, &ev->as.directory_include_directories.path)) return false;
            break;

        case EV_DIRECTORY_LINK_DIRECTORIES:
            if (!ev_copy_sv_inplace(arena, &ev->as.directory_link_directories.path)) return false;
            break;

        case EV_GLOBAL_COMPILE_DEFINITIONS:
            if (!ev_copy_sv_inplace(arena, &ev->as.global_compile_definitions.item)) return false;
            break;

        case EV_GLOBAL_COMPILE_OPTIONS:
            if (!ev_copy_sv_inplace(arena, &ev->as.global_compile_options.item)) return false;
            break;

        case EV_GLOBAL_LINK_OPTIONS:
            if (!ev_copy_sv_inplace(arena, &ev->as.global_link_options.item)) return false;
            break;

        case EV_GLOBAL_LINK_LIBRARIES:
            if (!ev_copy_sv_inplace(arena, &ev->as.global_link_libraries.item)) return false;
            break;

        case EV_TESTING_ENABLE:
            break;

        case EV_TEST_ADD:
            if (!ev_copy_sv_inplace(arena, &ev->as.test_add.name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.test_add.command)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.test_add.working_dir)) return false;
            break;

        case EV_INSTALL_ADD_RULE:
            if (!ev_copy_sv_inplace(arena, &ev->as.install_add_rule.item)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.install_add_rule.destination)) return false;
            break;

        case EV_CPACK_ADD_INSTALL_TYPE:
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_install_type.name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_install_type.display_name)) return false;
            break;

        case EV_CPACK_ADD_COMPONENT_GROUP:
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component_group.name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component_group.display_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component_group.description)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component_group.parent_group)) return false;
            break;

        case EV_CPACK_ADD_COMPONENT:
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component.name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component.display_name)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component.description)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component.group)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component.depends)) return false;
            if (!ev_copy_sv_inplace(arena, &ev->as.cpack_add_component.install_types)) return false;
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
        case EV_TARGET_LINK_OPTIONS: return "EV_TARGET_LINK_OPTIONS";
        case EV_TARGET_LINK_DIRECTORIES: return "EV_TARGET_LINK_DIRECTORIES";
        case EV_CUSTOM_COMMAND_TARGET: return "EV_CUSTOM_COMMAND_TARGET";
        case EV_CUSTOM_COMMAND_OUTPUT: return "EV_CUSTOM_COMMAND_OUTPUT";
        case EV_DIR_PUSH: return "EV_DIR_PUSH";
        case EV_DIR_POP: return "EV_DIR_POP";
        case EV_DIRECTORY_INCLUDE_DIRECTORIES: return "EV_DIRECTORY_INCLUDE_DIRECTORIES";
        case EV_DIRECTORY_LINK_DIRECTORIES: return "EV_DIRECTORY_LINK_DIRECTORIES";
        case EV_GLOBAL_COMPILE_DEFINITIONS: return "EV_GLOBAL_COMPILE_DEFINITIONS";
        case EV_GLOBAL_COMPILE_OPTIONS: return "EV_GLOBAL_COMPILE_OPTIONS";
        case EV_GLOBAL_LINK_OPTIONS: return "EV_GLOBAL_LINK_OPTIONS";
        case EV_GLOBAL_LINK_LIBRARIES: return "EV_GLOBAL_LINK_LIBRARIES";
        case EV_TESTING_ENABLE: return "EV_TESTING_ENABLE";
        case EV_TEST_ADD: return "EV_TEST_ADD";
        case EV_INSTALL_ADD_RULE: return "EV_INSTALL_ADD_RULE";
        case EV_CPACK_ADD_INSTALL_TYPE: return "EV_CPACK_ADD_INSTALL_TYPE";
        case EV_CPACK_ADD_COMPONENT_GROUP: return "EV_CPACK_ADD_COMPONENT_GROUP";
        case EV_CPACK_ADD_COMPONENT: return "EV_CPACK_ADD_COMPONENT";
        case EV_FIND_PACKAGE: return "EV_FIND_PACKAGE";
    }
    return "EV_UNKNOWN";
}

static void ev_print_sv(const char *label, String_View sv) {
    printf(" %s=\"%.*s\"", label, (int)sv.count, sv.data ? sv.data : "");
}

static void ev_print_sv_list(const char *label, String_View *items, size_t count) {
    printf(" %s=[", label);
    if (!items) {
        printf("]");
        return;
    }
    for (size_t i = 0; i < count; i++) {
        if (i > 0) printf(", ");
        printf("\"%.*s\"", (int)items[i].count, items[i].data ? items[i].data : "");
    }
    printf("]");
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
                ev_print_sv("code", ev->as.diag.code);
                ev_print_sv("class", ev->as.diag.error_class);
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
            case EV_TARGET_LINK_OPTIONS:
                ev_print_sv("target", ev->as.target_link_options.target_name);
                ev_print_sv("item", ev->as.target_link_options.item);
                printf(" vis=%d", (int)ev->as.target_link_options.visibility);
                break;
            case EV_TARGET_LINK_DIRECTORIES:
                ev_print_sv("target", ev->as.target_link_directories.target_name);
                ev_print_sv("path", ev->as.target_link_directories.path);
                printf(" vis=%d", (int)ev->as.target_link_directories.visibility);
                break;
            case EV_CUSTOM_COMMAND_TARGET:
                ev_print_sv("target", ev->as.custom_command_target.target_name);
                printf(" pre_build=%d", ev->as.custom_command_target.pre_build ? 1 : 0);
                ev_print_sv_list("commands",
                                 ev->as.custom_command_target.commands,
                                 ev->as.custom_command_target.command_count);
                ev_print_sv("working_dir", ev->as.custom_command_target.working_dir);
                ev_print_sv("comment", ev->as.custom_command_target.comment);
                ev_print_sv("outputs", ev->as.custom_command_target.outputs);
                ev_print_sv("byproducts", ev->as.custom_command_target.byproducts);
                ev_print_sv("depends", ev->as.custom_command_target.depends);
                ev_print_sv("main_dependency", ev->as.custom_command_target.main_dependency);
                ev_print_sv("depfile", ev->as.custom_command_target.depfile);
                printf(" append=%d verbatim=%d uses_terminal=%d command_expand_lists=%d depends_explicit_only=%d codegen=%d",
                       ev->as.custom_command_target.append ? 1 : 0,
                       ev->as.custom_command_target.verbatim ? 1 : 0,
                       ev->as.custom_command_target.uses_terminal ? 1 : 0,
                       ev->as.custom_command_target.command_expand_lists ? 1 : 0,
                       ev->as.custom_command_target.depends_explicit_only ? 1 : 0,
                       ev->as.custom_command_target.codegen ? 1 : 0);
                break;
            case EV_CUSTOM_COMMAND_OUTPUT:
                ev_print_sv_list("commands",
                                 ev->as.custom_command_output.commands,
                                 ev->as.custom_command_output.command_count);
                ev_print_sv("working_dir", ev->as.custom_command_output.working_dir);
                ev_print_sv("comment", ev->as.custom_command_output.comment);
                ev_print_sv("outputs", ev->as.custom_command_output.outputs);
                ev_print_sv("byproducts", ev->as.custom_command_output.byproducts);
                ev_print_sv("depends", ev->as.custom_command_output.depends);
                ev_print_sv("main_dependency", ev->as.custom_command_output.main_dependency);
                ev_print_sv("depfile", ev->as.custom_command_output.depfile);
                printf(" append=%d verbatim=%d uses_terminal=%d command_expand_lists=%d depends_explicit_only=%d codegen=%d",
                       ev->as.custom_command_output.append ? 1 : 0,
                       ev->as.custom_command_output.verbatim ? 1 : 0,
                       ev->as.custom_command_output.uses_terminal ? 1 : 0,
                       ev->as.custom_command_output.command_expand_lists ? 1 : 0,
                       ev->as.custom_command_output.depends_explicit_only ? 1 : 0,
                       ev->as.custom_command_output.codegen ? 1 : 0);
                break;
            case EV_DIR_PUSH:
                ev_print_sv("source_dir", ev->as.dir_push.source_dir);
                ev_print_sv("binary_dir", ev->as.dir_push.binary_dir);
                break;
            case EV_DIR_POP:
                break;
            case EV_DIRECTORY_INCLUDE_DIRECTORIES:
                ev_print_sv("path", ev->as.directory_include_directories.path);
                printf(" is_system=%d is_before=%d",
                       ev->as.directory_include_directories.is_system ? 1 : 0,
                       ev->as.directory_include_directories.is_before ? 1 : 0);
                break;
            case EV_DIRECTORY_LINK_DIRECTORIES:
                ev_print_sv("path", ev->as.directory_link_directories.path);
                printf(" is_before=%d", ev->as.directory_link_directories.is_before ? 1 : 0);
                break;
            case EV_GLOBAL_COMPILE_DEFINITIONS:
                ev_print_sv("item", ev->as.global_compile_definitions.item);
                break;
            case EV_GLOBAL_COMPILE_OPTIONS:
                ev_print_sv("item", ev->as.global_compile_options.item);
                break;
            case EV_GLOBAL_LINK_OPTIONS:
                ev_print_sv("item", ev->as.global_link_options.item);
                break;
            case EV_GLOBAL_LINK_LIBRARIES:
                ev_print_sv("item", ev->as.global_link_libraries.item);
                break;
            case EV_TESTING_ENABLE:
                printf(" enabled=%d", ev->as.testing_enable.enabled ? 1 : 0);
                break;
            case EV_TEST_ADD:
                ev_print_sv("name", ev->as.test_add.name);
                ev_print_sv("command", ev->as.test_add.command);
                ev_print_sv("working_dir", ev->as.test_add.working_dir);
                printf(" command_expand_lists=%d", ev->as.test_add.command_expand_lists ? 1 : 0);
                break;
            case EV_INSTALL_ADD_RULE:
                printf(" type=%d", (int)ev->as.install_add_rule.rule_type);
                ev_print_sv("item", ev->as.install_add_rule.item);
                ev_print_sv("destination", ev->as.install_add_rule.destination);
                break;
            case EV_CPACK_ADD_INSTALL_TYPE:
                ev_print_sv("name", ev->as.cpack_add_install_type.name);
                ev_print_sv("display_name", ev->as.cpack_add_install_type.display_name);
                break;
            case EV_CPACK_ADD_COMPONENT_GROUP:
                ev_print_sv("name", ev->as.cpack_add_component_group.name);
                ev_print_sv("display_name", ev->as.cpack_add_component_group.display_name);
                ev_print_sv("description", ev->as.cpack_add_component_group.description);
                ev_print_sv("parent_group", ev->as.cpack_add_component_group.parent_group);
                printf(" expanded=%d bold_title=%d",
                       ev->as.cpack_add_component_group.expanded ? 1 : 0,
                       ev->as.cpack_add_component_group.bold_title ? 1 : 0);
                break;
            case EV_CPACK_ADD_COMPONENT:
                ev_print_sv("name", ev->as.cpack_add_component.name);
                ev_print_sv("display_name", ev->as.cpack_add_component.display_name);
                ev_print_sv("description", ev->as.cpack_add_component.description);
                ev_print_sv("group", ev->as.cpack_add_component.group);
                ev_print_sv("depends", ev->as.cpack_add_component.depends);
                ev_print_sv("install_types", ev->as.cpack_add_component.install_types);
                printf(" required=%d hidden=%d disabled=%d downloaded=%d",
                       ev->as.cpack_add_component.required ? 1 : 0,
                       ev->as.cpack_add_component.hidden ? 1 : 0,
                       ev->as.cpack_add_component.disabled ? 1 : 0,
                       ev->as.cpack_add_component.downloaded ? 1 : 0);
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
