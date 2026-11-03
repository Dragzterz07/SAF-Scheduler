#ifndef DEQUE_H
#define DEQUE_H

#include <stdbool.h>
#include <stdint.h>

#include "tcb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WSDeque WSDeque;

// Allocate a new deque with a fixed capacity of 1024 slots. Returns NULL on
// allocation failure.
WSDeque *deque_create(void);

// Free the deque. Must not be called while steal() is in progress.
void deque_destroy(WSDeque *d);

/* Push a thread onto the back (owner only). Returns true on success, false if
 * the deque is full or t is NULL*/
bool deque_push(WSDeque *d, TCB *t);

/*  Pop a thread from the back (owner only). Returns the thread, or NULL if the
 * deque is empty (or lost CAS race).*/
TCB *deque_pop(WSDeque *d);

/* Steal a thread from the front (any non-owner worker). Returns the thread, or
 * NULL if empty or lost CAS race.*/
TCB *deque_steal(WSDeque *d);

// Approximate number of elements. May be stale immediately after return.
int64_t deque_size(WSDeque *d);

#ifdef __cplusplus
}
#endif

#endif
