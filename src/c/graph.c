#include "graph.h"
#include "heap.h"
#include <stdlib.h>

Graph *graph_create(int n) {
    if (n < 0) n = 0;
    Graph *g = (Graph *)malloc(sizeof(Graph));
    if (!g) return NULL;
    g->n = n;
    g->adj = (Edge **)calloc((size_t)(n > 0 ? n : 1), sizeof(Edge *));
    if (!g->adj) { free(g); return NULL; }
    return g;
}

void graph_free(Graph *g) {
    if (!g) return;
    for (int i = 0; i < g->n; i++) {
        Edge *e = g->adj[i];
        while (e) { Edge *nx = e->next; free(e); e = nx; }
    }
    free(g->adj);
    free(g);
}

static int add_directed(Graph *g, int u, int v, double cost) {
    Edge *e = (Edge *)malloc(sizeof(Edge));
    if (!e) return 0;
    e->to = v; e->cost = cost;
    e->next = g->adj[u];
    g->adj[u] = e;
    return 1;
}

int graph_add_road(Graph *g, int u, int v, double cost) {
    if (!g || u < 0 || v < 0 || u >= g->n || v >= g->n || cost < 0) return 0;
    return add_directed(g, u, v, cost) && add_directed(g, v, u, cost);
}

void dijkstra(const Graph *g, int src, double *dist, int *prev) {
    for (int i = 0; i < g->n; i++) { dist[i] = INF_DIST; prev[i] = -1; }
    if (src < 0 || src >= g->n) return;
    Heap *pq = heap_create(g->n - 1, 0);
    if (!pq) return;
    dist[src] = 0;
    heap_push_or_update(pq, src, 0);
    HeapItem it;
    while (heap_pop(pq, &it)) {
        int u = it.id;
        for (Edge *e = g->adj[u]; e; e = e->next) {
            double nd = dist[u] + e->cost;
            if (nd < dist[e->to]) {
                dist[e->to] = nd;
                prev[e->to] = u;
                heap_push_or_update(pq, e->to, nd);     /* decrease-key */
            }
        }
    }
    heap_free(pq);
}

int graph_path(const Graph *g, const int *prev, const double *dist, int dst, int *out) {
    if (dst < 0 || dst >= g->n || dist[dst] >= INF_DIST / 2) return 0;
    int len = 0;
    for (int v = dst; v != -1 && len < g->n; v = prev[v]) out[len++] = v;
    for (int i = 0, j = len - 1; i < j; i++, j--) { int t = out[i]; out[i] = out[j]; out[j] = t; }
    return len;
}
