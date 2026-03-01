#include "eval_file_backend_archive.h"

#include "sv_utils.h"
#include "tinydir.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#if defined(EVAL_HAVE_LIBARCHIVE)
#include <archive.h>
#include <archive_entry.h>
#endif

static void archive_log_line(Nob_String_Builder *sb, const char *text) {
    if (!sb || !text || text[0] == '\0') return;
    if (sb->count > 0 && sb->items[sb->count - 1] != '\n') nob_sb_append(&*sb, '\n');
    nob_sb_append_cstr(sb, text);
}

static bool archive_finalize_log(Evaluator_Context *ctx, Nob_String_Builder *sb, String_View *out_log) {
    if (!ctx || !sb || !out_log) return false;
    if (sb->count == 0) {
        *out_log = nob_sv_from_cstr("");
        return true;
    }
    *out_log = sv_copy_to_temp_arena(ctx, nob_sv_from_parts(sb->items, sb->count));
    return !ctx->oom;
}

static String_View archive_basename_sv(String_View path) {
    size_t base = 0;
    for (size_t i = 0; i < path.count; i++) {
        if (path.data[i] == '/' || path.data[i] == '\\') base = i + 1;
    }
    return nob_sv_from_parts(path.data + base, path.count - base);
}

static String_View archive_path_join_unix_temp(Evaluator_Context *ctx, String_View a, String_View b) {
    if (!ctx) return nob_sv_from_cstr("");
    if (a.count == 0) return b;
    if (b.count == 0) return a;

    char *buf = (char*)arena_alloc(eval_temp_arena(ctx), a.count + b.count + 2);
    EVAL_OOM_RETURN_IF_NULL(ctx, buf, nob_sv_from_cstr(""));

    memcpy(buf, a.data, a.count);
    size_t off = a.count;
    if (off > 0 && buf[off - 1] != '/') buf[off++] = '/';
    memcpy(buf + off, b.data, b.count);
    off += b.count;
    buf[off] = '\0';

    for (size_t i = 0; i < off; i++) {
        if (buf[i] == '\\') buf[i] = '/';
    }
    return nob_sv_from_parts(buf, off);
}

static bool archive_glob_match(String_View pat, String_View text) {
    size_t pi = 0;
    size_t ti = 0;
    size_t star_pi = (size_t)-1;
    size_t star_ti = (size_t)-1;

    while (ti < text.count) {
        if (pi < pat.count) {
            char pc = pat.data[pi];
            char tc = text.data[ti];
            if (pc == '*') {
                star_pi = pi++;
                star_ti = ti;
                continue;
            }
            if (pc == '?') {
                pi++;
                ti++;
                continue;
            }
            if (pc == tc) {
                pi++;
                ti++;
                continue;
            }
        }

        if (star_pi != (size_t)-1) {
            pi = star_pi + 1;
            ti = ++star_ti;
            continue;
        }
        return false;
    }

    while (pi < pat.count && pat.data[pi] == '*') pi++;
    return pi == pat.count;
}

#if defined(EVAL_HAVE_LIBARCHIVE)
static bool archive_set_create_format(struct archive *a,
                                      String_View format,
                                      int *out_status,
                                      Nob_String_Builder *log_sb) {
    if (!a || !out_status || !log_sb) return false;

    int rc = ARCHIVE_FAILED;
    if (eval_sv_eq_ci_lit(format, "7ZIP")) rc = archive_write_set_format_7zip(a);
    else if (eval_sv_eq_ci_lit(format, "GNUTAR") || eval_sv_eq_ci_lit(format, "TAR")) rc = archive_write_set_format_gnutar(a);
    else if (eval_sv_eq_ci_lit(format, "PAX")) rc = archive_write_set_format_pax(a);
    else if (eval_sv_eq_ci_lit(format, "PAXR")) rc = archive_write_set_format_pax_restricted(a);
    else if (eval_sv_eq_ci_lit(format, "RAW")) rc = archive_write_set_format_raw(a);
    else if (eval_sv_eq_ci_lit(format, "ZIP")) rc = archive_write_set_format_zip(a);

    if (rc != ARCHIVE_OK) {
        *out_status = 1;
        archive_log_line(log_sb, "libarchive: unsupported ARCHIVE_CREATE FORMAT");
        return true;
    }
    return true;
}

