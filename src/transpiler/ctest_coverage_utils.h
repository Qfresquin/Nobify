#ifndef CTEST_COVERAGE_UTILS_H_
#define CTEST_COVERAGE_UTILS_H_

#include "build_model.h"

bool ctest_coverage_collect_gcov_bundle(Arena *arena,
                                        String_View source_dir,
                                        String_View build_dir,
                                        String_View gcov_command,
                                        const String_List *gcov_options,
                                        String_View tarball_path,
                                        String_View tarball_compression,
                                        bool delete_after,
                                        String_View *out_data_json_path,
                                        String_View *out_labels_json_path,
                                        String_View *out_coverage_xml_path,
                                        size_t *out_file_count);

#endif // CTEST_COVERAGE_UTILS_H_
