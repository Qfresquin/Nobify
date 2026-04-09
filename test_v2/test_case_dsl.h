#ifndef TEST_CASE_DSL_H_
#define TEST_CASE_DSL_H_

#include "nob.h"
#include "test_case_pack.h"

typedef enum {
    TEST_CASE_DSL_EXPECT_SUCCESS = 0,
    TEST_CASE_DSL_EXPECT_ERROR,
} Test_Case_Dsl_Expected_Outcome;

typedef enum {
    TEST_CASE_DSL_LAYOUT_BODY_ONLY_PROJECT = 0,
    TEST_CASE_DSL_LAYOUT_RAW_CMAKELISTS,
} Test_Case_Dsl_Project_Layout;

typedef enum {
    TEST_CASE_DSL_MODE_PROJECT = 0,
    TEST_CASE_DSL_MODE_SCRIPT,
} Test_Case_Dsl_Mode;

typedef enum {
    TEST_CASE_DSL_PATH_SCOPE_SOURCE = 0,
    TEST_CASE_DSL_PATH_SCOPE_BUILD,
} Test_Case_Dsl_Path_Scope;

typedef enum {
    TEST_CASE_DSL_ENV_SET = 0,
    TEST_CASE_DSL_ENV_UNSET,
    TEST_CASE_DSL_ENV_SET_PATH,
} Test_Case_Dsl_Env_Op_Kind;

typedef enum {
    TEST_CASE_DSL_QUERY_VAR = 0,
    TEST_CASE_DSL_QUERY_CACHE_DEFINED,
    TEST_CASE_DSL_QUERY_TARGET_EXISTS,
    TEST_CASE_DSL_QUERY_TARGET_PROP,
    TEST_CASE_DSL_QUERY_FILE_EXISTS,
    TEST_CASE_DSL_QUERY_STDOUT,
    TEST_CASE_DSL_QUERY_STDERR,
    TEST_CASE_DSL_QUERY_FILE_TEXT,
    TEST_CASE_DSL_QUERY_FILE_SHA256,
    TEST_CASE_DSL_QUERY_TREE,
    TEST_CASE_DSL_QUERY_CMAKE_PROP,
    TEST_CASE_DSL_QUERY_GLOBAL_PROP,
    TEST_CASE_DSL_QUERY_DIR_PROP,
} Test_Case_Dsl_Query_Kind;

typedef struct {
    Test_Case_Dsl_Query_Kind kind;
    String_View arg0;
    String_View arg1;
} Test_Case_Dsl_Query;

typedef struct {
    Test_Case_Dsl_Path_Scope scope;
    String_View relpath;
} Test_Case_Dsl_Path_Entry;

typedef struct {
    Test_Case_Dsl_Path_Scope scope;
    String_View relpath;
    String_View text;
} Test_Case_Dsl_Text_Fixture;

typedef struct {
    Test_Case_Dsl_Env_Op_Kind kind;
    String_View name;
    String_View value;
    Test_Case_Dsl_Path_Scope path_scope;
    String_View path_relpath;
} Test_Case_Dsl_Env_Op;

typedef struct {
    String_View name;
    String_View type;
    String_View value;
} Test_Case_Dsl_Cache_Init;

typedef struct {
    String_View name;
    String_View body;
    Test_Case_Dsl_Expected_Outcome expected_outcome;
    Test_Case_Dsl_Mode mode;
    Test_Case_Dsl_Project_Layout layout;
    Test_Case_Dsl_Path_Entry *files;
    Test_Case_Dsl_Path_Entry *dirs;
    Test_Case_Dsl_Text_Fixture *text_files;
    Test_Case_Dsl_Env_Op *env_ops;
    Test_Case_Dsl_Cache_Init *cache_inits;
    Test_Case_Dsl_Query *queries;
} Test_Case_Dsl_Case;

bool test_case_dsl_split_scoped_path(String_View raw,
                                     Test_Case_Dsl_Path_Scope default_scope,
                                     Test_Case_Dsl_Path_Scope *out_scope,
                                     String_View *out_relpath);
bool test_case_dsl_parse_case(Arena *arena,
                              Test_Case_Pack_Entry entry,
                              Test_Case_Dsl_Case *out_case);
bool test_case_dsl_case_exists_in_pack(Arena *arena,
                                       const char *case_pack_path,
                                       const char *case_name);
bool test_case_dsl_load_case_from_pack(Arena *arena,
                                       const char *case_pack_path,
                                       const char *case_name,
                                       Test_Case_Dsl_Case *out_case);

#endif // TEST_CASE_DSL_H_
