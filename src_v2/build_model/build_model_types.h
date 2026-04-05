#ifndef BUILD_MODEL_TYPES_H_
#define BUILD_MODEL_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arena.h"
#include "nob.h"
#include "diagnostics.h"
#include "event_ir.h"

typedef struct BM_Builder BM_Builder;
typedef struct Build_Model_Draft Build_Model_Draft;
typedef struct Build_Model Build_Model;

typedef void (*Diag_Sink_Emit_Fn)(void *userdata,
                                  Diag_Severity severity,
                                  const char *component,
                                  String_View file_path,
                                  uint32_t line,
                                  uint32_t col,
                                  const char *command,
                                  const char *cause,
                                  const char *hint);

typedef struct Diag_Sink {
    Diag_Sink_Emit_Fn emit;
    void *userdata;
} Diag_Sink;

typedef uint32_t BM_Directory_Id;
typedef uint32_t BM_Target_Id;
typedef uint32_t BM_Build_Step_Id;
typedef uint32_t BM_Test_Id;
typedef uint32_t BM_Install_Rule_Id;
typedef uint32_t BM_Export_Id;
typedef uint32_t BM_Package_Id;
typedef uint32_t BM_CPack_Install_Type_Id;
typedef uint32_t BM_CPack_Component_Group_Id;
typedef uint32_t BM_CPack_Component_Id;
typedef uint32_t BM_CPack_Package_Id;

#define BM_DIRECTORY_ID_INVALID ((BM_Directory_Id)UINT32_MAX)
#define BM_TARGET_ID_INVALID ((BM_Target_Id)UINT32_MAX)
#define BM_BUILD_STEP_ID_INVALID ((BM_Build_Step_Id)UINT32_MAX)
#define BM_TEST_ID_INVALID ((BM_Test_Id)UINT32_MAX)
#define BM_INSTALL_RULE_ID_INVALID ((BM_Install_Rule_Id)UINT32_MAX)
#define BM_EXPORT_ID_INVALID ((BM_Export_Id)UINT32_MAX)
#define BM_PACKAGE_ID_INVALID ((BM_Package_Id)UINT32_MAX)
#define BM_CPACK_INSTALL_TYPE_ID_INVALID ((BM_CPack_Install_Type_Id)UINT32_MAX)
#define BM_CPACK_COMPONENT_GROUP_ID_INVALID ((BM_CPack_Component_Group_Id)UINT32_MAX)
#define BM_CPACK_COMPONENT_ID_INVALID ((BM_CPack_Component_Id)UINT32_MAX)
#define BM_CPACK_PACKAGE_ID_INVALID ((BM_CPack_Package_Id)UINT32_MAX)

typedef struct {
    const String_View *items;
    size_t count;
} BM_String_Span;

typedef struct {
    const BM_Target_Id *items;
    size_t count;
} BM_Target_Id_Span;

typedef struct {
    const BM_Install_Rule_Id *items;
    size_t count;
} BM_Install_Rule_Id_Span;

typedef struct {
    const BM_Export_Id *items;
    size_t count;
} BM_Export_Id_Span;

typedef struct {
    const BM_Build_Step_Id *items;
    size_t count;
} BM_Build_Step_Id_Span;

typedef struct {
    const BM_CPack_Component_Id *items;
    size_t count;
} BM_CPack_Component_Id_Span;

typedef struct {
    const BM_CPack_Install_Type_Id *items;
    size_t count;
} BM_CPack_Install_Type_Id_Span;

typedef struct {
    uint64_t event_seq;
    Event_Kind event_kind;
    String_View file_path;
    uint32_t line;
    uint32_t col;
} BM_Provenance;

typedef enum {
    BM_TARGET_EXECUTABLE = 0,
    BM_TARGET_STATIC_LIBRARY,
    BM_TARGET_SHARED_LIBRARY,
    BM_TARGET_MODULE_LIBRARY,
    BM_TARGET_INTERFACE_LIBRARY,
    BM_TARGET_OBJECT_LIBRARY,
    BM_TARGET_UTILITY,
} BM_Target_Kind;

typedef enum {
    BM_BUILD_STEP_OUTPUT_RULE = 0,
    BM_BUILD_STEP_CUSTOM_TARGET,
    BM_BUILD_STEP_TARGET_PRE_BUILD,
    BM_BUILD_STEP_TARGET_PRE_LINK,
    BM_BUILD_STEP_TARGET_POST_BUILD,
} BM_Build_Step_Kind;

typedef enum {
    BM_VISIBILITY_PRIVATE = 0,
    BM_VISIBILITY_PUBLIC,
    BM_VISIBILITY_INTERFACE,
} BM_Visibility;

typedef enum {
    BM_INSTALL_RULE_TARGET = 0,
    BM_INSTALL_RULE_FILE,
    BM_INSTALL_RULE_PROGRAM,
    BM_INSTALL_RULE_DIRECTORY,
} BM_Install_Rule_Kind;

typedef enum {
    BM_EXPORT_INSTALL = 0,
    BM_EXPORT_BUILD_TREE,
    BM_EXPORT_PACKAGE_REGISTRY,
} BM_Export_Kind;

typedef enum {
    BM_EXPORT_SOURCE_INSTALL_EXPORT = 0,
    BM_EXPORT_SOURCE_TARGETS,
    BM_EXPORT_SOURCE_EXPORT_SET,
    BM_EXPORT_SOURCE_PACKAGE,
} BM_Export_Source_Kind;

typedef enum {
    BM_ITEM_FLAG_NONE = 0,
    BM_ITEM_FLAG_BEFORE = 1u << 0,
    BM_ITEM_FLAG_SYSTEM = 1u << 1,
} BM_Item_Flags;

typedef struct {
    String_View value;
    BM_Visibility visibility;
    uint32_t flags;
    BM_Provenance provenance;
} BM_String_Item_View;

typedef struct {
    const BM_String_Item_View *items;
    size_t count;
} BM_String_Item_Span;

Diag_Sink *bm_diag_sink_create(Arena *arena, Diag_Sink_Emit_Fn emit, void *userdata);
Diag_Sink *bm_diag_sink_create_default(Arena *arena);

#endif
