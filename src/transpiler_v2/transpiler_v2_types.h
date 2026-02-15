#ifndef TRANSPILER_V2_TYPES_H_
#define TRANSPILER_V2_TYPES_H_

#include "parser.h"
#include "build_model_types.h"

typedef enum {
    TRANSPILER_COMPAT_PROFILE_CMAKE_3_X = 0,
    TRANSPILER_COMPAT_PROFILE_CMAKE_4_X,
    TRANSPILER_COMPAT_PROFILE_STRICT,
} Transpiler_Compat_Profile_Kind;

typedef struct {
    Transpiler_Compat_Profile_Kind kind;
    String_View cmake_version;
    bool allow_behavior_drift;
} Transpiler_Compat_Profile;

typedef struct {
    String_View command_name;
    Node node;
} Cmake_IR_Command;

typedef struct {
    String_View kind;
    String_View payload;
} Cmake_IR_Effect;

typedef struct {
    Cmake_IR_Command *commands;
    size_t command_count;
    size_t command_capacity;
    Cmake_IR_Effect *effects;
    size_t effect_count;
    size_t effect_capacity;
} Cmake_IR_Program;

#endif // TRANSPILER_V2_TYPES_H_

