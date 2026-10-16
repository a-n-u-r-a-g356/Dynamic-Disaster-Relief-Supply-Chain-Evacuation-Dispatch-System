/* hashmap.h - string -> int hash map (separate chaining, auto-resize) */
#ifndef HASHMAP_H
#define HASHMAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HNode {
    char *key;
    int val;
    struct HNode *next;
} HNode;

typedef struct {
    HNode **buckets;
    int cap;
    int size;
} HashMap;

HashMap *hm_create(int initialCap);
void     hm_free(HashMap *m);
/* Inserts or overwrites. Returns 1 if a new key was added, 0 if overwritten, -1 on error. */
int      hm_put(HashMap *m, const char *key, int val);
/* Returns 1 and sets *out if found, else 0. */
int      hm_get(const HashMap *m, const char *key, int *out);
/* Returns 1 if the key was removed, else 0. */
int      hm_remove(HashMap *m, const char *key);

#ifdef __cplusplus
}
#endif
#endif
