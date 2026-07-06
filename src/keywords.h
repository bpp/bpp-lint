#ifndef BPP_LINT_KEYWORDS_H
#define BPP_LINT_KEYWORDS_H

/* Classification of a keyword's status in BPP 4.x. */
typedef enum {
    KW_UNKNOWN          = 0,  /* not in any table */
    KW_VALID            = 1,  /* accepted in BPP 4.x as-is */
    KW_VALID_SIM        = 2,  /* accepted in BPP 4.x --simulate mode */
    KW_RENAMED          = 3,  /* pure rename, value format unchanged */
    KW_REPARAMETERISED  = 4,  /* renamed AND value semantics changed */
    KW_REMOVED          = 5,  /* dropped with no direct replacement */
    KW_UNIMPLEMENTED    = 6   /* still tokenised by BPP but aborts at runtime */
} kw_status_t;

/* "Mode" a keyword applies to. INFER and SIM are the two BPP run modes;
 * BOTH means the keyword is accepted in either parser. */
typedef enum {
    MODE_INFER = 1,
    MODE_SIM   = 2,
    MODE_BOTH  = 3
} kw_mode_t;

typedef struct {
    const char  *name;          /* canonical (lowercase) keyword */
    kw_status_t  status;
    kw_mode_t    mode;
    const char  *replacement;   /* new keyword name; NULL if none / unchanged */
    const char  *note;           /* human-readable explanation, may be NULL */
    int          since_version; /* version code where change took effect, e.g. 480 for 4.8.0 */
    const char  *default_value; /* BPP's default when this keyword is absent; NULL if must-set or N/A */
} bpp_keyword_t;

/* Look up a keyword by name (case-insensitive). Returns NULL if unknown. */
const bpp_keyword_t *bpp_keyword_find(const char *name);

/* Iterate the canonical 4.x keyword list, for "did you mean" suggestions.
 * Returns the i-th entry, or NULL if i is out of range. */
const bpp_keyword_t *bpp_keyword_at(int i);

/* Suggest the closest known keyword to `name` using Levenshtein distance.
 * `mode` filters candidates by run mode. Returns NULL if no candidate is
 * within `max_distance` edits. */
const bpp_keyword_t *bpp_keyword_suggest(const char *name, kw_mode_t mode, int max_distance);

/* ---------- value grammar (for generic type/arity/range/enum checking) ----------
 *
 * A keyword's value is described as an ordered list of typed "slots", compiled
 * from the spec's value.grammar by spec/gen_keywords_c.py. The generic value
 * checker in lint.c walks the tokens of a value against these slots. Keywords
 * with an irreducible grammar (alternation, Newick, multi-line blocks) or a
 * bespoke check carry no slots and are skipped here. */
typedef enum {
    VT_END = 0,   /* array terminator */
    VT_BOOL,      /* 0 or 1 */
    VT_INT,       /* integer */
    VT_UINT,      /* non-negative integer */
    VT_FLOAT,     /* real number */
    VT_STRING     /* free token, optionally constrained to `enums` */
} kw_vtype_t;

typedef struct {
    kw_vtype_t   type;
    unsigned     optional : 1;   /* slot may be omitted (trailing optionals) */
    unsigned     repeat   : 1;   /* slot matches 0+ tokens; must be last */
    unsigned     has_min  : 1;
    unsigned     has_max  : 1;
    unsigned     min_excl : 1;   /* bound is exclusive: value > min (else >=) */
    double       min;
    double       max;
    const char  *const *enums;   /* NULL, else NULL-terminated allowed strings */
    const char  *label;          /* parameter name for messages, or NULL */
} kw_slot_t;

typedef struct {
    const char      *name;    /* lowercase keyword */
    const kw_slot_t *slots;   /* VT_END-terminated slot list */
} kw_valuespec_t;

/* Return the VT_END-terminated slot list describing `name`'s value, or NULL if
 * the keyword has no generic value profile (unknown, bespoke, or irreducible). */
const kw_slot_t *bpp_keyword_slots(const char *name);

#endif
