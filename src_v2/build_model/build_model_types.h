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
typedef uint32_t BM_Replay_Action_Id;
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
#define BM_REPLAY_ACTION_ID_INVALID ((BM_Replay_Action_Id)UINT32_MAX)
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
    BM_TARGET_UNKNOWN_LIBRARY,
} BM_Target_Kind;

typedef enum {
    BM_TARGET_SOURCE_REGULAR = 0,
    BM_TARGET_SOURCE_HEADER_FILE_SET,
    BM_TARGET_SOURCE_CXX_MODULE_FILE_SET,
} BM_Target_Source_Kind;

typedef enum {
    BM_TARGET_FILE_SET_HEADERS = 0,
    BM_TARGET_FILE_SET_CXX_MODULES,
} BM_Target_File_Set_Kind;

typedef enum {
    BM_BUILD_STEP_OUTPUT_RULE = 0,
    BM_BUILD_STEP_CUSTOM_TARGET,
    BM_BUILD_STEP_TARGET_PRE_BUILD,
    BM_BUILD_STEP_TARGET_PRE_LINK,
    BM_BUILD_STEP_TARGET_POST_BUILD,
} BM_Build_Step_Kind;

typedef enum {
    BM_REPLAY_PHASE_CONFIGURE = 0,
    BM_REPLAY_PHASE_BUILD,
    BM_REPLAY_PHASE_TEST,
    BM_REPLAY_PHASE_INSTALL,
    BM_REPLAY_PHASE_EXPORT,
    BM_REPLAY_PHASE_PACKAGE,
    BM_REPLAY_PHASE_HOST_ONLY,
} BM_Replay_Phase;

typedef enum {
    BM_REPLAY_ACTION_FILESYSTEM = 0,
    BM_REPLAY_ACTION_PROCESS,
    BM_REPLAY_ACTION_PROBE,
    BM_REPLAY_ACTION_DEPENDENCY_MATERIALIZATION,
    BM_REPLAY_ACTION_TEST_DRIVER,
    BM_REPLAY_ACTION_HOST_EFFECT,
} BM_Replay_Action_Kind;

typedef enum {
    BM_REPLAY_OPCODE_NONE = 0,
    BM_REPLAY_OPCODE_FS_MKDIR,
    BM_REPLAY_OPCODE_FS_WRITE_TEXT,
    BM_REPLAY_OPCODE_FS_APPEND_TEXT,
    BM_REPLAY_OPCODE_FS_COPY_FILE,
    BM_REPLAY_OPCODE_HOST_DOWNLOAD_LOCAL,
    BM_REPLAY_OPCODE_HOST_ARCHIVE_CREATE_PAXR,
    BM_REPLAY_OPCODE_HOST_ARCHIVE_EXTRACT_TAR,
    BM_REPLAY_OPCODE_HOST_LOCK_ACQUIRE,
    BM_REPLAY_OPCODE_HOST_LOCK_RELEASE,
    BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_SOURCE,
    BM_REPLAY_OPCODE_PROBE_TRY_COMPILE_PROJECT,
    BM_REPLAY_OPCODE_PROBE_TRY_RUN,
    BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_SOURCE_DIR,
    BM_REPLAY_OPCODE_DEPS_FETCHCONTENT_LOCAL_ARCHIVE,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_EMPTY_BINARY_DIRECTORY,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_START_LOCAL,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_CONFIGURE_SELF,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_BUILD_SELF,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_TEST,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_SLEEP,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_COVERAGE_LOCAL,
    BM_REPLAY_OPCODE_TEST_DRIVER_CTEST_MEMCHECK_LOCAL,
} BM_Replay_Opcode;

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
    Event_Link_Item_Metadata semantic;
} BM_String_Item_View;

typedef struct {
    const BM_String_Item_View *items;
    size_t count;
} BM_String_Item_Span;

typedef struct {
    String_View value;
    BM_Visibility visibility;
    uint32_t flags;
    BM_Provenance provenance;
    Event_Link_Item_Metadata semantic;
    BM_Target_Id target_id;
} BM_Link_Item_View;

typedef struct {
    const BM_Link_Item_View *items;
    size_t count;
} BM_Link_Item_Span;

Diag_Sink *bm_diag_sink_create(Arena *arena, Diag_Sink_Emit_Fn emit, void *userdata);
Diag_Sink *bm_diag_sink_create_default(Arena *arena);

#endif
