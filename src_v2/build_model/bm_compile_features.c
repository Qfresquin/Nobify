#include "bm_compile_features.h"

#include <ctype.h>

static bool bm_compile_feature_eq_ci(String_View lhs, const char *rhs) {
    size_t rhs_len = 0;
    if (!rhs) return false;
    rhs_len = strlen(rhs);
    if (lhs.count != rhs_len) return false;
    for (size_t i = 0; i < lhs.count; ++i) {
        if (tolower((unsigned char)lhs.data[i]) != tolower((unsigned char)rhs[i])) return false;
    }
    return true;
}

static const BM_Compile_Feature_Info k_bm_compile_feature_info[] = {
    {"c_std_90", BM_COMPILE_FEATURE_LANG_C, 90, true},
    {"c_std_99", BM_COMPILE_FEATURE_LANG_C, 99, true},
    {"c_std_11", BM_COMPILE_FEATURE_LANG_C, 11, true},
    {"c_std_17", BM_COMPILE_FEATURE_LANG_C, 17, true},
    {"c_std_23", BM_COMPILE_FEATURE_LANG_C, 23, true},
    {"c_function_prototypes", BM_COMPILE_FEATURE_LANG_C, 90, false},
    {"c_restrict", BM_COMPILE_FEATURE_LANG_C, 99, false},
    {"c_static_assert", BM_COMPILE_FEATURE_LANG_C, 11, false},
    {"c_variadic_macros", BM_COMPILE_FEATURE_LANG_C, 99, false},
    {"cxx_std_98", BM_COMPILE_FEATURE_LANG_CXX, 98, true},
    {"cxx_std_11", BM_COMPILE_FEATURE_LANG_CXX, 11, true},
    {"cxx_std_14", BM_COMPILE_FEATURE_LANG_CXX, 14, true},
    {"cxx_std_17", BM_COMPILE_FEATURE_LANG_CXX, 17, true},
    {"cxx_std_20", BM_COMPILE_FEATURE_LANG_CXX, 20, true},
    {"cxx_std_23", BM_COMPILE_FEATURE_LANG_CXX, 23, true},
    {"cxx_alias_templates", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cxx_constexpr", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cxx_decltype", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cxx_final", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cxx_generic_lambdas", BM_COMPILE_FEATURE_LANG_CXX, 14, false},
    {"cxx_lambdas", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cxx_nullptr", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cxx_range_for", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cxx_rvalue_references", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cxx_static_assert", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cxx_variadic_templates", BM_COMPILE_FEATURE_LANG_CXX, 11, false},
    {"cuda_std_03", BM_COMPILE_FEATURE_LANG_CUDA, 3, true},
    {"cuda_std_11", BM_COMPILE_FEATURE_LANG_CUDA, 11, true},
    {"cuda_std_14", BM_COMPILE_FEATURE_LANG_CUDA, 14, true},
    {"cuda_std_17", BM_COMPILE_FEATURE_LANG_CUDA, 17, true},
    {"cuda_std_20", BM_COMPILE_FEATURE_LANG_CUDA, 20, true},
};

const BM_Compile_Feature_Info *bm_compile_feature_lookup(String_View feature) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(k_bm_compile_feature_info); ++i) {
        if (bm_compile_feature_eq_ci(feature, k_bm_compile_feature_info[i].name)) {
            return &k_bm_compile_feature_info[i];
        }
    }
    return NULL;
}

String_View bm_compile_feature_lang_compile_var(BM_Compile_Feature_Lang lang) {
    switch (lang) {
        case BM_COMPILE_FEATURE_LANG_C: return nob_sv_from_cstr("CMAKE_C_COMPILE_FEATURES");
        case BM_COMPILE_FEATURE_LANG_CXX: return nob_sv_from_cstr("CMAKE_CXX_COMPILE_FEATURES");
        case BM_COMPILE_FEATURE_LANG_CUDA: return nob_sv_from_cstr("CMAKE_CUDA_COMPILE_FEATURES");
        default: return nob_sv_from_cstr("");
    }
}

String_View bm_compile_feature_lang_standard_prop(BM_Compile_Feature_Lang lang) {
    switch (lang) {
        case BM_COMPILE_FEATURE_LANG_C: return nob_sv_from_cstr("C_STANDARD");
        case BM_COMPILE_FEATURE_LANG_CXX: return nob_sv_from_cstr("CXX_STANDARD");
        case BM_COMPILE_FEATURE_LANG_CUDA: return nob_sv_from_cstr("CUDA_STANDARD");
        default: return nob_sv_from_cstr("");
    }
}

String_View bm_compile_feature_lang_standard_required_prop(BM_Compile_Feature_Lang lang) {
    switch (lang) {
        case BM_COMPILE_FEATURE_LANG_C: return nob_sv_from_cstr("C_STANDARD_REQUIRED");
        case BM_COMPILE_FEATURE_LANG_CXX: return nob_sv_from_cstr("CXX_STANDARD_REQUIRED");
        case BM_COMPILE_FEATURE_LANG_CUDA: return nob_sv_from_cstr("CUDA_STANDARD_REQUIRED");
        default: return nob_sv_from_cstr("");
    }
}

String_View bm_compile_feature_lang_extensions_prop(BM_Compile_Feature_Lang lang) {
    switch (lang) {
        case BM_COMPILE_FEATURE_LANG_C: return nob_sv_from_cstr("C_EXTENSIONS");
        case BM_COMPILE_FEATURE_LANG_CXX: return nob_sv_from_cstr("CXX_EXTENSIONS");
        case BM_COMPILE_FEATURE_LANG_CUDA: return nob_sv_from_cstr("CUDA_EXTENSIONS");
        default: return nob_sv_from_cstr("");
    }
}
