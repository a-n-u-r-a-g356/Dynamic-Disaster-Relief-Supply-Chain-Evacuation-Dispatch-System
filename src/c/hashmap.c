#include "hashmap.h"
#include <stdlib.h>
#include <string.h>

static unsigned long hash_str(const char *s) {          /* djb2 */
    unsigned long h = 5381;
    int c;
    while ((c = (unsigned char)*s++)) h = ((h << 5) + h) + (unsigned long)c;
    return h;
}

static char *copy_str(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

HashMap *hm_create(int initialCap) {
    HashMap *m = (HashMap *)malloc(sizeof(HashMap));
    if (!m) return NULL;
    if (initialCap < 8) initialCap = 8;
    m->buckets = (HNode **)calloc((size_t)initialCap, sizeof(HNode *));
    if (!m->buckets) { free(m); return NULL; }
    m->cap = initialCap;
    m->size = 0;
    return m;
}

void hm_free(HashMap *m) {
    if (!m) return;
    for (int i = 0; i < m->cap; i++) {
        HNode *n = m->buckets[i];
        while (n) { HNode *nx = n->next; free(n->key); free(n); n = nx; }
    }
    free(m->buckets);
    free(m);
}

static void resize(HashMap *m) {
    int ncap = m->cap * 2;
    HNode **nb = (HNode **)calloc((size_t)ncap, sizeof(HNode *));
    if (!nb) return;                                    /* keep working at the old size */
    for (int i = 0; i < m->cap; i++) {
        HNode *n = m->buckets[i];
        while (n) {
            HNode *nx = n->next;
            unsigned long b = hash_str(n->key) % (unsigned long)ncap;
            n->next = nb[b];
            nb[b] = n;
            n = nx;
        }
    }
    free(m->buckets);
    m->buckets = nb;
    m->cap = ncap;
}

int hm_put(HashMap *m, const char *key, int val) {
    if (!m || !key) return -1;
    unsigned long b = hash_str(key) % (unsigned long)m->cap;
    for (HNode *n = m->buckets[b]; n; n = n->next)
        if (strcmp(n->key, key) == 0) { n->val = val; return 0; }
    HNode *n = (HNode *)malloc(sizeof(HNode));
    if (!n) return -1;
    n->key = copy_str(key);
    if (!n->key) { free(n); return -1; }
    n->val = val;
    n->next = m->buckets[b];
    m->buckets[b] = n;
    m->size++;
    if (m->size > m->cap * 2) resize(m);
    return 1;
}

int hm_get(const HashMap *m, const char *key, int *out) {
    if (!m || !key) return 0;
    unsigned long b = hash_str(key) % (unsigned long)m->cap;
    for (HNode *n = m->buckets[b]; n; n = n->next)
        if (strcmp(n->key, key) == 0) { if (out) *out = n->val; return 1; }
    return 0;
}

int hm_remove(HashMap *m, const char *key) {
    if (!m || !key) return 0;
    unsigned long b = hash_str(key) % (unsigned long)m->cap;
    HNode **link = &m->buckets[b];
    while (*link) {
        HNode *n = *link;
        if (strcmp(n->key, key) == 0) { *link = n->next; free(n->key); free(n); m->size--; return 1; }
        link = &n->next;
    }
    return 0;
}
