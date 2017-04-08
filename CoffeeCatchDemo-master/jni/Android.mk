LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := native
LOCAL_SRC_FILES := native_lib.cpp native_lib.h coffeecatch/coffeecatch.c coffeecatch/coffeejni.c coffeecatch/coffeejni.h
LOCAL_LDLIBS := -L$(SYSROOT)/usr/lib -llog
APP_MODULES + = breakpad_client
# ARM (coffeecatch)
LOCAL_CFLAGS := -funwind-tables -Wl,--no-merge-exidx-entries

include $(BUILD_SHARED_LIBRARY)