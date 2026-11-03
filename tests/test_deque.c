/* tests/test_deque.c — Work-Stealing deque.
 * Tests:
 *   1. Basic push/pop LIFO order (owner only)
 *   2. Steal takes from the front (FIFO)
 *   3. Pop on empty returns NULL
 *   4. Steal on empty returns NULL
 *   5. Full deque rejects push, returns false
 *   6. 10,000 push/pop cycles without error
 *   7. Concurrent steal vs pop on a single-element deque (CAS race test) */

#include "deque.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* We use small integer casts as fake TCB pointers in tests — the deque
 * only stores and returns pointers, it never dereferences them. */
#define FAKE(n) ((TCB *)(uintptr_t)(n))

// 1. Basic push / pop (LIFO)
static void test_push_pop_lifo(void) {
  WSDeque *d = deque_create();
  assert(d != NULL);

  assert(deque_push(d, FAKE(1)));
  assert(deque_push(d, FAKE(2)));
  assert(deque_push(d, FAKE(3)));

  /* pop returns from the back → LIFO order: 3, 2, 1 */
  assert(deque_pop(d) == FAKE(3));
  assert(deque_pop(d) == FAKE(2));
  assert(deque_pop(d) == FAKE(1));
  assert(deque_pop(d) == NULL); /* now empty */

  deque_destroy(d);
}

// 2. Steal takes from the front (FIFO)
static void test_steal_fifo(void) {
  WSDeque *d = deque_create();
  assert(d != NULL);

  assert(deque_push(d, FAKE(10)));
  assert(deque_push(d, FAKE(20)));
  assert(deque_push(d, FAKE(30)));

  // steal takes from the front → FIFO order: 10, 20, 30
  assert(deque_steal(d) == FAKE(10));
  assert(deque_steal(d) == FAKE(20));
  assert(deque_steal(d) == FAKE(30));
  assert(deque_steal(d) == NULL);

  deque_destroy(d);
}

// 3 & 4. Empty checks
static void test_empty(void) {
  WSDeque *d = deque_create();
  assert(d != NULL);
  assert(deque_pop(d) == NULL);
  assert(deque_steal(d) == NULL);
  assert(deque_size(d) == 0);
  deque_destroy(d);
}

// 5. Full deque rejects push
static void test_full(void) {
  WSDeque *d = deque_create();
  assert(d != NULL);

  // Fill to capacity (1024 slots)
  for (int i = 0; i < 1024; i++) {
    assert(deque_push(d, FAKE(i + 1)));
  }
  // One more should fail
  assert(deque_push(d, FAKE(9999)) == false);
  assert(deque_size(d) == 1024);

  deque_destroy(d);
}

// 6. 10,000 push/pop cycles
static void test_cycles(void) {
  WSDeque *d = deque_create();
  assert(d != NULL);

  for (int i = 0; i < 10000; i++) {
    assert(deque_push(d, FAKE(i + 1)));
    TCB *got = deque_pop(d);
    assert(got == FAKE(i + 1));
  }
  assert(deque_pop(d) == NULL);

  deque_destroy(d);
}

// 7. Concurrent steal vs pop on 1 element (CAS race)

static WSDeque *g_race_deque;
static TCB *g_stolen;

static void *thief_thread(void *arg) {
  (void)arg;
  // Spin until we get something or the deque is gone
  TCB *t = NULL;
  for (int i = 0; i < 100000 && t == NULL; i++) {
    t = deque_steal(g_race_deque);
  }
  g_stolen = t;
  return NULL;
}

static void test_concurrent_race(void) {
  g_race_deque = deque_create();
  assert(g_race_deque != NULL);

  assert(deque_push(g_race_deque, FAKE(42)));

  // Spawn a thief that tries to steal the single element
  pthread_t thief;
  pthread_create(&thief, NULL, thief_thread, NULL);

  // Owner also tries to pop it
  TCB *popped = deque_pop(g_race_deque);

  pthread_join(thief, NULL);

  // Exactly one of them must have gotten the element; the other gets NULL
  int owner_got = (popped == FAKE(42));
  int thief_got = (g_stolen == FAKE(42));
  assert(owner_got + thief_got == 1); /* exactly one winner */

  deque_destroy(g_race_deque);
}

// NULL push rejected
static void test_null_push(void) {
  WSDeque *d = deque_create();
  assert(d != NULL);
  assert(deque_push(d, NULL) == false);
  assert(deque_size(d) == 0);
  deque_destroy(d);
}

int main(void) {
  test_push_pop_lifo();
  test_steal_fifo();
  test_empty();
  test_full();
  test_cycles();
  test_concurrent_race();
  test_null_push();

  puts("PASS test_deque");
  return 0;
}
