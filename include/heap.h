/* heap.h - indexed binary heap (max or min). pos[] gives O(log n) key updates. */
#ifndef HEAP_H
#define HEAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int id; double key; } HeapItem;

typedef struct {
    HeapItem *a;
    int *pos;      /* pos[id] = index inside a[], or -1 when absent */
    int size;
    int maxId;     /* valid ids are 0..maxId */
    int isMax;     /* 1 = max-heap, 0 = min-heap */
} Heap;

Heap *heap_create(int maxId, int isMax);
void  heap_free(Heap *h);
/* Insert id, or change its key if already present. Returns 0 on bad id. */
int   heap_push_or_update(Heap *h, int id, double key);
int   heap_pop(Heap *h, HeapItem *out);    /* returns 0 if empty */
int   heap_peek(const Heap *h, HeapItem *out);
int   heap_empty(const Heap *h);

#ifdef __cplusplus
}
#endif
#endif
