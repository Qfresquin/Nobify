#ifndef BUILD_MODEL_CPACK_H_
#define BUILD_MODEL_CPACK_H_

#include "build_model_types.h"

#if defined(__GNUC__) || defined(__clang__)
#define BUILD_MODEL_DEPRECATED(msg) __attribute__((deprecated(msg)))
#elif defined(_MSC_VER)
#define BUILD_MODEL_DEPRECATED(msg) __declspec(deprecated(msg))
#else
#define BUILD_MODEL_DEPRECATED(msg)
#endif

CPack_Component_Group* build_model_add_cpack_component_group(Build_Model *model, String_View name);
CPack_Install_Type* build_model_add_cpack_install_type(Build_Model *model, String_View name);
CPack_Component* build_model_add_cpack_component(Build_Model *model, String_View name);

CPack_Component_Group* build_model_ensure_cpack_group(Build_Model *model, String_View name);
CPack_Component* build_model_ensure_cpack_component(Build_Model *model, String_View name);
CPack_Install_Type* build_model_ensure_cpack_install_type(Build_Model *model, String_View name);

size_t build_model_get_cpack_install_type_count(const Build_Model *model);
CPack_Install_Type* build_model_get_cpack_install_type_at(Build_Model *model, size_t index);
size_t build_model_get_cpack_component_group_count(const Build_Model *model);
CPack_Component_Group* build_model_get_cpack_component_group_at(Build_Model *model, size_t index);
size_t build_model_get_cpack_component_count(const Build_Model *model);
CPack_Component* build_model_get_cpack_component_at(Build_Model *model, size_t index);

String_View build_cpack_install_type_get_name(const CPack_Install_Type *install_type);
String_View build_cpack_install_type_get_display_name(const CPack_Install_Type *install_type);
String_View build_cpack_group_get_name(const CPack_Component_Group *group);
String_View build_cpack_group_get_display_name(const CPack_Component_Group *group);
String_View build_cpack_group_get_description(const CPack_Component_Group *group);
String_View build_cpack_group_get_parent_group(const CPack_Component_Group *group);
bool build_cpack_group_get_expanded(const CPack_Component_Group *group);
bool build_cpack_group_get_bold_title(const CPack_Component_Group *group);
String_View build_cpack_component_get_name(const CPack_Component *component);
String_View build_cpack_component_get_display_name(const CPack_Component *component);
String_View build_cpack_component_get_description(const CPack_Component *component);
String_View build_cpack_component_get_group(const CPack_Component *component);
String_View build_cpack_component_get_archive_file(const CPack_Component *component);
String_View build_cpack_component_get_plist(const CPack_Component *component);
const String_List* build_cpack_component_get_depends(const CPack_Component *component);
const String_List* build_cpack_component_get_install_types(const CPack_Component *component);
bool build_cpack_component_get_required(const CPack_Component *component);
bool build_cpack_component_get_hidden(const CPack_Component *component);
bool build_cpack_component_get_disabled(const CPack_Component *component);
bool build_cpack_component_get_downloaded(const CPack_Component *component);

void build_cpack_install_type_set_display_name(CPack_Install_Type *install_type, String_View display_name);
void build_cpack_group_set_display_name(CPack_Component_Group *group, String_View display_name);
void build_cpack_group_set_description(CPack_Component_Group *group, String_View description);
void build_cpack_group_set_parent_group(CPack_Component_Group *group, String_View parent_group);
void build_cpack_group_set_expanded(CPack_Component_Group *group, bool expanded);
void build_cpack_group_set_bold_title(CPack_Component_Group *group, bool bold_title);

void build_cpack_component_clear_dependencies(CPack_Component *component);
void build_cpack_component_clear_install_types(CPack_Component *component);
void build_cpack_component_set_display_name(CPack_Component *component, String_View display_name);
void build_cpack_component_set_description(CPack_Component *component, String_View description);
void build_cpack_component_set_group(CPack_Component *component, String_View group);
void build_cpack_component_set_archive_file(CPack_Component *component, String_View archive_file);
void build_cpack_component_set_plist(CPack_Component *component, String_View plist);
void build_cpack_component_add_dependency(CPack_Component *component, Arena *arena, String_View dependency);
void build_cpack_component_add_install_type(CPack_Component *component, Arena *arena, String_View install_type);
void build_cpack_component_set_required(CPack_Component *component, bool required);
void build_cpack_component_set_hidden(CPack_Component *component, bool hidden);
void build_cpack_component_set_disabled(CPack_Component *component, bool disabled);
void build_cpack_component_set_downloaded(CPack_Component *component, bool downloaded);

// Legacy wrappers kept during migration.
// Planned removal: next major version after all call sites migrate to ensure_* and add_test().
Build_Test* build_model_add_test_ex(Build_Model *model,
                                    Arena *arena,
                                    String_View name,
                                    String_View command,
                                    String_View working_dir) BUILD_MODEL_DEPRECATED("Use build_model_add_test()");
CPack_Component_Group* build_model_get_or_create_cpack_group(Build_Model *model,
                                                             Arena *arena,
                                                             String_View name) BUILD_MODEL_DEPRECATED("Use build_model_ensure_cpack_group()");
CPack_Component* build_model_get_or_create_cpack_component(Build_Model *model,
                                                           Arena *arena,
                                                           String_View name) BUILD_MODEL_DEPRECATED("Use build_model_ensure_cpack_component()");
CPack_Install_Type* build_model_get_or_create_cpack_install_type(Build_Model *model,
                                                                  Arena *arena,
                                                                  String_View name) BUILD_MODEL_DEPRECATED("Use build_model_ensure_cpack_install_type()");

#endif // BUILD_MODEL_CPACK_H_

