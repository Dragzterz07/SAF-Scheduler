/* Design choices
 * 1. int64_t -> Standard over cpp libs
 * 2. atomic -> Self Explanatory
 * 3. atomic operations (store(), load()) -> See above + prevent memory
 * reordering
 * 4. Why no full() empty() funcs -> After they run, front and back may change
 * 5. pop() and steal() -> Explained at relevant section below */
#include "deque.h"
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>

// Fixed capacity: 1024 slots (1 << 10).
#define DEQUE_MAX_SIZE (INT64_C(1) << 10)

struct WSDeque {
  _Atomic int64_t front;
  _Atomic int64_t back;
  _Atomic(TCB *) array[DEQUE_MAX_SIZE];
};

// Allocate and zero-initialise a new deque. Returns NULL on failure.
WSDeque *deque_create(void) {
  WSDeque *d = calloc(1, sizeof(WSDeque));
  if (d == NULL) {
    return NULL;
  }
  atomic_init(&d->front, 0);
  atomic_init(&d->back, 0);
  for (int64_t i = 0; i < DEQUE_MAX_SIZE; i++) {
    atomic_init(&d->array[i], NULL);
  }
  return d;
}

// Free the deque. Must not be called while steal() may still be running.
void deque_destroy(WSDeque *d) {
  if (d != NULL) {
    free(d);
  }
}

/*  Pushes a new TCB* thread onto the back.Returns true if succeed, false if
 * fails (deque full or t is NULL). */
bool deque_push(WSDeque *d, TCB *t) {
  if (t == NULL) {
    return false;
  }

  int64_t curr_f = atomic_load(&d->front);
  int64_t curr_b = atomic_load(&d->back);

  if (curr_b - curr_f >= DEQUE_MAX_SIZE) {
    return false;
  }

  atomic_store(&d->array[curr_b % DEQUE_MAX_SIZE], t);
  atomic_store(&d->back, curr_b + 1);
  return true;
}

/*  5. pop() and steal() !!!! Possibility of race condition due to steal() vs
 * pop() Flow of program in comments explain how they mitigate this */

/* Owner thread tries to remove element from the back.
 * Returns the element if succeed, NULL if the deque is empty. */
TCB *deque_pop(WSDeque *d) {
  int64_t curr_b = atomic_load(&d->back) - 1; // Claiming first
  atomic_store(&d->back, curr_b);

  int64_t curr_f = atomic_load(&d->front);

  // Deque was empty or it got stolen
  if (curr_f > curr_b) {
    atomic_store(&d->back, curr_b + 1);
    return NULL;
  }
  TCB *elem = atomic_load(&d->array[curr_b % DEQUE_MAX_SIZE]);

  // Last element race condition with thief
  if (curr_f == curr_b) {
    // Compare and Swap
    if (!atomic_compare_exchange_strong(&d->front, &curr_f, curr_f + 1)) {
      elem = NULL; // lost the race; a thief took it
    }
    // Restore back to front so the deque is consistently empty.
    atomic_store(&d->back, curr_b + 1); // back = front
  }
  return elem;
}

/* Non-owner thread tries to remove element from the front. Returns the element
 * if succeed, NULL if empty or CAS lost. */
TCB *deque_steal(WSDeque *d) {
  int64_t curr_f = atomic_load(&d->front);
  int64_t curr_b = atomic_load(&d->back);

  if (curr_f >= curr_b) {
    return NULL;
  }

  TCB *elem = atomic_load(&d->array[curr_f % DEQUE_MAX_SIZE]);

  // Compare and Swap
  if (!atomic_compare_exchange_strong(&d->front, &curr_f, curr_f + 1)) {
    return NULL;
  }
  return elem;
}

/* Returns an approximate count of elements in the deque. This value may be
 * stale by the time the caller uses it (design choice 4).*/
int64_t deque_size(WSDeque *d) {
  int64_t b = atomic_load(&d->back);
  int64_t f = atomic_load(&d->front);
  int64_t sz = b - f;
  return sz < 0 ? 0 : sz;
}
