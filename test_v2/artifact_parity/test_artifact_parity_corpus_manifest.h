#ifndef TEST_ARTIFACT_PARITY_CORPUS_MANIFEST_H_
#define TEST_ARTIFACT_PARITY_CORPUS_MANIFEST_H_

#include "nob.h"

#include <stdbool.h>
#include <stddef.h>

#define ARTIFACT_PARITY_CORPUS_MANIFEST_PATH "test_v2/artifact_parity/real_projects/manifest.json"
#define ARTIFACT_PARITY_CORPUS_ARCHIVE_ROOT "test_v2/artifact_parity/real_projects/archives"
#define ARTIFACT_PARITY_CORPUS_CONSUMER_ROOT "test_v2/artifact_parity/real_projects/consumers"

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} Artifact_Parity_Corpus_String_List;

typedef struct {
    char *name;
    char *upstream_url;
    char *archive_url;
    char *pinned_ref;
    char *archive_prefix;
    Artifact_Parity_Corpus_String_List retain_paths;
    Artifact_Parity_Corpus_String_List supported_phases;
    Artifact_Parity_Corpus_String_List expected_imported_targets;
    char *support_tier;
} Artifact_Parity_Corpus_Project;

typedef struct {
    Artifact_Parity_Corpus_Project *items;
    size_t count;
    size_t capacity;
} Artifact_Parity_Corpus_Project_List;

bool artifact_parity_corpus_manifest_load(Artifact_Parity_Corpus_Project_List *out_projects);
bool artifact_parity_corpus_manifest_load_path(const char *manifest_path,
                                               Artifact_Parity_Corpus_Project_List *out_projects);
void artifact_parity_corpus_manifest_free(Artifact_Parity_Corpus_Project_List *projects);

const Artifact_Parity_Corpus_Project *artifact_parity_corpus_manifest_find(
    const Artifact_Parity_Corpus_Project_List *projects,
    const char *name);

bool artifact_parity_corpus_project_archive_relpath(
    const Artifact_Parity_Corpus_Project *project,
    char *out_relpath,
    size_t out_relpath_cap);
bool artifact_parity_corpus_project_consumer_relpath(
    const Artifact_Parity_Corpus_Project *project,
    char *out_relpath,
    size_t out_relpath_cap);

bool artifact_parity_corpus_project_has_support_tier(
    const Artifact_Parity_Corpus_Project *project,
    const char *support_tier);
bool artifact_parity_corpus_string_list_contains(
    const Artifact_Parity_Corpus_String_List *list,
    const char *value);
bool artifact_parity_corpus_format_string_list(
    const Artifact_Parity_Corpus_String_List *list,
    char *buffer,
    size_t buffer_size);

#endif // TEST_ARTIFACT_PARITY_CORPUS_MANIFEST_H_
