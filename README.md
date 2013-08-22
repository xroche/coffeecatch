coffeecatch
===========

CoffeeCatch, a tiny native signal handler/catcher for JNI code (especially for Android/Dalvik)

Allows to "gracefully" recover from a signal (segv, sibus...) as if it was a Java exception. It will not gracefully recover from allocator/mutexes corruption etc., however, but at least "most" gentle crashes (null pointer dereferencing, integer division, stack overflow etc.) should be handled without too much troubles.

The handler is thread-safe, but client must have exclusive control on the signal handlers (ie. the library is installing its own signal handlers on top of the existing ones).

Example:

```c
COFFEE_TRY() {
  call_some_native_function()
} COFFEE_CATCH() {
  const char*const message = coffeecatch_get_message();
  jclass cls = (*env)->FindClass(env, "java/lang/RuntimeException");
  (*env)->ThrowNew(env, cls, strdup(message));
} COFFEE_END();
```
