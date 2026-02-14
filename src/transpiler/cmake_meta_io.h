#ifndef CMAKE_META_IO_H_
#define CMAKE_META_IO_H_

#include "build_model.h"

int cmk_meta_parse_version_major(String_View tok, int fallback);
bool cmk_meta_emit_file_api_query(Arena *arena, String_View query_root, String_View kind, String_View version_token);
bool cmk_meta_emit_empty_file_api_query(Arena *arena, String_View query_root);
bool cmk_meta_emit_instrumentation_query(Arena *arena,
                                         String_View root,
                                         const String_List *hooks,
                                         const String_List *queries,
                                         const String_List *callbacks,
                                         size_t query_counter,
                                         String_View *out_path);
bool cmk_meta_export_write_targets_file(Arena *arena,
                                        String_View out_path,
                                        String_View ns,
                                        String_View signature,
                                        String_View export_set_name,
                                        const String_List *targets,
                                        bool append_mode);
bool cmk_meta_export_register_package(Arena *arena,
                                      String_View registry_dir,
                                      String_View package_name,
                                      String_View dir_key,
                                      String_View package_dir);

#endif // CMAKE_META_IO_H_