static bool archive_set_create_compression(struct archive *a,
                                           String_View compression,
                                           int *out_status,
                                           Nob_String_Builder *log_sb) {
    if (!a || !out_status || !log_sb) return false;
    if (compression.count == 0 || eval_sv_eq_ci_lit(compression, "NONE")) return true;

    int rc = ARCHIVE_FAILED;
    if (eval_sv_eq_ci_lit(compression, "GZIP")) rc = archive_write_add_filter_gzip(a);
    else if (eval_sv_eq_ci_lit(compression, "BZIP2")) rc = archive_write_add_filter_bzip2(a);
    else if (eval_sv_eq_ci_lit(compression, "XZ")) rc = archive_write_add_filter_xz(a);
    else if (eval_sv_eq_ci_lit(compression, "ZSTD")) rc = archive_write_add_filter_zstd(a);
    else if (eval_sv_eq_ci_lit(compression, "LZMA")) rc = archive_write_add_filter_lzma(a);

    if (rc != ARCHIVE_OK) {
        *out_status = 1;
        archive_log_line(log_sb, "libarchive: unsupported ARCHIVE_CREATE COMPRESSION");
        return true;
    }

    return true;
}

static bool archive_copy_file_data(struct archive *a, const char *src_c) {
    if (!a || !src_c) return false;
    FILE *f = fopen(src_c, "rb");
    if (!f) return false;

    bool ok = true;
    char buf[16 * 1024];
    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), f);
        if (n > 0) {
            la_ssize_t wr = archive_write_data(a, buf, n);
            if (wr < 0 || (size_t)wr != n) {
                ok = false;
                break;
            }
        }
        if (n < sizeof(buf)) {
            if (ferror(f)) ok = false;
            break;
        }
    }

    fclose(f);
    return ok;
}

