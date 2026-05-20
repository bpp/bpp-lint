#include "imap.h"
#include "lex.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reserve(bpp_imap_t *m, size_t need) {
    if (m->n + need <= m->cap) return 0;
    size_t nc = m->cap ? m->cap * 2 : 16;
    while (nc < m->n + need) nc *= 2;
    bpp_imap_entry_t *p = realloc(m->entries, nc * sizeof(*p));
    if (!p) return -1;
    m->entries = p;
    m->cap = nc;
    return 0;
}

int bpp_imap_load(bpp_imap_t *m, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;
    m->entries = NULL;
    m->n = m->cap = 0;

    char *line = NULL;
    size_t cap = 0;
    while (1) {
        /* read one line */
        size_t used = 0;
        int c;
        int eof = 0;
        while ((c = fgetc(fp)) != EOF && c != '\n') {
            if (used + 1 >= cap) {
                size_t nc = cap ? cap * 2 : 128;
                char *p = realloc(line, nc);
                if (!p) { free(line); fclose(fp); return -1; }
                line = p;
                cap = nc;
            }
            line[used++] = (char) c;
        }
        if (c == EOF) eof = 1;
        if (used == 0 && eof) break;
        if (used + 1 > cap) {
            cap = used + 1;
            char *p = realloc(line, cap);
            if (!p) { free(line); fclose(fp); return -1; }
            line = p;
        }
        line[used] = '\0';

        /* strip comments (# or *) and trim */
        for (size_t i = 0; i < used; i++) {
            if (line[i] == '#' || line[i] == '*') { line[i] = '\0'; break; }
        }
        const char *s = line;
        while (*s && isspace((unsigned char) *s)) s++;
        if (!*s) { if (eof) break; continue; }

        /* parse two whitespace-separated tokens */
        const char *t1s = s;
        while (*s && !isspace((unsigned char) *s)) s++;
        size_t t1len = (size_t)(s - t1s);
        while (*s && isspace((unsigned char) *s)) s++;
        const char *t2s = s;
        while (*s && !isspace((unsigned char) *s)) s++;
        size_t t2len = (size_t)(s - t2s);
        if (t1len == 0 || t2len == 0) {
            if (eof) break;
            continue;
        }

        if (reserve(m, 1) != 0) { free(line); fclose(fp); return -1; }
        char *indiv = malloc(t1len + 1);
        char *spec  = malloc(t2len + 1);
        if (!indiv || !spec) {
            free(indiv); free(spec); free(line); fclose(fp); return -1;
        }
        memcpy(indiv, t1s, t1len); indiv[t1len] = '\0';
        memcpy(spec,  t2s, t2len); spec[t2len]  = '\0';
        m->entries[m->n].individual = indiv;
        m->entries[m->n].species    = spec;
        m->n++;

        if (eof) break;
    }
    free(line);
    fclose(fp);
    return 0;
}

const char *bpp_imap_lookup(const bpp_imap_t *m, const char *individual) {
    if (!m || !individual) return NULL;
    for (size_t i = 0; i < m->n; i++) {
        if (strcmp(m->entries[i].individual, individual) == 0) {
            return m->entries[i].species;
        }
    }
    return NULL;
}

char **bpp_imap_species_list(const bpp_imap_t *m, size_t *out_n) {
    *out_n = 0;
    if (!m || m->n == 0) return NULL;
    char **out = malloc(m->n * sizeof(*out));
    if (!out) return NULL;
    for (size_t i = 0; i < m->n; i++) {
        const char *s = m->entries[i].species;
        int seen = 0;
        for (size_t j = 0; j < *out_n; j++) {
            if (strcmp(out[j], s) == 0) { seen = 1; break; }
        }
        if (!seen) out[(*out_n)++] = (char *) s;
    }
    return out;
}

void bpp_imap_free(bpp_imap_t *m) {
    if (!m) return;
    for (size_t i = 0; i < m->n; i++) {
        free(m->entries[i].individual);
        free(m->entries[i].species);
    }
    free(m->entries);
    m->entries = NULL;
    m->n = m->cap = 0;
}
