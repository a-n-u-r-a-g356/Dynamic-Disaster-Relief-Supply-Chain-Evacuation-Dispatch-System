/* queue.h - FIFO queue of void* (linked list). The queue never owns the data. */
#ifndef QUEUE_H
#define QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct QNode {
    void *data;
    struct QNode *next;
} QNode;

typedef struct {
    QNode *head;
    QNode *tail;
    int size;
} Queue;

Queue *queue_create(void);
void   queue_free(Queue *q);               /* frees nodes only, not the data */
int    queue_push(Queue *q, void *data);   /* returns 0 on allocation failure */
void  *queue_pop(Queue *q);                /* NULL when empty */
int    queue_size(const Queue *q);

#ifdef __cplusplus
}
#endif
#endif