static bool archive_add_path_recursive(Evaluator_Context *ctx,
                                       struct archive *a,
                                       String_View src_path,
                                       String_View entry_name,
                                       const Eval_File_Archive_Create_Options *opt,
                                       Nob_String_Builder *log_sb,
                                       int *out_status) {
    if (!ctx || !a || !opt || !log_sb || !out_status) return false;

    char *src_c = eval_sv_to_cstr_temp(ctx, src_path);
    char *entry_c = eval_sv_to_cstr_temp(ctx, entry_name);
    EVAL_OOM_RETURN_IF_NULL(ctx, src_c, false);
    EVAL_OOM_RETURN_IF_NULL(ctx, entry_c, false);

    struct stat st = {0};
#if defined(_WIN32)
    if (stat(src_c, &st) != 0) {
#else
    if (lstat(src_c, &st) != 0) {
#endif
        *out_status = 1;
        archive_log_line(log_sb, "libarchive: failed to stat path during ARCHIVE_CREATE");
        return true;
    }

    struct archive_entry *e = archive_entry_new();
    if (!e) {
        *out_status = 1;
        archive_log_line(log_sb, "libarchive: failed to allocate archive_entry");
        return true;
    }

    archive_entry_set_pathname(e, entry_c);
    archive_entry_set_perm(e, st.st_mode & 07777);
    archive_entry_set_uid(e, st.st_uid);
    archive_entry_set_gid(e, st.st_gid);
    if (opt->has_mtime) archive_entry_set_mtime(e, (time_t)opt->mtime_epoch, 0);
    else archive_entry_set_mtime(e, st.st_mtime, 0);

    if (S_ISREG(st.st_mode)) {
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_size(e, st.st_size);
    } else if (S_ISDIR(st.st_mode)) {
        archive_entry_set_filetype(e, AE_IFDIR);
        archive_entry_set_size(e, 0);
    }
#if !defined(_WIN32) && defined(S_ISLNK)
    else if (S_ISLNK(st.st_mode)) {
        archive_entry_set_filetype(e, AE_IFLNK);
        archive_entry_set_size(e, 0);
        char link_target[4096];
        ssize_t ln = readlink(src_c, link_target, sizeof(link_target) - 1);
        if (ln >= 0) {
            link_target[ln] = '\0';
            archive_entry_set_symlink(e, link_target);
        }
    }
#endif
    else {
        archive_entry_set_filetype(e, AE_IFREG);
        archive_entry_set_size(e, 0);
    }

    int rc = archive_write_header(a, e);
    if (rc != ARCHIVE_OK) {
        archive_entry_free(e);
        *out_status = 1;
        archive_log_line(log_sb, archive_error_string(a));
        return true;
    }

    if (S_ISREG(st.st_mode)) {
        if (!archive_copy_file_data(a, src_c)) {
            archive_entry_free(e);
            *out_status = 1;
            archive_log_line(log_sb, "libarchive: failed to write file content");
            return true;
        }
    }

    if (opt->verbose) {
        archive_log_line(log_sb, entry_c);
    }

    archive_entry_free(e);

    if (!S_ISDIR(st.st_mode)) return true;

    tinydir_dir dir = {0};
    if (tinydir_open(&dir, src_c) != 0) {
        *out_status = 1;
        archive_log_line(log_sb, "libarchive: failed to open directory for recursion");
        return true;
    }

    bool ok = true;
    while (dir.has_next) {
        tinydir_file f = {0};
        if (tinydir_readfile(&dir, &f) != 0) {
            ok = false;
            break;
        }
        if (tinydir_next(&dir) != 0 && dir.has_next) {
            ok = false;
            break;
        }
        if (strcmp(f.name, ".") == 0 || strcmp(f.name, "..") == 0) continue;

        String_View child_name = nob_sv_from_cstr(f.name);
        String_View child_src = eval_sv_path_join(eval_temp_arena(ctx), src_path, child_name);
        String_View child_entry = archive_path_join_unix_temp(ctx, entry_name, child_name);
        if (ctx->oom) {
            ok = false;
            break;
        }

        if (!archive_add_path_recursive(ctx, a, child_src, child_entry, opt, log_sb, out_status)) {
            ok = false;
            break;
        }
        if (*out_status != 0) {
            ok = true;
            break;
        }
    }

    tinydir_close(&dir);
    return ok;
}

bool eval_file_backend_archive_create(Evaluator_Context *ctx,
                                      const Eval_File_Archive_Create_Options *opt,
                                      int *out_status_code,
                                      String_View *out_log) {
    if (!ctx || !opt || !out_status_code || !out_log) return false;
    *out_status_code = 1;
    *out_log = nob_sv_from_cstr("");

    struct archive *a = archive_write_new();
    if (!a) {
        *out_status_code = 1;
        *out_log = nob_sv_from_cstr("libarchive: archive_write_new failed");
        return true;
    }

    Nob_String_Builder log_sb = {0};

    if (!archive_set_create_format(a, opt->format, out_status_code, &log_sb)) {
        archive_write_free(a);
        nob_sb_free(log_sb);
        return false;
    }
    if (!archive_set_create_compression(a, opt->compression, out_status_code, &log_sb)) {
        archive_write_free(a);
        nob_sb_free(log_sb);
        return false;
    }

    if (opt->has_compression_level) {
        char options[64] = {0};
        snprintf(options, sizeof(options), "compression-level=%ld", opt->compression_level);
        (void)archive_write_set_options(a, options);
    }

    char *out_c = eval_sv_to_cstr_temp(ctx, opt->output);
    EVAL_OOM_RETURN_IF_NULL(ctx, out_c, false);

    if (archive_write_open_filename(a, out_c) != ARCHIVE_OK) {
        *out_status_code = 1;
        archive_log_line(&log_sb, archive_error_string(a));
        archive_write_free(a);
        bool ok = archive_finalize_log(ctx, &log_sb, out_log);
        nob_sb_free(log_sb);
        return ok;
    }

    *out_status_code = 0;
    for (size_t i = 0; i < opt->paths.count; i++) {
        String_View src = opt->paths.items[i];
        String_View base = archive_basename_sv(src);
        if (!archive_add_path_recursive(ctx, a, src, base, opt, &log_sb, out_status_code)) {
            archive_write_close(a);
            archive_write_free(a);
            nob_sb_free(log_sb);
            return false;
        }
        if (*out_status_code != 0) break;
    }

    archive_write_close(a);
    archive_write_free(a);

    bool ok = archive_finalize_log(ctx, &log_sb, out_log);
    nob_sb_free(log_sb);
    return ok;
}

static bool archive_extract_should_include(const Eval_File_Archive_Extract_Options *opt, String_View entry_path) {
    if (!opt) return false;
    if (opt->patterns.count == 0) return true;
    for (size_t i = 0; i < opt->patterns.count; i++) {
        if (archive_glob_match(opt->patterns.items[i], entry_path)) return true;
    }
    return false;
}

static int archive_copy_data(struct archive *ar, struct archive *aw) {
    const void *buff = NULL;
    size_t size = 0;
    la_int64_t offset = 0;

    for (;;) {
        int r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF) return ARCHIVE_OK;
        if (r != ARCHIVE_OK) return r;
        r = archive_write_data_block(aw, buff, size, offset);
        if (r != ARCHIVE_OK) {
            return r;
        }
    }
}

