coffeecatch
===========

**CoffeeCatch**, a *tiny* native signal handler/catcher for JNI code (especially for **Android**/Dalvik)

Allows to "gracefully" recover from a signal (segv, sibus...) as if it was a Java exception. It will not gracefully recover from allocator/mutexes corruption etc., however, but at least "most" gentle crashes (null pointer dereferencing, integer division, stack overflow etc.) should be handled without too much troubles.

The handler is thread-safe, but client must have exclusive control on the signal handlers (ie. the library is installing its own signal handlers on top of the existing ones).

You must build all your libraries with `-funwind-tables`, to get proper unwinding information on all binaries. On Android, this can be achieved by using this line in the `Android.mk` file in each library block:
```
  LOCAL_CFLAGS := -funwind-tables
```

**Example**:

```c
void my_native_function(JNIEnv* env, jobject object, jint *retcode) {
  /* Try to call 'call_dangerous_function', and raise proper Java Error upon 
   * fatal error (SEGV, etc.). **/
  COFFEE_TRY_JNI(env, *retcode = call_dangerous_function(env, object));
}
```

*and, in case of crash, get something like this*:
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
Caused by: java.lang.Error: signal 11 (Address not mapped to object) at address 0x42 [at libhttrack.so:0xa024]
	at com.httrack.android.jni.HTTrackLib.main(Native Method)
	at com.httrack.android.HTTrackActivity$Runner.runInternal(HTTrackActivity.java:998)
	at com.httrack.android.HTTrackActivity$Runner.doInBackground(HTTrackActivity.java:919)
	at com.httrack.android.HTTrackActivity$Runner.doInBackground(HTTrackActivity.java:1)
	at android.os.AsyncTask$2.call(AsyncTask.java:287)
	at java.util.concurrent.FutureTask.run(FutureTask.java:234)
	... 4 more
Caused by: java.lang.Error: signal 11 (Address not mapped to object) at address 0x42 [at libhttrack.so:0xa024]
	at data.app_lib.com_httrack_android_2.libhttrack_so.0xa024(Native Method)
	at data.app_lib.com_httrack_android_2.libhttrack_so.0x705fc(hts_main2:0x8f74:0)
	at data.app_lib.com_httrack_android_2.libhtslibjni_so.0x4cc8(HTTrackLib_main:0xf8:0)
	at data.app_lib.com_httrack_android_2.libhtslibjni_so.0x52d8(Java_com_httrack_android_jni_HTTrackLib_main:0x64:0)
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

*or, outside JNI code*:
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
