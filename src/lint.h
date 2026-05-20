#ifndef BPP_LINT_LINT_H
#define BPP_LINT_LINT_H

#include "lex.h"
#include "keywords.h"

typedef enum {
    SEV_INFO    = 0,
    SEV_WARNING = 1,
    SEV_ERROR   = 2
} bpp_severity_t;

/* A single diagnostic emitted by the linter. */
typedef struct {
    bpp_severity_t  severity;
    int             lineno;
    int             column;
    char           *code;      /* short code like "BPP001" */
    char           *message;
    char           *suggestion; /* optional, NULL if none */

    /* If the linter knows how to autofix this issue, replacement_line holds
     * the new content for the source line (without trailing newline).
     * replacement_lineno identifies the line to substitute; multi-line fixes
     * are not supported in this version. */
    char           *replacement_line;
    int             replacement_lineno;
} bpp_diagnostic_t;

typedef struct {
    bpp_diagnostic_t *items;
    size_t            n;
    size_t            cap;
} bpp_diag_list_t;

/* Options that influence linting behavior. */
typedef struct {
    int simulate;   /* if non-zero, treat as --simulate control file */
    int verbose;
} bpp_lint_opts_t;

/* Lint the file `f` and write diagnostics into `out`. The diagnostic list
 * must be zero-initialised before the call. Returns the number of errors
 * (warnings/infos do not count). */
int  bpp_lint(const bpp_file_t *f, const bpp_lint_opts_t *opts,
              bpp_diag_list_t *out);

/* Print a diagnostic list to stderr in compiler-style format. */
void bpp_diag_print(const bpp_diag_list_t *diags, const char *path);

/* Apply any auto-fixable diagnostics back into the file's line buffers.
 * Returns the number of replacements made. */
int  bpp_apply_fixes(bpp_file_t *f, const bpp_diag_list_t *diags);

/* Write the file out to `path`, reconstructing from line buffers. */
int  bpp_file_write(const bpp_file_t *f, const char *path);

void bpp_diag_list_free(bpp_diag_list_t *list);

#endif
