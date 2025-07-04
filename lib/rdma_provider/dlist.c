#include "dlist.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
size_t dlist_size(const struct dlist *list) { return list->size; }

bool dlist_prepend(struct dlist *list, void *data)
{
	struct dlist_node *node = calloc(1UL, sizeof(struct dlist_node));
	node->data = data;
	node->next = list->head; // Point new node to current head
	node->prev = NULL;
	list->head = node;
	if (++list->size == 1) {
		list->tail = list->head;
	}
	return true;
}

void *dlist_remove_last(struct dlist *list)
{
	if (list->tail == NULL) { // Empty list
		return NULL;
	}

	struct dlist_node *last = list->tail;
	void *data = last->data;

	if (list->head == list->tail) { // Only one element
		list->head = NULL;
		list->tail = NULL;
	} else {
		list->tail = last->prev; // Update tail
		list->tail->next = NULL; // New tail's next points to NULL
	}

	free(last); // Free the removed node
	list->size--;
	return data;
}

bool dlist_is_empty(const struct dlist *list) { return list->size == 0; }

// Create a new list
struct dlist *dlist_create(dlist_compare_fn compare,
			   dlist_destructor_fn destructor)
{
	struct dlist *list = calloc(1UL, sizeof(struct dlist));
	if (!list) {
		return NULL;
	}

	list->head = NULL;
	list->tail = NULL;
	list->size = 0;
	list->compare = compare;
	list->destructor = destructor;
	return list;
}

// Destroy list and optionally its contents
void dlist_destroy(struct dlist *list)
{
	if (!list) {
		return;
	}
	fprintf(stderr, "List size is %lu\n", list->size);
	struct dlist_node *current = list->head;
	while (current) {
		struct dlist_node *next = current->next;
		if (list->destructor) {
			list->destructor(current->data);
		}
		fprintf(stderr, "Freeing\n");
		free(current);
		current = next;
	}
	free(list);
}

// Append element to the end
bool dlist_append(struct dlist *list, void *data)
{
	assert(list != NULL);

	struct dlist_node *node = calloc(1UL, sizeof(struct dlist_node));
	if (!node) {
		return false;
	}

	node->data = data;
	node->next = NULL;
	node->prev = list->tail;

	if (list->tail) {
		list->tail->next = node; // Connect current tail to new node
	} else {
		// If tail is NULL, this is the first node
		list->head = node;
	}
	list->tail = node; // Set new tail
	list->size++;
	return true;
}

// Get first element
void *dlist_get_first(struct dlist *list)
{
	return list && list->head ? list->head->data : NULL;
}

void *dlist_remove_first(struct dlist *list)
{
	if (list->head == NULL) {
		return NULL;
	}
	void *node = list->head->data;
	struct dlist_node *to_free = list->head;
	list->head = list->head->next;
	--list->size;
  if(list->size == 0)
    list->tail = list->head;
	free(to_free);
	return node;
}

// Get last element
void *dlist_get_last(struct dlist *list)
{
	return list && list->tail ? list->tail->data : NULL;
}

// Search for an element
void *dlist_search(const struct dlist *list, const void *key)
{
	assert(list != NULL && list->compare != NULL);

	for (struct dlist_node *current = list->head; current;
	     current = current->next) {
		if (list->compare(current->data, key) == 0) {
			return current->data;
		}
	}
	return NULL;
}

// Merge sort implementation
static struct dlist_node *merge(struct dlist_node *left,
				struct dlist_node *right,
				dlist_compare_fn compare)
{
	if (!left) {
		return right;
	}
	if (!right) {
		return left;
	}

	struct dlist_node *result = NULL;

	if (compare(left->data, right->data) <= 0) {
		result = left;
		result->next = merge(left->next, right, compare);
		if (result->next) {
			result->next->prev = result;
		}
	} else {
		result = right;
		result->next = merge(left, right->next, compare);
		if (result->next) {
			result->next->prev = result;
		}
	}

	return result;
}

static struct dlist_node *split(struct dlist_node *head)
{
	if (!head || !head->next) {
		return NULL;
	}

	struct dlist_node *fast = head->next;
	struct dlist_node *slow = head;

	while (fast && fast->next) {
		fast = fast->next->next;
		slow = slow->next;
	}

	struct dlist_node *mid = slow->next;
	slow->next = NULL;
	if (mid) {
		mid->prev = NULL;
	}
	return mid;
}

static struct dlist_node *merge_sort(struct dlist_node *head,
				     dlist_compare_fn compare)
{
	if (!head || !head->next) {
		return head;
	}

	struct dlist_node *right = split(head);
	head = merge_sort(head, compare);
	right = merge_sort(right, compare);

	return merge(head, right, compare);
}

// Sort the list
void dlist_sort(struct dlist *list)
{
	if (!list || !list->head || !list->compare) {
		return;
	}

	list->head = merge_sort(list->head, list->compare);

	// Update tail and prev pointers
	struct dlist_node *current = list->head;
	while (current->next) {
		current = current->next;
	}
	list->tail = current;
}

// Iterator implementation
struct dlist_iter dlist_iter_create(struct dlist *list)
{
	struct dlist_iter iter = {list->head, list};
	return iter;
}

void *dlist_iter_next(struct dlist_iter *iter)
{
	if (!iter->current) {
		return NULL;
	}

	void *data = iter->current->data;
	iter->current = iter->current->next;
	return data;
}

bool dlist_iter_has_next(const struct dlist_iter *iter)
{
	return iter->current != NULL;
}

void dlist_iter_reset(struct dlist_iter *iter)
{
	iter->current = iter->list->head;
}
