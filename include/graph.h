/* graph.h - weighted undirected graph (adjacency list of linked edges) + Dijkstra */
#ifndef GRAPH_H
#define GRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

#define INF_DIST 1e18

typedef struct Edge {
    int to;
    double cost;
    struct Edge *next;
} Edge;

typedef struct {
    int n;
    Edge **adj;
} Graph;

Graph *graph_create(int n);
void   graph_free(Graph *g);
/* Adds an undirected road. Returns 0 on bad node ids or negative cost. */
int    graph_add_road(Graph *g, int u, int v, double cost);

/* Dijkstra from src. dist[] and prev[] must each hold g->n entries. */
void   dijkstra(const Graph *g, int src, double *dist, int *prev);

/* Writes the node sequence src..dst into out[] (capacity >= g->n).
   Returns its length, or 0 if dst is unreachable. */
int    graph_path(const Graph *g, const int *prev, const double *dist, int dst, int *out);

#ifdef __cplusplus
}
#endif
#endif
