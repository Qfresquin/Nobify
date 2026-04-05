#ifndef BM_COMPILE_FEATURES_H_
#define BM_COMPILE_FEATURES_H_

#include "nob.h"

typedef enum {
    BM_COMPILE_FEATURE_LANG_UNKNOWN = 0,
    BM_COMPILE_FEATURE_LANG_C,
    BM_COMPILE_FEATURE_LANG_CXX,
    BM_COMPILE_FEATURE_LANG_CUDA,
} BM_Compile_Feature_Lang;

typedef struct {
    const char *name;
    BM_Compile_Feature_Lang lang;
    int standard;
    bool meta;
} BM_Compile_Feature_Info;

const BM_Compile_Feature_Info *bm_compile_feature_lookup(String_View feature);
String_View bm_compile_feature_lang_compile_var(BM_Compile_Feature_Lang lang);
String_View bm_compile_feature_lang_standard_prop(BM_Compile_Feature_Lang lang);
String_View bm_compile_feature_lang_standard_required_prop(BM_Compile_Feature_Lang lang);
String_View bm_compile_feature_lang_extensions_prop(BM_Compile_Feature_Lang lang);

#endif
