/* Unit tests for the C data-structure library. Build/run with: make test */
#include "graph.h"
#include "hashmap.h"
#include "heap.h"
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void test_heap(void) {
    Heap *h = heap_create(9, 1);
    heap_push_or_update(h, 3, 10); heap_push_or_update(h, 5, 50); heap_push_or_update(h, 7, 30);
    HeapItem it;
    CHECK(heap_peek(h, &it) && it.id == 5);
    heap_push_or_update(h, 3, 99);                      /* update existing key */
    CHECK(heap_pop(h, &it) && it.id == 3 && it.key == 99);
    CHECK(heap_pop(h, &it) && it.id == 5);
    CHECK(heap_pop(h, &it) && it.id == 7);
    CHECK(!heap_pop(h, &it) && heap_empty(h));
    CHECK(!heap_push_or_update(h, 42, 1));              /* out-of-range id rejected */
    heap_free(h);

    Heap *mn = heap_create(4, 0);
    for (int i = 0; i < 5; i++) heap_push_or_update(mn, i, 10 - i);
    CHECK(heap_pop(mn, &it) && it.id == 4);
    heap_free(mn);
}

static void test_hashmap(void) {
    HashMap *m = hm_create(8);
    char key[32];
    for (int i = 0; i < 500; i++) { snprintf(key, sizeof key, "id%d", i); CHECK(hm_put(m, key, i) == 1); }
    int v = -1;
    CHECK(hm_get(m, "id321", &v) && v == 321);
    CHECK(hm_put(m, "id321", 7) == 0 && hm_get(m, "id321", &v) && v == 7);
    CHECK(!hm_get(m, "nope", &v));
    CHECK(hm_remove(m, "id10") && !hm_get(m, "id10", &v) && !hm_remove(m, "id10"));
    CHECK(m->size == 499);
    hm_free(m);
}

static void test_graph(void) {
    /* 0-1 (4), 0-2 (1), 2-1 (2), 1-3 (5); node 4 isolated */
    Graph *g = graph_create(5);
    graph_add_road(g, 0, 1, 4); graph_add_road(g, 0, 2, 1);
    graph_add_road(g, 2, 1, 2); graph_add_road(g, 1, 3, 5);
    CHECK(!graph_add_road(g, 0, 9, 1));
    double dist[5]; int prev[5], path[5];
    dijkstra(g, 0, dist, prev);
    CHECK(dist[1] == 3 && dist[3] == 8);
    int len = graph_path(g, prev, dist, 3, path);
    CHECK(len == 4 && path[0] == 0 && path[1] == 2 && path[2] == 1 && path[3] == 3);
    CHECK(graph_path(g, prev, dist, 4, path) == 0);     /* unreachable */
    graph_free(g);
}

static void test_queue(void) {
    Queue *q = queue_create();
    int a = 1, b = 2;
    queue_push(q, &a); queue_push(q, &b);
    CHECK(queue_size(q) == 2 && queue_pop(q) == &a && queue_pop(q) == &b && queue_pop(q) == NULL);
    queue_free(q);
}

int main(void) {
    test_heap(); test_hashmap(); test_graph(); test_queue();
    if (failures) { printf("%d test(s) failed\n", failures); return 1; }
    printf("all C data-structure tests passed\n");
    return 0;
}
