#include "keywords.h"
#include "lex.h"

#include <stddef.h>

/*
 * Keyword lookup API for BPP 4.x control files.
 *
 * The keyword catalogue itself (`kw_table[]`) is NOT hand-maintained here: it
 * is generated into src/keywords_gen.c from the canonical spec/bpp-syntax.json
 * by spec/gen_keywords_c.py (see spec/README.md). The spec is in turn derived
 * from the BPP release tags (keyword set / context / history) plus authored
 * value grammars and deprecation notes in spec/enrich.json. This file keeps
 * only the stable lookup logic and references the generated table via extern.
 *
 * Regenerate the table with `make gen`; `make check-gen` verifies it is in
 * sync with the spec.
 */
extern const bpp_keyword_t kw_table[];
extern const kw_valuespec_t kw_value_table[];

const bpp_keyword_t *bpp_keyword_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; kw_table[i].name; i++) {
        if (bpp_strieq(kw_table[i].name, name)) return &kw_table[i];
    }
    return NULL;
}

const bpp_keyword_t *bpp_keyword_at(int i) {
    if (i < 0) return NULL;
    int n = 0;
    while (kw_table[n].name) n++;
    if (i >= n) return NULL;
    return &kw_table[i];
}

const bpp_keyword_t *bpp_keyword_suggest(const char *name, kw_mode_t mode, int max_distance) {
    if (!name) return NULL;
    const bpp_keyword_t *best = NULL;
    int best_d = max_distance + 1;
    for (int i = 0; kw_table[i].name; i++) {
        const bpp_keyword_t *k = &kw_table[i];
        /* only suggest currently-valid keywords */
        if (k->status != KW_VALID && k->status != KW_VALID_SIM) continue;
        if ((k->mode & mode) == 0) continue;
        int d = bpp_levenshtein(name, k->name);
        if (d < 0) continue;
        if (d < best_d) {
            best_d = d;
            best = k;
        }
    }
    return (best_d <= max_distance) ? best : NULL;
}

const kw_valuespec_t *bpp_keyword_valuespec(const char *name) {
    if (!name) return NULL;
    for (int i = 0; kw_value_table[i].name; i++) {
        if (bpp_strieq(kw_value_table[i].name, name)) return &kw_value_table[i];
    }
    return NULL;
}

const kw_slot_t *bpp_keyword_slots(const char *name) {
    const kw_valuespec_t *v = bpp_keyword_valuespec(name);
    if (!v || !v->alts || !v->alts[0] || v->alts[1]) return NULL;
    return v->alts[0];
}
