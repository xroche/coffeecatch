# Contributing to CoffeeCatch

CoffeeCatch turns fatal POSIX signals into recoverable exceptions. It runs on a
crash path, inside a signal handler, in a process that is already damaged. A
bounds, lifetime, or reentrancy bug here is a security bug, not a style nit.
Please read this file before your first patch.

## Build and test

```sh
make          # static lib, shared lib, tests, sample
make check    # build and run the test suite
```

`tests` and `sample` link the static archive, so there is no `LD_LIBRARY_PATH`
to set and the run is identical on Linux and macOS.

## Signal-handler discipline

Code reachable from the handler must stay async-signal-safe as far as
practical. The header documents the two deliberate exceptions the design relies
on: `siglongjmp()` and `pthread_getspecific()`. Do not add `malloc()` or stdio
to the handler path.

Signal dispositions are process-wide; coffeecatch's state is per-thread. Never
use `signal()`/`sigaction()` to express a per-thread decision, or one thread's
catch disarms every other thread (issue #52). Arm per-thread state instead.

Recovery is `siglongjmp()`, which does not unwind C++ frames or run
destructors. The model is "log and die", not "recover and continue". The
`alarm()` armed on catch is a watchdog against deadlocking inside a
non-signal-safe function; cancelling it is the dangerous operation, not the
safe one.

## Platform gating

Backtrace support is Android-only by default:

```c
#ifdef __ANDROID__
#define USE_UNWIND
#define USE_CORKSCREW
#define USE_LIBUNWIND
#endif
```

Off Android none of these exist, so the `native_code_handler_struct` `frames[]`
member and the whole `_Unwind_*` / libcorkscrew / libunwind machinery are
absent. Anything touching `t->frames`, `frames_size`, or `.absolute_pc` must
sit inside the matching guard, and `frames[]` is a `backtrace_frame_t` under
corkscrew but a bare `uintptr_t` under plain unwind. Ask "is this Android-only
or common?" before writing the code: these configurations compile different
sources.

## The COFFEE_TRY block

Rules baked into the macros and documented in `coffeecatch.h`:

- No `return` or exit out of the protected block or the handler; cleanup would
  be skipped.
- Locals used before the block and read after it must be `volatile`.
- The enclosing function should be `extern` or `__attribute__((noinline))`.
- Nested blocks are allowed and throw to the outermost handler. Preserve that
  if you touch the setup/cleanup path.

## Code style

Portable C89-ish, building clean under `-W -Wall -Wextra -Werror
-Wno-unused-function`. A warning is a build break: fix the cause rather than
silencing it with a pragma. Public declarations in the headers carry
`/** ... **/` Doxygen comments; match that when adding API.

Keep comments to one line per block by default; a second line has to earn its
place. Comment the why, not the what. Do not narrate the bug you fixed or the
investigation that found it, in code or in tests. That belongs in the commit
message.

Format only the lines you touch. This tree does not round-trip cleanly through
any current formatter, so a whole-file reformat is churn and will be asked for
removal.

## Tests

`tests.c` (C, linked with `$(CC)`) and `tests_cxx.cpp` (the `COFFEE_CXX_*`
macros, linked with `$(CXX)`) are the two suites behind `make check`. Keep the
C cases in `tests.c`: they `siglongjmp` across their own frames, which is well
defined in C but UB across the C++ frames a `.cpp` file would introduce. Each
case runs in its own forked child, because coffeecatch installs process-global
handlers and per-thread state, and isolation keeps one case's crash from
corrupting the runner. Register new cases in the `tests[]` table of the matching
suite and confirm they actually ran: a silently skipped test is worse than no
test.

Any behaviour change needs a test that would catch the regression. When you
write one, ask what buggy implementation would still pass it. If you can name
one, the test is not doing its job yet.

## CI

The workflow drives the Makefile directly, since that is what rotted unnoticed
before:

- build+test matrix over x86-64 and arm64, gcc and clang. The arm64 leg is the
  closest proxy for the Android arm64 target.
- a `-DUSE_UNWIND` leg on x86-64, the only coverage the unwind path and its
  nested-fault guard get outside an NDK build.
- UBSan only, deliberately. ASan's own signal handling and alternate stack
  cannot coexist with coffeecatch's handlers and per-thread altstack: every
  intentional crash then dies uncaught. UBSan installs no handlers. Please do
  not add an ASan leg.
- no macOS, and Android is not built at all.

Pull requests from forks need maintainer approval before CI runs, so your first
push will show no checks until someone approves them.

## Things that bite

- `coffeejni.c` needs `<jni.h>` and is built by neither the Makefile nor CI.
  "It built" never means the JNI layer built; validate it against an NDK or JDK
  include path separately.
- This is Android-first code. Green CI on Linux is a real but partial signal:
  symbolication and the Bionic `ucontext`/`sigcontext` quirks are exactly what
  every non-Android leg skips.
- The two headers are the public surface, consumed downstream as a git
  submodule. Treat any signature or macro change as an external break: flag it
  in the PR and expect discussion before it lands.

## Pull requests

Keep the description short: what changed and why. One logical change per PR. If
your work uncovers a pre-existing bug, file it separately rather than folding
the fix in.
