#include "queue.h"
#include <stdlib.h>

Queue *queue_create(void) {
    Queue *q = (Queue *)malloc(sizeof(Queue));
    if (!q) return NULL;
    q->head = q->tail = NULL;
    q->size = 0;
    return q;
}

void queue_free(Queue *q) {
    if (!q) return;
    QNode *n = q->head;
    while (n) { QNode *nx = n->next; free(n); n = nx; }
    free(q);
}

int queue_push(Queue *q, void *data) {
    if (!q) return 0;
    QNode *n = (QNode *)malloc(sizeof(QNode));
    if (!n) return 0;
    n->data = data; n->next = NULL;
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    q->size++;
    return 1;
}

void *queue_pop(Queue *q) {
    if (!q || !q->head) return NULL;
    QNode *n = q->head;
    void *d = n->data;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    free(n);
    q->size--;
    return d;
}

int queue_size(const Queue *q) { return q ? q->size : 0; }
