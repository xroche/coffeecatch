###############################################################################
#
# CoffeeCatch — tiny native POSIX signal catcher
#
###############################################################################

# --- Toolchain (overridable) -------------------------------------------------
# cc/ar are the POSIX defaults; CI overrides CC with clang or "gcc -m32".
CC      ?= cc
AR      ?= ar
RM      ?= rm -f

# --- Flags -------------------------------------------------------------------
# CFLAGS holds the user-overridable optimization/debug flags. Everything
# mandatory (PIC, reentrancy, strict warnings) is appended with `override` so
# it survives a command-line CFLAGS=... — e.g. CI injecting sanitizer flags.
CFLAGS  ?= -O3 -g

override CPPFLAGS += -D_REENTRANT -D_GNU_SOURCE
override CFLAGS   += -fPIC -pthread \
                    -W -Wall -Wextra -Werror -Wno-unused-function

# --- Platform shared-library wiring ------------------------------------------
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  SHLIB   := libcoffeecatch.dylib
  SOFLAGS := -dynamiclib -install_name @rpath/$(SHLIB)
  SOLIBS  :=
  LDLIBS  :=
else
  SHLIB   := libcoffeecatch.so
  SOFLAGS := -shared -Wl,-soname=$(SHLIB) -Wl,--no-undefined -rdynamic
  SOLIBS  := -ldl
  LDLIBS  := -ldl
endif

STATICLIB := libcoffeecatch.a

# --- Sources -----------------------------------------------------------------
# coffeejni.c is intentionally excluded: it needs <jni.h> (an NDK/JDK header)
# and is meant to be compiled into the embedder, not this standalone build.
LIBSRC := coffeecatch.c
LIBOBJ := $(LIBSRC:.c=.o)
BINS   := tests sample

# --- Targets -----------------------------------------------------------------
.PHONY: all check test clean dist
.DEFAULT_GOAL := all

all: $(STATICLIB) $(SHLIB) $(BINS)

%.o: %.c coffeecatch.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.cpp coffeecatch.h
	$(CXX) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(STATICLIB): $(LIBOBJ)
	$(AR) rcs $@ $^

$(SHLIB): $(LIBOBJ)
	$(CC) $(CFLAGS) $(SOFLAGS) $(LIBOBJ) -o $@ $(LDFLAGS) $(SOLIBS) $(LDLIBS)

# tests/sample link the static archive: no LD_LIBRARY_PATH, identical run on
# Linux and macOS.
tests: tests.o $(STATICLIB)
	$(CXX) $(CFLAGS) $< $(STATICLIB) -o $@ $(LDFLAGS) $(LDLIBS)
sample: sample.o $(STATICLIB)
	$(CC) $(CFLAGS) $< $(STATICLIB) -o $@ $(LDFLAGS) $(LDLIBS)

check test: tests
	./tests

dist:
	$(RM) coffeecatch.tgz
	tar cvfz coffeecatch.tgz $(LIBSRC) coffeecatch.h coffeejni.c coffeejni.h \
		sample.c tests.c Makefile LICENSE README.md

clean:
	$(RM) *.o $(STATICLIB) $(SHLIB) libcoffeecatch.so.* $(BINS) coffeecatch.tgz
