/* CoffeeCatch C++ test suite.
 *
 * Copyright (c) 2013, Xavier Roche (http://www.httrack.com/)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the conditions in the LICENSE
 * file are met.
 *
 * Companion to tests.c: exercises the COFFEE_CXX_* macros. Kept separate so the
 * C suite stays C (no siglongjmp across C++ frames, and CC/clang coverage is
 * preserved). Each case runs in its own forked child, as in tests.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "coffeecatch.h"

/* COFFEE_TRY requires its enclosing function to be non-inlined. */
#define NOINLINE __attribute__ ((noinline))

/* Fail the current test with context. Only valid outside a TRY/CATCH block. */
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "    check failed: %s (%s:%d)\n",                 \
              #cond, __FILE__, __LINE__);                               \
      return 1;                                                         \
    }                                                                   \
  } while (0)

/* Unmapped first-page address; volatile so the store is not folded away. */
static volatile uintptr_t bad_addr = 0x100;
#define CRASH() (*(volatile int *) bad_addr = 1)

/* The sentinel-ran check below is CHECK(!coffeecatch_inside()). That is safe
 * despite coffeecatch_inside() not being idempotent: its t->reenter++ is guarded
 * by reenter>0, so on the passing path (cleanup ran, reenter==0) it returns 0
 * without mutating; the increment only fires when a leak is present, i.e. when
 * the case is already failing out. */

/* --- individual cases: return 0 on success, 1 on failure ----------------- */

static NOINLINE int test_cxx_segv(void) {
  volatile int caught = 0, sig = 0;
  COFFEE_CXX_TRY() {
    CRASH();
  } COFFEE_CXX_CATCH() {
    caught = 1;
    sig = coffeecatch_get_signal();
    coffeecatch_cancel_pending_alarm();
  } COFFEE_CXX_END();
  CHECK(!coffeecatch_inside());   /* the sentinel ran the cleanup (see note above) */
  CHECK(caught);
  CHECK(sig == SIGSEGV);
  return 0;
}

/* A caught signal arms the deadlock alarm; cancel reports success (rc 0) only
 * when an alarm was actually pending, so this asserts the alarm state. */
static NOINLINE int test_cxx_alarm_armed_on_signal(void) {
  volatile int caught = 0, rc = -1;
  COFFEE_CXX_TRY() {
    CRASH();
  } COFFEE_CXX_CATCH() {
    caught = 1;
    rc = coffeecatch_cancel_pending_alarm();
  } COFFEE_CXX_END();
  CHECK(!coffeecatch_inside());
  CHECK(caught);
  CHECK(rc == 0);
  return 0;
}

/* A C++ throw inside the protected block skips COFFEE_CXX_CATCH() and unwinds
 * to the enclosing C++ catch; the sentinel still runs the cleanup on the way. */
static NOINLINE int test_cxx_throw_inside_try(void) {
  volatile int coffee_caught = 0, reached = 0, cxx_caught = 0;
  try {
    COFFEE_CXX_TRY() {
      reached = 1;
      throw 1;
    } COFFEE_CXX_CATCH() {
      coffee_caught = 1;
    } COFFEE_CXX_END();
  } catch (int c) {
    cxx_caught = c;
  }
  CHECK(!coffeecatch_inside());   /* the sentinel cleaned up on the way out */
  CHECK(reached);
  CHECK(!coffee_caught);
  CHECK(cxx_caught);
  return 0;
}

/* Re-raising a signal as a C++ exception from COFFEE_CXX_CATCH() reaches the
 * enclosing catch. */
static NOINLINE int test_cxx_throw_inside_catch(void) {
  volatile int coffee_caught = 0, cxx_caught = 0;
  try {
    COFFEE_CXX_TRY() {
      coffeecatch_abort("deliberate", __FILE__, __LINE__);
    } COFFEE_CXX_CATCH() {
      coffee_caught = 1;
      int sig = coffeecatch_get_signal();
      throw sig;
    } COFFEE_CXX_END();
  } catch (int c) {
    cxx_caught = c;
    coffeecatch_cancel_pending_alarm();
  }
  CHECK(!coffeecatch_inside());
  CHECK(coffee_caught);
  CHECK(cxx_caught == SIGABRT);
  return 0;
}

