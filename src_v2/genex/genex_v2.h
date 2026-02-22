#ifndef GENEX_V2_H_
#define GENEX_V2_H_

#include <stddef.h>

#include "arena.h"
#include "nob.h"

typedef enum {
    GENEX_V2_OK = 0,
    GENEX_V2_UNSUPPORTED,
    GENEX_V2_ERROR,
    GENEX_V2_CYCLE_GUARD_HIT,
} Genex_V2_Status;

typedef String_View (*Genex_V2_Target_Property_Read_Fn)(void *userdata,
                                                        String_View target_name,
                                                        String_View property_name);

typedef struct {
    Arena *arena;
    String_View config;
    Genex_V2_Target_Property_Read_Fn read_target_property;
    void *userdata;
    size_t max_depth;
    size_t max_target_property_depth;
} Genex_V2_Context;

typedef struct {
    Genex_V2_Status status;
    String_View value;
    String_View diag_message;
} Genex_V2_Result;

Genex_V2_Result genex_v2_eval(const Genex_V2_Context *ctx, String_View input);

#endif // GENEX_V2_H_
