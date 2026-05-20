#ifndef BPP_LINT_LEX_H
#define BPP_LINT_LEX_H

#include <stddef.h>

/*
 * A "line" parsed from a BPP control file. Owns its buffers.
 *
 *   raw       : the original line, with trailing newline stripped, exactly
 *               as it appeared in the source (including comments and
 *               surrounding whitespace). Used for reconstruction.
 *   key       : lowercased keyword text from the LHS of '=', or NULL if the
 *               line is blank / comment-only / has no '='.
 *   key_orig  : the original-case keyword text. NULL when key is NULL.
 *   value     : everything after '=' up to a comment marker, trimmed.
 *               NULL if line has no '='.
 *   key_col   : 1-based column where the keyword starts in raw.
 *   eq_col    : 1-based column where '=' appears in raw, or 0 if absent.
 *   val_col   : 1-based column where value starts, or 0 if absent.
 *   lineno    : 1-based line number in the source file.
 */
typedef struct {
    char  *raw;
    char  *key;
    char  *key_orig;
    char  *value;
    int    key_col;
    int    eq_col;
    int    val_col;
    int    lineno;
} bpp_line_t;

/* A whole control file, line-by-line. */
typedef struct {
    bpp_line_t *lines;
    size_t      n;
    size_t      cap;
    char       *path;   /* path passed in; not modified after load */
} bpp_file_t;

/* Read `path` into `f`. Returns 0 on success, -1 on I/O failure (errno set).
 * On success, the caller must call bpp_file_free(f). */
int  bpp_file_load(bpp_file_t *f, const char *path);
void bpp_file_free(bpp_file_t *f);

/* Tokenise the value field of a line into whitespace-separated tokens.
 * Writes up to max_tokens pointers into out_tokens, each pointing into a
 * freshly-allocated buffer (free with bpp_tokens_free).
 * Returns the number of tokens produced (may exceed max_tokens, in which
 * case only max_tokens are stored). Negative on error. */
int  bpp_tokenise_value(const char *value, char **out_tokens, int max_tokens);
void bpp_tokens_free(char **tokens, int n);

/* True if s is blank or NULL. */
int  bpp_is_blank(const char *s);

/* Case-insensitive equality. */
int  bpp_strieq(const char *a, const char *b);

/* Allocate a lowercased duplicate of s. */
char *bpp_strdup_lower(const char *s);

/* Allocate a plain duplicate of s. */
char *bpp_strdup(const char *s);

/* Levenshtein distance between a and b (case-insensitive). */
int  bpp_levenshtein(const char *a, const char *b);

#endif
