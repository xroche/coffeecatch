# CLAUDE.md

Read `CONTRIBUTING.md` first. It carries the build commands, the
signal-handler discipline, the platform-gating model, the `COFFEE_TRY` usage
rules, and the CI rationale, and all of it applies to you. This file adds only
what an agent gets wrong that a human reader does not.

## Before writing code

Establish whether the code you are touching is Android-only (`USE_UNWIND`,
`USE_CORKSCREW`, `USE_LIBUNWIND`) or common. Reasoning about `t->frames` in a
configuration where the member does not exist is the most common wasted pass
on this repo.

`make check` passing on Linux says nothing about a macOS or Android change.
State plainly which configurations you actually built, and never describe an
unbuilt one as working.

## Comments

One line per block by default. Two only if the second earns it. Write them
terse the first time, then re-read every comment you added and cut before
showing a diff: delete anything that restates the code, re-derives the bug, or
narrates how you got there.

This is the most common correction on machine-written patches here.

## Scope

Do one thing. If a build fix would also turn on a new feature, or a new API
would also restructure the test suite, split the second thing into its own PR
and say why.

Do not reformat untouched lines, modernize the C89-ish style, or add a
sanitizer leg to CI without reading why UBSan is alone there.

## Claims

Verify before asserting. If you say it builds, build it. If you say a test
catches a regression, reintroduce the bug and watch the test fail. An
unverified claim in a PR description costs more review time than the patch
saves.
