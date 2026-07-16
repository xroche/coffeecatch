/* CoffeeCatch test suite.
 *
 * Copyright (c) 2013, Xavier Roche (http://www.httrack.com/)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the conditions in the LICENSE
 * file are met.
 *
 * Each case runs in its own forked child: coffeecatch installs process-global
 * signal handlers and keeps per-thread state, so isolation keeps one case's
 * crash (or an uncaught signal) from corrupting the runner or the next case.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "coffeecatch.h"

/* COFFEE_TRY requires its enclosing function to be non-inlined. */
#define NOINLINE __attribute__ ((noinline))

/* Fail the current test with context. Only valid outside a TRY/CATCH block:
 * a return must never cross COFFEE_TRY/CATCH/END or cleanup is skipped. */
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "    check failed: %s (%s:%d)\n",                 \
              #cond, __FILE__, __LINE__);                               \
      return 1;                                                         \
    }                                                                   \
  } while (0)

/* An address in the unmapped first page, so writing through it faults. Kept in
 * a volatile so the compiler cannot fold it and reject the store at compile
 * time (-Warray-bounds). Aligned to int so UBSan does not report the store as
 * misalignment UB before the hardware fault the handler is meant to catch. */
static volatile uintptr_t bad_addr = 0x100;
#define CRASH() (*(volatile int *) bad_addr = 1)

/* --- individual cases: return 0 on success, 1 on failure ----------------- */

