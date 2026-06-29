#ifndef LIB_FFMPEG_JNI_DEFINE_H
#define LIB_FFMPEG_JNI_DEFINE_H

#include <android/log.h>

#define LOGI(TAG, FORMAT, ...) __android_log_print(ANDROID_LOG_INFO, TAG, FORMAT, ##__VA_ARGS__)
#define LOGE(TAG, FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR, TAG, FORMAT, ##__VA_ARGS__)

#define FFMPEG_FUNC(RETURN_TYPE, FUNC_NAME, ...) \
    JNIEXPORT RETURN_TYPE JNICALL Java_com_dongao_lib_ffmpeg_FFmpegCmd_ ## FUNC_NAME \
    (JNIEnv *env, jclass thiz, ##__VA_ARGS__)\

#endif