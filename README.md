coffeecatch
===========

**CoffeeCatch**, a *tiny* native signal handler/catcher for JNI code (especially for **Android**/Dalvik)

Allows to "gracefully" recover from a signal (segv, sibus...) as if it was a Java exception. It will not gracefully recover from allocator/mutexes corruption etc., however, but at least "most" gentle crashes (null pointer dereferencing, integer division, stack overflow etc.) should be handled without too much troubles.

The handler is thread-safe, but client must have exclusive control on the signal handlers (ie. the library is installing its own signal handlers on top of the existing ones).

**Example**:

```c
void my_native_function(JNIEnv* env, jobject object, jint *retcode) {
  /* Try to call 'call_dangerous_function', and raise proper Java Error upon 
   * fatal error (SEGV, etc.). **/
  COFFEE_TRY_JNI(env, *retcode = call_dangerous_function(env, object));
}
```

or, outside JNI code:
```c
void my_function() {
  COFFEE_TRY() {
    /** Try to call 'call_some_native_function'. **/
    call_some_native_function()
  } COFFEE_CATCH() {
    /** Caught a signal. **/
    const char*const message = coffeecatch_get_message();
    fprintf(stderr, "**FATAL ERROR: %s\n", message);
  } COFFEE_END();
}
```
