#include "native_lib.h"

#include "coffeecatch/coffeecatch.h"
#include "coffeecatch/coffeejni.h"

#include <android/log.h>

void debug(const char *format, ...) {
	va_list argptr;
	va_start(argptr, format);
	__android_log_vprint(ANDROID_LOG_ERROR, "NATIVE_LIB", format, argptr);
	va_end(argptr);
}

/** The potentially dangerous function. **/
jint call_dangerous_function(JNIEnv* env, jobject object) {
	// ... do dangerous things!
	return 42;
}

JNIEXPORT jint JNICALL Java_com_example_CoffeeCatchDemo_native_1lib_native_1func(
		JNIEnv *env, jobject obj, jfloat a) {

	debug(" crash report rajesh test \n");
	jint *retcode = 0;
	COFFEE_TRY_JNI(env, *retcode = call_dangerous_function(env, obj));
	jclass cls = env->FindClass("Main"); // ERROR CODE.
	debug("unreachable\n");
}
