coffeecatch
===========

**CoffeeCatch**, a *tiny* native POSIX signal catcher (especially useful for JNI code on **Android**/Dalvik, but it can be used in non-Java projects)

It allows to "gracefully" recover from a **signal** (`SIGSEGV`, `SIGBUS`...) as if it was an **exception**. It will not gracefully recover from allocator/mutexes corruption etc., however, but at least "most" gentle crashes (null pointer dereferencing, integer division, stack overflow etc.) should be handled without too much troubles.

```c
/** Enter protected section. **/
COFFEE_TRY() {
  /** Try to call 'call_some_native_function'. **/
  call_some_protected_function();
} COFFEE_CATCH() {
  /** Caught a signal: throw Java exception. **/
  /** In pure C projects, you may print an error message (coffeecatch_get_message()). **/
  coffeecatch_throw_exception(env);
} COFFEE_END();
```

You may read the corresponding [discussion](http://blog.httrack.com/blog/2013/08/23/catching-posix-signals-on-android/) about this project.

The handler is thread-safe, but client must have exclusive control on the signal handlers (ie. the library is installing its own signal handlers on top of the existing ones).

**Libraries**

If you want to get useful stack traces, you should build all your libraries with `-funwind-tables` (this adds unwinding information). On ARM, you may also use the `--no-merge-exidx-entries` linker switch, to solve certain issues with unwinding (the switch is possibly not needed anymore). On Android, this can be achieved by using this line in the `Android.mk` file in each library block:
```
  LOCAL_CFLAGS := -funwind-tables -Wl,--no-merge-exidx-entries
```

**Example**

* Inside JNI (typically, Android)

*First, build the library, or just add the two files in the list of local files to be built:*
```
LOCAL_SRC_FILES += coffeecatch.c coffeejni.c
```

*then, use the COFFEE_TRY_JNI() macro to protect your call(s):*

```c
/** The potentially dangerous function. **/
jint call_dangerous_function(JNIEnv* env, jobject object) {
  // ... do dangerous things!
  return 42;
}

/** Protected function stub. **/
void foo_protected(JNIEnv* env, jobject object, jint *retcode) {
  /* Try to call 'call_dangerous_function', and raise proper Java Error upon 
   * fatal error (SEGV, etc.). **/
  COFFEE_TRY_JNI(env, *retcode = call_dangerous_function(env, object));
}

/** Regular JNI entry point. **/
jint Java_com_example_android_MyNative_foo(JNIEnv* env, jobject object) {
  jint retcode = 0;
  foo_protected(env, object, &retcode);
  return retcode;
}
```

*and, in case of crash, get something like this (note: the last Exception with native backtrace is produced on Android >= 4.1.1)*:
```
FATAL EXCEPTION: AsyncTask #5
java.lang.RuntimeException: An error occured while executing doInBackground()
	at android.os.AsyncTask$3.done(AsyncTask.java:299)
	at java.util.concurrent.FutureTask.finishCompletion(FutureTask.java:352)
	at java.util.concurrent.FutureTask.setException(FutureTask.java:219)
	at java.util.concurrent.FutureTask.run(FutureTask.java:239)
	at android.os.AsyncTask$SerialExecutor$1.run(AsyncTask.java:230)
	at java.util.concurrent.ThreadPoolExecutor.runWorker(ThreadPoolExecutor.java:1080)
	at java.util.concurrent.ThreadPoolExecutor$Worker.run(ThreadPoolExecutor.java:573)
	at java.lang.Thread.run(Thread.java:841)
Caused by: java.lang.Error: signal 11 (Address not mapped to object) at address 0x42 [at libexample.so:0xa024]
	at com.example.jni.ExampleLib.main(Native Method)
	at com.example.ExampleActivity$Runner.runInternal(ExampleActivity.java:998)
	at com.example.ExampleActivity$Runner.doInBackground(ExampleActivity.java:919)
	at com.example.ExampleActivity$Runner.doInBackground(ExampleActivity.java:1)
	at android.os.AsyncTask$2.call(AsyncTask.java:287)
	at java.util.concurrent.FutureTask.run(FutureTask.java:234)
	... 4 more
Caused by: java.lang.Error: signal 11 (Address not mapped to object) at address 0x42 [at libexample.so:0xa024]
	at data.app_lib.com_example.libexample_so.0xa024(Native Method)
	at data.app_lib.com_example.libexample_so.0x705fc(hts_main2:0x8f74:0)
	at data.app_lib.com_example.libexamplejni_so.0x4cc8(ExampleLib_main:0xf8:0)
	at data.app_lib.com_example.libexamplejni_so.0x52d8(Java_com_example_jni_ExampleLib_main:0x64:0)
	at system.lib.libdvm_so.0x1dc4c(dvmPlatformInvoke:0x70:0)
	at system.lib.libdvm_so.0x4dcab(dvmCallJNIMethod(unsigned int const*, JValue*, Method const*, Thread*):0x18a:0)
	at system.lib.libdvm_so.0x385e1(dvmCheckCallJNIMethod(unsigned int const*, JValue*, Method const*, Thread*):0x8:0)
	at system.lib.libdvm_so.0x4f699(dvmResolveNativeMethod(unsigned int const*, JValue*, Method const*, Thread*):0xb8:0)
	at system.lib.libdvm_so.0x27060(Native Method)
	at system.lib.libdvm_so.0x2b580(dvmInterpret(Thread*, Method const*, JValue*):0xb8:0)
	at system.lib.libdvm_so.0x5fcbd(dvmCallMethodV(Thread*, Method const*, Object*, bool, JValue*, std::__va_list):0x124:0)
	at system.lib.libdvm_so.0x5fce7(dvmCallMethod(Thread*, Method const*, Object*, JValue*, ...):0x14:0)
	at system.lib.libdvm_so.0x54a6f(Native Method)
	at system.lib.libc_so.0xca58(__thread_entry:0x48:0)
	at system.lib.libc_so.0xcbd4(pthread_create:0xd0:0)
```

* Outside JNI code

The COFFEE_TRY()/COFFEE_CATCH()/COFFEE_END() syntax can be used:

```c
void my_function() {
  COFFEE_TRY() {
    /** Try to call 'call_some_native_function'. **/
    call_some_native_function();
  } COFFEE_CATCH() {
    /** Caught a signal. **/
    const char*const message = coffeecatch_get_message();
    fprintf(stderr, "**FATAL ERROR: %s\n", message);
  } COFFEE_END();
}
```

* In C++ code

The `COFFEE_CXX_TRY()`/`COFFEE_CXX_CATCH()`/`COFFEE_CXX_END()` macros are C++-friendly variants. A sentinel object runs the cleanup from its destructor, so cleanup happens even if the protected block exits by a `return` or a propagating C++ exception (both of which are forbidden with the plain `COFFEE_TRY()` macros):

```cpp
void my_function() {
  COFFEE_CXX_TRY() {
    call_some_native_function();
    return;                     // safe: the sentinel still cleans up
  } COFFEE_CXX_CATCH() {
    const char*const message = coffeecatch_get_message();
    fprintf(stderr, "**FATAL ERROR: %s\n", message);
  } COFFEE_CXX_END();
}
```

These unify *cleanup*, not *catching*: `COFFEE_CXX_CATCH()` catches only signals (never a C++ `throw`), and a C++ `catch` catches only C++ exceptions (never a signal). The two mechanisms stay separate, and can be nested. A signal in the protected block goes to `COFFEE_CXX_CATCH()`; a C++ `throw` skips it entirely and unwinds (running the sentinel's cleanup on the way) to the enclosing C++ `catch`:

```cpp
try {
  COFFEE_CXX_TRY() {
    if (use_native) {
      call_some_native_function();  // a SIGSEGV here -> COFFEE_CXX_CATCH below
    } else {
      throw std::runtime_error("bad input");  // -> C++ catch below, NOT COFFEE_CXX_CATCH
    }
  } COFFEE_CXX_CATCH() {
    // Reached only for a signal. The C++ throw never lands here.
    coffeecatch_cancel_pending_alarm();
    throw std::runtime_error(coffeecatch_get_message());  // re-raise as a C++ exception if desired
  } COFFEE_CXX_END();
} catch (const std::exception& e) {
  // Reached for the C++ throw above, or for one re-raised from COFFEE_CXX_CATCH().
  fprintf(stderr, "**C++ EXCEPTION: %s\n", e.what());
}
```

If you re-raise a C++ exception from inside `COFFEE_CXX_CATCH()` (as above), you *may* want to cancel the pending alarm first — otherwise the process is still killed after the grace period, even though the exception was handled. Consider wisely though, your C++ objects may be left in an inconsistent state after handling a signal; see below.

Recovery from a signal is performed with `siglongjmp()`, which does **not** unwind the C++ frames between `COFFEE_CXX_TRY()` and the faulting instruction — destructors of objects in those frames are skipped, so locks stay held and resources leak. (The C++ standard even makes this undefined behavior when such a destructor is non-trivial; it works in practice only because `siglongjmp()` just restores the saved register/stack context and leaks the skipped objects rather than crashing.) Treat the catch block as last rites: log, then exit. Do not use it to recover and continue.
Note that this behavior is not unique to the `COFFEE_CXX_*` macros; the same would happen with the standard C versions when used in C++.

* Hints

If you wish to catch signals and continue running your program rather than ending it (this may be dangerous, especially if a crash was spotted within a C library function, such as `malloc()`, or within C++ code and destructors were skipped), use the `coffeecatch_cancel_pending_alarm()` function to cancel the default pending alarm triggered to avoid deadlocks.

