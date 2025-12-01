#include <stdlib.h>
#include "linkedlist.h"

// Constructor
LinkedList *linkedlist_create(void) {
	LinkedList *list = malloc(sizeof(LinkedList));
	list->current = NULL;
	list->head = NULL;
	list->tail = NULL;
	return list;
}

// Destructor (for primitives)
// If anything else owns referenced data, the result is double fre. usee free_ctx(noop)
void linkedlist_free(LinkedList *list) {
	while (list->head != NULL) {
		free(linkedlist_pop_head(list)); // the owner of void *data from pop must free it
	}
	free(list);
}

void linkedlist_free_ctx(LinkedList *list, void (*free_data)(void*, va_list), ...) {
	va_list args_copy;
	va_list args;
	va_start(args, free_data);

	while (list->head != NULL) {
		va_copy(args_copy, args);  // make a fresh copy for each element
		free_data(linkedlist_pop_head(list), args_copy);
		va_end(args_copy);
	}

	va_end(args);
	free(list);
}

// Add to tail
void linkedlist_push_tail(LinkedList *list, void *item) {
	LLNode *new_node = malloc(sizeof(LLNode));
	new_node->data = item;
	new_node->next = NULL;
	new_node->prev = list->tail;

	if (list->tail != NULL) {
		list->tail->next = new_node;
	} else {
		list->head = new_node;
	}

	list->tail = new_node;
}

// Add to head
void linkedlist_push_head(LinkedList *list, void *item) {
	LLNode *new_node = malloc(sizeof(LLNode));
	new_node->data = item;
	new_node->prev = NULL;
	new_node->next = list->head;

	if (list->head != NULL) {
		list->head->prev = new_node;
	} else {
		list->tail = new_node;
	}

	list->head = new_node;
}

// Move current forward
void linkedlist_forward(LinkedList *list) {
	if (list->current != NULL /*&& list->current != list->tail*/) {
		list->current = list->current->next;
	}
}

// Move current back
void linkedlist_back(LinkedList *list) {
	if (list->current != NULL /*&& list->current != list->head*/) {
		list->current = list->current->prev;
	}
}

// Move current to head
void linkedlist_start(LinkedList *list) {
	list->current = list->head;
}

// Move current to tail
void linkedlist_end(LinkedList *list) {
	list->current = list->tail;
}

// Get current data
void *linkedlist_get_current(const LinkedList *list) {
	return (list->current != NULL) ? list->current->data : NULL;
}

// Check if empty
int linkedlist_is_empty(const LinkedList *list) {
	return list->head == NULL;
}

// Remove from current
void *linkedlist_pop_current(LinkedList *list) {
	if (list->current == NULL) return NULL;

	if (list->current == list->head) {
		return linkedlist_pop_head(list);
	} else if (list->current == list->tail) {
		return linkedlist_pop_tail(list);
	}

	LLNode *left = list->current->prev;
	LLNode *right = list->current->next;
	void *data = list->current->data;

	left->next = right;
	right->prev = left;

	free(list->current);
	list->current = left;

	return data;
}

// Remove from head
void *linkedlist_pop_head(LinkedList *list) {
	if (list->head == NULL) return NULL;

	LLNode *new_head = list->head->next;
	void *data = list->head->data;

	free(list->head);
	list->head = new_head;

	if (new_head != NULL) {
		new_head->prev = NULL;
	} else {
		list->tail = NULL;
	}

	list->current = list->head;
	return data;
}

// Remove from tail
void *linkedlist_pop_tail(LinkedList *list) {
	if (list->tail == NULL) return NULL;

	LLNode *new_tail = list->tail->prev;
	void *data = list->tail->data;

	free(list->tail);
	list->tail = new_tail;

	if (new_tail != NULL) {
		new_tail->next = NULL;
	} else {
		list->head = NULL;
	}

	list->current = list->tail;
	return data;
}

