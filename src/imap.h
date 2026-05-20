#ifndef BPP_LINT_IMAP_H
#define BPP_LINT_IMAP_H

#include <stddef.h>

typedef struct {
    char *individual;
    char *species;
} bpp_imap_entry_t;

typedef struct {
    bpp_imap_entry_t *entries;
    size_t            n;
    size_t            cap;
} bpp_imap_t;

/* Load an Imap file at `path`. Each non-empty, non-comment line is
 * '<individual>\s+<species>'. Returns 0 on success, -1 on I/O failure or
 * malformed content. */
int  bpp_imap_load(bpp_imap_t *m, const char *path);

/* Look up the species assigned to `individual`. Returns NULL if not mapped. */
const char *bpp_imap_lookup(const bpp_imap_t *m, const char *individual);

/* All distinct species names in insertion order. The returned strings alias
 * those stored in `m->entries`; do not free. `*out_n` is set to the count. */
char **bpp_imap_species_list(const bpp_imap_t *m, size_t *out_n);

void bpp_imap_free(bpp_imap_t *m);

#endif
