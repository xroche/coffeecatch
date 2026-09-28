/* CoffeeCatch JNI layer test.
 *
 * Copyright (c) 2013, Xavier Roche (http://www.httrack.com/)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the conditions in the LICENSE
 * file are met.
 *
 * Runs coffeejni.c against a fake JNIEnv, so no JVM is needed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include <jni.h>
#include "coffeecatch.h"
#include "coffeejni.h"

/* COFFEE_TRY requires its enclosing function to be non-inlined. */
#define NOINLINE __attribute__ ((noinline))

/* Only valid outside a TRY/CATCH block. */
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "    check failed: %s (%s:%d)\n",                 \
              #cond, __FILE__, __LINE__);                               \
      return 1;                                                         \
    }                                                                   \
  } while (0)

/* Each local ref is a slot that is never reused, so a stale use shows. */
typedef struct ref {
  int live;
  const char *sig;      /* constructor signature, for objects */
  char text[64];        /* NewStringUTF() content */
  struct ref *args[2];  /* constructor arguments */
  jsize length;         /* NewObjectArray() length */
  jsize stored;         /* SetObjectArrayElement() calls */
  struct ref *trace;    /* setStackTrace() argument */
} ref;

static ref refs[1024];
static size_t nrefs;
static int live_refs, jni_errors;
static ref *thrown;

static const char ste_ctor[] =
  "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)V";
static const char set_stack_trace[] = "([Ljava/lang/StackTraceElement;)V";

static ref *new_ref(void) {
  if (nrefs == sizeof(refs) / sizeof(refs[0])) {
    fprintf(stderr, "    out of fake local refs\n");
    exit(1);
  }
  refs[nrefs].live = 1;
  live_refs++;
  return &refs[nrefs++];
}

/* A NULL, deleted or foreign ref is a JNI error. */
static ref *use(void *p) {
  ref *const r = (ref*) p;
  if (r == NULL || r < refs || r >= refs + nrefs || !r->live) {
    jni_errors++;
    return NULL;
  }
  return r;
}

/* A method ID is its signature string. */
static jmethodID fake_GetMethodID(JNIEnv *env, jclass cls, const char *name,
                                  const char *sig) {
  (void) env; (void) name;
  use(cls);
  return (jmethodID) sig;
}

static jclass fake_FindClass(JNIEnv *env, const char *name) {
  (void) env; (void) name;
  return (jclass) new_ref();
}

static jstring fake_NewStringUTF(JNIEnv *env, const char *s) {
  ref *const r = new_ref();
  (void) env;
  snprintf(r->text, sizeof(r->text), "%s", s);
  return (jstring) r;
}

static jobject fake_NewObject(JNIEnv *env, jclass cls, jmethodID m, ...) {
  const char *const sig = (const char*) m;
  ref *const r = new_ref();
  va_list ap;
  (void) env;
  use(cls);
  r->sig = sig;
  va_start(ap, m);
  r->args[0] = use(va_arg(ap, jobject));
  if (strstr(sig, "Throwable") != NULL) {
    r->args[1] = use(va_arg(ap, jobject));
  }
  va_end(ap);
  return (jobject) r;
}

static jobjectArray fake_NewObjectArray(JNIEnv *env, jsize length, jclass cls,
                                        jobject init) {
  ref *const r = new_ref();
  (void) env; (void) init;
  use(cls);
  r->length = length;
  return (jobjectArray) r;
}

static void fake_SetObjectArrayElement(JNIEnv *env, jobjectArray array,
                                       jsize index, jobject value) {
  ref *const a = use(array);
  const ref *const v = use(value);
  (void) env;
  if (v == NULL || v->sig == NULL || strcmp(v->sig, ste_ctor) != 0) {
    jni_errors++;
  } else if (a != NULL && index >= 0 && index < a->length) {
    a->stored++;
  }
}

static void fake_CallVoidMethod(JNIEnv *env, jobject obj, jmethodID m, ...) {
  ref *const r = use(obj);
  va_list ap;
  (void) env;
  if (strcmp((const char*) m, set_stack_trace) != 0) {
    jni_errors++;
  }
  va_start(ap, m);
  if (r != NULL) {
    r->trace = use(va_arg(ap, jobject));
  }
  va_end(ap);
}

static jint fake_Throw(JNIEnv *env, jthrowable obj) {
  (void) env;
  thrown = use(obj);
  return 0;
}

static jint fake_ThrowNew(JNIEnv *env, jclass cls, const char *msg) {
  (void) env; (void) cls; (void) msg;
  jni_errors++;
  return 0;
}

static void fake_DeleteLocalRef(JNIEnv *env, jobject obj) {
  ref *const r = obj != NULL ? use(obj) : NULL;
  (void) env;
  if (r != NULL) {
    r->live = 0;
    live_refs--;
  }
}

static struct JNINativeInterface_ functions;
static JNIEnv env = &functions;

static volatile uintptr_t bad_addr = 0x100;

static NOINLINE void crash(void) {
  *(volatile int *) bad_addr = 1;
}

/* A JNI native method that crashes once. */
static NOINLINE void native_method(void) {
  COFFEE_TRY_JNI(&env, crash());
  coffeecatch_cancel_pending_alarm();  /* the process lives on after a catch */
}

static int check_thrown(void) {
  CHECK(thrown != NULL);
  CHECK(thrown->args[0] != NULL);
  CHECK(strncmp(thrown->args[0]->text, "signal ", 7) == 0);
/* Only these builds record a backtrace. */
#if defined(__ANDROID__) || defined(__APPLE__) || defined(USE_UNWIND)
  {
    /* Error(message, cause), where cause carries the native stack trace. */
    const ref *const cause = thrown->args[1];
    CHECK(cause != NULL);
    CHECK(cause->trace != NULL);
    CHECK(cause->trace->length > 2);
    CHECK(cause->trace->stored == cause->trace->length);
  }
#endif
  return 0;
}

int main(void) {
  int i;
  functions.FindClass = fake_FindClass;
  functions.GetMethodID = fake_GetMethodID;
  functions.NewStringUTF = fake_NewStringUTF;
  functions.NewObject = fake_NewObject;
  functions.NewObjectArray = fake_NewObjectArray;
  functions.SetObjectArrayElement = fake_SetObjectArrayElement;
  functions.CallVoidMethod = fake_CallVoidMethod;
  functions.Throw = fake_Throw;
  functions.ThrowNew = fake_ThrowNew;
  functions.DeleteLocalRef = fake_DeleteLocalRef;

  /* Local refs pile up across catches until the native call returns. */
  for (i = 0; i < 3; i++) {
    thrown = NULL;
    native_method();
    if (check_thrown() != 0) {
      return 1;
    }
    CHECK(jni_errors == 0);
  }
  printf("jni: %d local refs left\n", live_refs);
  return 0;
}