/* Return straight out of the protected block. The plain COFFEE_TRY() forbids
 * this (cleanup would be skipped); the sentinel makes it safe. */
static NOINLINE void returns_from_cxx_try(volatile int *const reached) {
  COFFEE_CXX_TRY() {
    *reached = 1;
    return;
  } COFFEE_CXX_CATCH() {
    /* not reached: no signal */
  } COFFEE_CXX_END();
}

static NOINLINE int test_cxx_return_from_try(void) {
  volatile int reached = 0;
  returns_from_cxx_try(&reached);
  CHECK(reached);
  CHECK(!coffeecatch_inside());   /* the sentinel ran cleanup despite the early return */
  return 0;
}

/* Nested COFFEE_CXX_TRY must compile (the sentinel names differ, so an embedder
 * building with -Wshadow can nest them) and both sentinels must balance the
 * cleanup. Neither block crashes on purpose: a signal in a nested block unwinds
 * to the outermost handler over the inner sentinel -- that is the siglongjmp
 * caveat, covered by the C suite's test_nested, not a nesting feature. */
static NOINLINE int test_cxx_nested(void) {
  volatile int outer_reached = 0, inner_reached = 0;
  volatile int outer_caught = 0, inner_caught = 0;
  COFFEE_CXX_TRY() {
    outer_reached = 1;
    COFFEE_CXX_TRY() {
      inner_reached = 1;
    } COFFEE_CXX_CATCH() {
      inner_caught = 1;
    } COFFEE_CXX_END();
  } COFFEE_CXX_CATCH() {
    outer_caught = 1;
  } COFFEE_CXX_END();
  CHECK(!coffeecatch_inside());   /* both sentinels ran; reenter is back to 0 */
  CHECK(outer_reached);
  CHECK(inner_reached);
  CHECK(!inner_caught);
  CHECK(!outer_caught);
  return 0;
}

/* --- fork harness -------------------------------------------------------- */

struct test {
  const char *name;
  int (*fn)(void);
  const char *skip;   /* non-NULL: reason the case is not run */
};

static const struct test tests[] = {
  { "C++: segv (null deref)",           test_cxx_segv,                 NULL },
  { "C++: alarm armed on signal",       test_cxx_alarm_armed_on_signal, NULL },
  { "C++: throw inside try",            test_cxx_throw_inside_try,     NULL },
  { "C++: throw inside catch",          test_cxx_throw_inside_catch,   NULL },
  { "C++: return from try cleans up",   test_cxx_return_from_try,      NULL },
  { "C++: nested try",                  test_cxx_nested,               NULL },
};

static int run_forked(const struct test *t) {
  pid_t pid;
  int status = 0;

  if (t->skip != NULL) {
    printf("  SKIP  %s (%s)\n", t->name, t->skip);
    return 0;
  }

  fflush(stdout);
  fflush(stderr);
  pid = fork();
  if (pid < 0) {
    perror("fork");
    return 1;
  }
  if (pid == 0) {
    const int rc = t->fn();
    fflush(NULL);
    _exit(rc == 0 ? 0 : 1);
  }
  if (waitpid(pid, &status, 0) != pid) {
    perror("waitpid");
    return 1;
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    printf("  PASS  %s\n", t->name);
    return 0;
  }
  if (WIFSIGNALED(status)) {
    printf("  FAIL  %s (uncaught signal %d)\n", t->name, WTERMSIG(status));
  } else {
    printf("  FAIL  %s (exit %d)\n", t->name, WEXITSTATUS(status));
  }
  return 1;
}

int main(int argc, char **argv) {
  const size_t n = sizeof(tests) / sizeof(tests[0]);
  size_t i;
  int failures = 0;
  (void) argc;
  (void) argv;

  printf("running %zu coffeecatch C++ tests...\n", n);
  for (i = 0; i < n; i++) {
    failures += run_forked(&tests[i]);
  }
  if (failures == 0) {
    printf("all %zu tests passed\n", n);
    return EXIT_SUCCESS;
  }
  printf("%d of %zu tests FAILED\n", failures, n);
  return EXIT_FAILURE;
}
