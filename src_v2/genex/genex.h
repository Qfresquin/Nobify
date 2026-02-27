#ifndef GENEX_H_
#define GENEX_H_

#include <stdbool.h>
#include <stddef.h>

#include "arena.h"
#include "nob.h"

typedef enum {
    GENEX_OK = 0,
    GENEX_UNSUPPORTED,
    GENEX_ERROR,
    GENEX_CYCLE_GUARD_HIT,
} Genex_Status;

typedef String_View (*Genex_Target_Property_Read_Fn)(void *userdata,
                                                        String_View target_name,
                                                        String_View property_name);
typedef String_View (*Genex_Target_File_Read_Fn)(void *userdata,
                                                 String_View target_name);
typedef String_View (*Genex_Target_Linker_File_Read_Fn)(void *userdata,
                                                         String_View target_name);
// Callback contract:
// - Returned String_View may be non-null-terminated (length is authoritative).
// - If count > 0, data must be non-NULL.
// - count should stay below max_callback_value_len (or internal default).

typedef struct {
    Arena *arena;
    String_View config;
    String_View current_target_name;
    String_View platform_id;
    String_View compile_language;
    Genex_Target_Property_Read_Fn read_target_property;
    Genex_Target_File_Read_Fn read_target_file;
    Genex_Target_Linker_File_Read_Fn read_target_linker_file;
    void *userdata;
    bool link_only_active;
    bool build_interface_active;
    bool install_interface_active;
    bool target_name_case_insensitive;
    // Defensive bound for callback-returned values. 0 => use internal default.
    size_t max_callback_value_len;
    size_t max_depth;
    size_t max_target_property_depth;
} Genex_Context;

typedef struct {
    Genex_Status status;
    String_View value;
    String_View diag_message;
} Genex_Result;

Genex_Result genex_eval(const Genex_Context *ctx, String_View input);

#endif // GENEX_H_


