#include "nob_codegen_internal.h"

bool cg_emit_package_function(CG_Context *ctx, Nob_String_Builder *out) {
    bool has_package_data = false;
    if (!ctx || !out) return false;

    has_package_data =
        bm_query_package_count(ctx->model) > 0 ||
        bm_query_cpack_install_type_count(ctx->model) > 0 ||
        bm_query_cpack_component_group_count(ctx->model) > 0 ||
        bm_query_cpack_component_count(ctx->model) > 0;

    nob_sb_append_cstr(out, "static bool package_all(void) {\n");
    if (!has_package_data) {
        nob_sb_append_cstr(out, "    return true;\n");
    } else {
        nob_sb_append_cstr(out,
            "    nob_log(NOB_ERROR, \"codegen: package command is not implemented in the minimal backend\");\n"
            "    return false;\n");
    }
    nob_sb_append_cstr(out, "}\n\n");
    return true;
}