static NOINLINE int test_segv(void) {
  volatile int caught = 0, sig = 0, have_msg = 0;
  COFFEE_TRY() {
    CRASH();
  } COFFEE_CATCH() {
    const char *const msg = coffeecatch_get_message();
    caught = 1;
    sig = coffeecatch_get_signal();
    have_msg = msg != NULL && msg[0] != '\0';
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  CHECK(caught);
  CHECK(sig == SIGSEGV);
  CHECK(have_msg);
  return 0;
}

/* raise() rather than a real division trap: integer div-by-zero is itself UB
 * that UBSan reports before the signal, and it does not trap at all on some
 * arches. This exercises the same SIGFPE catch path portably. */
static NOINLINE int test_fpe(void) {
  volatile int caught = 0, sig = 0;
  COFFEE_TRY() {
    raise(SIGFPE);
  } COFFEE_CATCH() {
    caught = 1;
    sig = coffeecatch_get_signal();
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  CHECK(caught);
  CHECK(sig == SIGFPE);
  return 0;
}

static NOINLINE int test_abort(void) {
  volatile int caught = 0, sig = 0;
  COFFEE_TRY() {
    coffeecatch_abort("deliberate", __FILE__, __LINE__);
  } COFFEE_CATCH() {
    caught = 1;
    sig = coffeecatch_get_signal();
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  CHECK(caught);
  CHECK(sig == SIGABRT);
  return 0;
}

static NOINLINE int test_assert_fail(void) {
  volatile int caught = 0, sig = 0;
  COFFEE_TRY() {
    coffeecatch_assert(1 == 2);
  } COFFEE_CATCH() {
    caught = 1;
    sig = coffeecatch_get_signal();
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  CHECK(caught);
  CHECK(sig == SIGABRT);
  return 0;
}

static NOINLINE int test_assert_pass(void) {
  volatile int caught = 0, reached = 0;
  COFFEE_TRY() {
    coffeecatch_assert(1 == 1);
    reached = 1;
  } COFFEE_CATCH() {
    caught = 1;
  } COFFEE_END();
  CHECK(reached);
  CHECK(!caught);
  return 0;
}

static NOINLINE int test_no_crash(void) {
  volatile int caught = 0, reached = 0;
  COFFEE_TRY() {
    reached = 1;
  } COFFEE_CATCH() {
    caught = 1;
  } COFFEE_END();
  CHECK(reached);
  CHECK(!caught);
  return 0;
}

/* A clean catch must leave the thread able to protect another block. */
static NOINLINE int test_reentry(void) {
  volatile int first = 0, second = 0;
  COFFEE_TRY() {
    CRASH();
  } COFFEE_CATCH() {
    first = 1;
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  COFFEE_TRY() {
    CRASH();
  } COFFEE_CATCH() {
    second = 1;
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  CHECK(first);
  CHECK(second);
  return 0;
}

/* A crash inside a nested block unwinds to the outermost handler; the inner
 * CATCH and the rest of the outer TRY body are skipped. */
static NOINLINE int test_nested(void) {
  volatile int outer_caught = 0, inner_caught = 0, outer_after = 0;
  COFFEE_TRY() {
    COFFEE_TRY() {
      CRASH();
    } COFFEE_CATCH() {
      inner_caught = 1;
    } COFFEE_END();
    outer_after = 1;
  } COFFEE_CATCH() {
    outer_caught = 1;
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  CHECK(outer_caught);
  CHECK(!inner_caught);
  CHECK(!outer_after);
  return 0;
}

static NOINLINE int test_cancel_alarm(void) {
  volatile int caught = 0, rc = -1;
  COFFEE_TRY() {
    CRASH();
  } COFFEE_CATCH() {
    caught = 1;
    rc = coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  CHECK(caught);
  CHECK(rc == 0);
  return 0;
}

/* A pre-existing SIG_IGN must not be called back: SIG_IGN is the constant 1,
 * not a function. */
static NOINLINE int test_old_handler_ignore(void) {
  volatile int caught = 0;
  CHECK(signal(SIGSEGV, SIG_IGN) != SIG_ERR);
  COFFEE_TRY() {
    CRASH();
  } COFFEE_CATCH() {
    caught = 1;
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  CHECK(caught);
  return 0;
}

static volatile sig_atomic_t old_handler_ran;

static void plain_old_handler(int code) {
  (void) code;
  old_handler_ran = 1;
}

/* A pre-existing handler installed without SA_SIGINFO still gets called. */
static NOINLINE int test_old_handler_plain(void) {
  volatile int caught = 0;
  old_handler_ran = 0;
  CHECK(signal(SIGSEGV, plain_old_handler) != SIG_ERR);
  COFFEE_TRY() {
    CRASH();
  } COFFEE_CATCH() {
    caught = 1;
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  CHECK(caught);
  CHECK(old_handler_ran);
  return 0;
}

/* macOS doesn't have pthread_barrier_t; use pthread_cond_t to mimic it. */
static pthread_mutex_t crash_mutex;
static pthread_cond_t crash_cond;
static sig_atomic_t thread_count;

static NOINLINE void *thread_body(void *arg) {
  volatile int *const caught = (volatile int *) arg;
  COFFEE_TRY() {
    /* Crash only once every thread holds a catch context: a serialized run
     * would let the handlers be reinstalled between threads, hiding a
     * process-wide disarm. */
    pthread_mutex_lock(&crash_mutex);
    if (--thread_count <= 0) {
      pthread_cond_broadcast(&crash_cond);
    } else {
      pthread_cond_wait(&crash_cond, &crash_mutex);
    }
    pthread_mutex_unlock(&crash_mutex);
    CRASH();
  } COFFEE_CATCH() {
    *caught = coffeecatch_get_signal() == SIGSEGV ? 1 : -1;
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  return NULL;
}

/* The handler is documented thread-safe: concurrent catches must all succeed,
 * and each must report its own signal. */
static NOINLINE int test_threads(void) {
  enum { N = 8 };
  pthread_t th[N];
  volatile int caught[N] = { 0 };
  int i;
  CHECK(pthread_mutex_init(&crash_mutex, NULL) == 0);
  CHECK(pthread_cond_init(&crash_cond, NULL) == 0);
  thread_count = N;
  for (i = 0; i < N; i++) {
    CHECK(pthread_create(&th[i], NULL, thread_body, (void *) &caught[i]) == 0);
  }
  for (i = 0; i < N; i++) {
    CHECK(pthread_join(th[i], NULL) == 0);
  }
  CHECK(pthread_mutex_destroy(&crash_mutex) == 0);
  CHECK(pthread_cond_destroy(&crash_cond) == 0);
  for (i = 0; i < N; i++) {
    CHECK(caught[i] == 1);
  }
  return 0;
}

static volatile int holder_armed;

static NOINLINE void *holder_body(void *arg) {
  (void) arg;
  COFFEE_TRY() {
    holder_armed = 1;
    for (;;) {
      usleep(1000);
    }
  } COFFEE_CATCH() {
    coffeecatch_cancel_pending_alarm();
  } COFFEE_END();
  return NULL;
}

/* The give-up path: a crash with no catch context on this thread must still
 * kill the process, even while another thread keeps the handlers installed. */
static NOINLINE int test_no_context_dies(void) {
  pid_t pid;
  int status = 0;

  fflush(NULL);
  pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    pthread_t th;
    pthread_create(&th, NULL, holder_body, NULL);
    while (!holder_armed) {
      usleep(1000);
    }
    usleep(50000);
    CRASH();          /* outside any COFFEE_TRY: must not be swallowed */
    _exit(0);         /* reached only if the crash was wrongly caught */
  }
  alarm(20);          /* a handler re-entry loop would hang here */
  CHECK(waitpid(pid, &status, 0) == pid);
  alarm(0);
  CHECK(WIFSIGNALED(status));
  return 0;
}

/* --- fork harness -------------------------------------------------------- */

struct test {
  const char *name;
  int (*fn)(void);
  const char *skip;   /* non-NULL: reason the case is not run */
};

static const struct test tests[] = {
  { "segv (null deref)",            test_segv,        NULL },
  { "fpe (raised)",                 test_fpe,         NULL },
  { "abort()",                      test_abort,       NULL },
  { "assert(false)",                test_assert_fail, NULL },
  { "assert(true) passthrough",     test_assert_pass, NULL },
  { "no-crash passthrough",         test_no_crash,    NULL },
  { "reentry after catch",          test_reentry,     NULL },
  { "nested throws to outermost",   test_nested,      NULL },
  { "cancel_pending_alarm",         test_cancel_alarm, NULL },
  { "old handler SIG_IGN",          test_old_handler_ignore, NULL },
  { "old handler without SA_SIGINFO", test_old_handler_plain, NULL },
  { "concurrent catches (threads)", test_threads, NULL },
  { "no context: crash still kills", test_no_context_dies, NULL },
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

  printf("running %zu coffeecatch tests...\n", n);
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
