#ifndef TEST_SNAPSHOT_SUPPORT_H_
#define TEST_SNAPSHOT_SUPPORT_H_

#include "nob.h"
#include "test_case_pack.h"

typedef void (*Test_Snapshot_Golden_Mismatch_Fn)(const char *subject_path,
                                                 const char *expected_path,
                                                 String_View expected_norm,
                                                 String_View actual_norm,
                                                 void *userdata);

bool test_snapshot_load_text_file_to_arena(Arena *arena, const char *path, String_View *out);
String_View test_snapshot_normalize_newlines_to_arena(Arena *arena, String_View in);
bool test_snapshot_parse_case_pack_to_arena(Arena *arena,
                                            String_View content,
                                            Test_Case_Pack_Entry **out_items);
void test_snapshot_append_escaped_sv(Nob_String_Builder *sb, String_View sv);
bool test_snapshot_assert_golden_output(
    Arena *arena,
    const char *subject_path,
    const char *expected_path,
    String_View actual,
    Test_Snapshot_Golden_Mismatch_Fn on_mismatch,
    void *userdata);

#endif // TEST_SNAPSHOT_SUPPORT_H_