bool eval_file_backend_archive_extract(Evaluator_Context *ctx,
                                       const Eval_File_Archive_Extract_Options *opt,
                                       int *out_status_code,
                                       String_View *out_log) {
    if (!ctx || !opt || !out_status_code || !out_log) return false;
    *out_status_code = 1;
    *out_log = nob_sv_from_cstr("");

    Nob_String_Builder log_sb = {0};

    struct archive *ar = archive_read_new();
    struct archive *aw = archive_write_disk_new();
    if (!ar || !aw) {
        if (ar) archive_read_free(ar);
        if (aw) archive_write_free(aw);
        *out_status_code = 1;
        *out_log = nob_sv_from_cstr("libarchive: allocation failed");
        return true;
    }

    archive_read_support_format_all(ar);
    archive_read_support_filter_all(ar);

    archive_write_disk_set_options(aw, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
    archive_write_disk_set_standard_lookup(aw);

    char *in_c = eval_sv_to_cstr_temp(ctx, opt->input);
    EVAL_OOM_RETURN_IF_NULL(ctx, in_c, false);

    if (archive_read_open_filename(ar, in_c, 10240) != ARCHIVE_OK) {
        *out_status_code = 1;
        archive_log_line(&log_sb, archive_error_string(ar));
        archive_read_free(ar);
        archive_write_free(aw);
        bool ok = archive_finalize_log(ctx, &log_sb, out_log);
        nob_sb_free(log_sb);
        return ok;
    }

    *out_status_code = 0;

    struct archive_entry *entry = NULL;
    int r = ARCHIVE_OK;
    while ((r = archive_read_next_header(ar, &entry)) == ARCHIVE_OK) {
        const char *entry_name_c = archive_entry_pathname(entry);
        if (!entry_name_c) {
            (void)archive_read_data_skip(ar);
            continue;
        }

        String_View entry_name = nob_sv_from_cstr(entry_name_c);
        if (!archive_extract_should_include(opt, entry_name)) {
            (void)archive_read_data_skip(ar);
            continue;
        }

        if (opt->verbose || opt->list_only) {
            archive_log_line(&log_sb, entry_name_c);
        }

        if (opt->list_only) {
            (void)archive_read_data_skip(ar);
            continue;
        }

        String_View dst = eval_sv_path_join(eval_temp_arena(ctx), opt->destination, entry_name);
        char *dst_c = eval_sv_to_cstr_temp(ctx, dst);
        EVAL_OOM_RETURN_IF_NULL(ctx, dst_c, false);
        archive_entry_set_pathname(entry, dst_c);

        if (opt->touch) {
            archive_entry_set_mtime(entry, time(NULL), 0);
        }

        r = archive_write_header(aw, entry);
        if (r != ARCHIVE_OK) {
            *out_status_code = 1;
            archive_log_line(&log_sb, archive_error_string(aw));
            break;
        }

        r = archive_copy_data(ar, aw);
        if (r != ARCHIVE_OK) {
            *out_status_code = 1;
            archive_log_line(&log_sb, archive_error_string(aw));
            break;
        }

        r = archive_write_finish_entry(aw);
        if (r != ARCHIVE_OK) {
            *out_status_code = 1;
            archive_log_line(&log_sb, archive_error_string(aw));
            break;
        }
    }

    if (r != ARCHIVE_EOF && r != ARCHIVE_OK && *out_status_code == 0) {
        *out_status_code = 1;
        archive_log_line(&log_sb, archive_error_string(ar));
    }

    archive_read_close(ar);
    archive_read_free(ar);
    archive_write_close(aw);
    archive_write_free(aw);

    bool ok = archive_finalize_log(ctx, &log_sb, out_log);
    nob_sb_free(log_sb);
    return ok;
}

#else

bool eval_file_backend_archive_create(Evaluator_Context *ctx,
                                      const Eval_File_Archive_Create_Options *opt,
                                      int *out_status_code,
                                      String_View *out_log) {
    (void)ctx;
    (void)opt;
    if (out_status_code) *out_status_code = 1;
    if (out_log) *out_log = nob_sv_from_cstr("libarchive backend not compiled (define EVAL_HAVE_LIBARCHIVE=1)");
    return false;
}

bool eval_file_backend_archive_extract(Evaluator_Context *ctx,
                                       const Eval_File_Archive_Extract_Options *opt,
                                       int *out_status_code,
                                       String_View *out_log) {
    (void)ctx;
    (void)opt;
    if (out_status_code) *out_status_code = 1;
    if (out_log) *out_log = nob_sv_from_cstr("libarchive backend not compiled (define EVAL_HAVE_LIBARCHIVE=1)");
    return false;
}

#endif
