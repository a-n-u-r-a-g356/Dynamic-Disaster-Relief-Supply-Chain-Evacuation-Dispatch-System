#include "heap.h"
#include <stdlib.h>

static int better(const Heap *h, double x, double y) { return h->isMax ? x > y : x < y; }

static void swap_items(Heap *h, int i, int j) {
    HeapItem t = h->a[i]; h->a[i] = h->a[j]; h->a[j] = t;
    h->pos[h->a[i].id] = i;
    h->pos[h->a[j].id] = j;
}

static void sift_up(Heap *h, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!better(h, h->a[i].key, h->a[p].key)) break;
        swap_items(h, i, p);
        i = p;
    }
}

static void sift_down(Heap *h, int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, b = i;
        if (l < h->size && better(h, h->a[l].key, h->a[b].key)) b = l;
        if (r < h->size && better(h, h->a[r].key, h->a[b].key)) b = r;
        if (b == i) break;
        swap_items(h, i, b);
        i = b;
    }
}

Heap *heap_create(int maxId, int isMax) {
    if (maxId < 0) maxId = 0;
    Heap *h = (Heap *)malloc(sizeof(Heap));
    if (!h) return NULL;
    h->a = (HeapItem *)malloc(sizeof(HeapItem) * (size_t)(maxId + 1));
    h->pos = (int *)malloc(sizeof(int) * (size_t)(maxId + 1));
    if (!h->a || !h->pos) { free(h->a); free(h->pos); free(h); return NULL; }
    for (int i = 0; i <= maxId; i++) h->pos[i] = -1;
    h->size = 0; h->maxId = maxId; h->isMax = isMax;
    return h;
}

void heap_free(Heap *h) {
    if (!h) return;
    free(h->a); free(h->pos); free(h);
}

int heap_push_or_update(Heap *h, int id, double key) {
    if (!h || id < 0 || id > h->maxId) return 0;
    int i = h->pos[id];
    if (i >= 0) {
        h->a[i].key = key;
        sift_up(h, i);
        sift_down(h, h->pos[id]);
        return 1;
    }
    i = h->size++;
    h->a[i].id = id; h->a[i].key = key; h->pos[id] = i;
    sift_up(h, i);
    return 1;
}

int heap_pop(Heap *h, HeapItem *out) {
    if (!h || h->size == 0) return 0;
    if (out) *out = h->a[0];
    h->pos[h->a[0].id] = -1;
    h->size--;
    if (h->size > 0) {
        h->a[0] = h->a[h->size];
        h->pos[h->a[0].id] = 0;
        sift_down(h, 0);
    }
    return 1;
}

int heap_peek(const Heap *h, HeapItem *out) {
    if (!h || h->size == 0) return 0;
    if (out) *out = h->a[0];
    return 1;
}

int heap_empty(const Heap *h) { return !h || h->size == 0; }
