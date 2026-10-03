#ifndef BPP_LINT_DATACHECK_H
#define BPP_LINT_DATACHECK_H

/*
 * Data-consistency pass: open the control file's seqfile / Imapfile and check
 * that what they contain agrees with what the control file declares. Every
 * check mirrors a fatal() in BPP 4.8.7 that fires AFTER the control file has
 * parsed cleanly, so a file that passes the syntax checks alone can still be
 * rejected by BPP. Codes BPP150-BPP157 (BPP155, the phase digit count, needs
 * no data and lives in lint.c's cross-keyword rules).
 *
 * Runs for inference control files only: in --simulate mode seqfile and
 * Imapfile are OUTPUTS that BPP writes, not inputs.
 */

#include <stddef.h>

#include "imap.h"
#include "lex.h"
#include "lint.h"
#include "seqfile.h"

/* One data file named by the control file. */
typedef struct {
    const bpp_line_t *line;     /* the 'seqfile = ...' / 'imapfile = ...' line, or NULL */
    char *name;                 /* the value as written */
    char *path_ctl;             /* resolved against the control file's directory
                                 * (bpp-lint's default, friendlier for editors) */
    char *path_cwd;             /* as BPP opens it: relative to the current
                                 * working directory (phylip.c:283 / parsemap.c:237
                                 * fopen the name verbatim) */
    char *abs;                  /* absolute path of the file used (or looked for) */
    int   ctl_exists;           /* path_ctl can be opened */
    int   cwd_exists;           /* path_cwd can be opened */
    int   same;                 /* both resolutions name the same file */
    int   used_cwd;             /* loaded from path_cwd because path_ctl failed */
    int   loaded;               /* parsed successfully */
    int   parse_failed;         /* opened but not parseable */
} bpp_data_file_t;

typedef struct {
    bpp_data_file_t seq;
    bpp_data_file_t imap;
    bpp_alignment_t al;          /* valid when seq.loaded */
    bpp_imap_t      map;         /* valid when imap.loaded */

    int    n_species;            /* N from the species&tree header (0 if absent) */
    char **species;              /* the header names (malloc'd) */
    int    n_species_names;

    char **imap_species;         /* distinct Imap species (alias into map; free the array only) */
    size_t n_imap_species;
    int    n_distinct_tags;      /* distinct sequence tags across all loci */
} bpp_data_t;

/* Resolve the data paths named in `f` (located at `cfile_path`) and load
 * whatever can be loaded. Never prints and never fails the run: open/parse
 * problems are recorded in the struct for bpp_data_check to report. The
 * alignment is read exactly once; --check-priors reuses d->al / d->map.
 * `d` must be zero-initialised. Returns 0, or -1 on allocation failure. */
int  bpp_data_load(const bpp_file_t *f, const char *cfile_path, bpp_data_t *d);

/* Emit BPP150-BPP154, BPP156, BPP157 into `out`. Returns the number of
 * error-severity diagnostics added. */
int  bpp_data_check(const bpp_file_t *f, const bpp_data_t *d, bpp_diag_list_t *out);

void bpp_data_free(bpp_data_t *d);

#endif
