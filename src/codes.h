#ifndef BPP_LINT_CODES_H
#define BPP_LINT_CODES_H

#include <stdio.h>

/* A documented diagnostic code. */
typedef struct {
    const char *code;     /* short form, e.g. "020" — no 'BPP' prefix */
    const char *summary;  /* one-line description for --list-codes */
    const char *detail;   /* multi-line explanation for --explain */
} bpp_code_doc_t;

/* Print one-line summaries of every code to `fp`. */
void bpp_codes_list(FILE *fp);

/* Look up `code` (accepted as "020", "BPP020", or "20") and write its
 * detail block to `fp`. Returns 0 on success, -1 if not found. */
int  bpp_codes_explain(const char *code, FILE *fp);

#endif
