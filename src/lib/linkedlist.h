
#ifndef LINKEDLIST_H
#define LINKEDLIST_H

#include <stdarg.h>

typedef struct llnode {
    void *data;
    struct llnode *next;
    struct llnode *prev;
} LLNode;

typedef struct linkedlist {
    LLNode *head;
    LLNode *tail;
    LLNode *current;
} LinkedList;

// Constructor/Destructor
LinkedList *linkedlist_create(void);
void linkedlist_free(LinkedList *list);
// void linkedlist_free_ctx(LinkedList *list, void (*free_data)(void*, void*), void *ctx);
void linkedlist_free_ctx(LinkedList *list, void (*free_data)(void*, va_list), ...);

// Add functions
void linkedlist_push_head(LinkedList *list, void *item);
void linkedlist_push_tail(LinkedList *list, void *item);

// Navigation
void linkedlist_forward(LinkedList *list);
void linkedlist_back(LinkedList *list);
void linkedlist_start(LinkedList *list);
void linkedlist_end(LinkedList *list);

// Access
void *linkedlist_get_current(const LinkedList *list);
int linkedlist_is_empty(const LinkedList *list);

// Removal
void *linkedlist_pop_head(LinkedList *list);
void *linkedlist_pop_tail(LinkedList *list);
void *linkedlist_pop_current(LinkedList *list);

#endif

