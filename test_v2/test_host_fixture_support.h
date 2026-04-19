#ifndef TEST_HOST_FIXTURE_SUPPORT_H_
#define TEST_HOST_FIXTURE_SUPPORT_H_

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char *name;
    char *prev_value;
    bool had_prev_value;
    bool heap_allocated;
} Test_Host_Env_Guard;

bool test_host_set_env_value(const char *name, const char *value);
bool test_host_env_guard_begin(Test_Host_Env_Guard *guard, const char *name, const char *value);
bool test_host_env_guard_begin_heap(Test_Host_Env_Guard **out_guard,
                                    const char *name,
                                    const char *value);
void test_host_env_guard_cleanup(void *ctx);

bool test_host_prepare_symlink_escape_fixture(const char *outside_dir,
                                              const char *outside_file_path,
                                              const char *outside_file_contents,
                                              const char *inside_link,
                                              const char *link_target);
bool test_host_create_directory_link_like(const char *link_path, const char *target_path);
bool test_host_prepare_mock_site_name_command(char *out_path, size_t out_path_size);
bool test_host_create_tar_archive(const char *archive_path,
                                  const char *parent_dir,
                                  const char *entry_name);
bool test_host_create_git_repo_with_tag(const char *repo_dir,
                                        const char *cmakelists_text,
                                        const char *version_text,
                                        const char *tag_name);

#endif // TEST_HOST_FIXTURE_SUPPORT_H_
