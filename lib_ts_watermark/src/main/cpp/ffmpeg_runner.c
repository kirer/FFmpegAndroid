#include <jni.h>
#include <stdlib.h>
#include <string.h>

#include "ffmpeg/ffmpeg.h"

#define INPUT_SIZE (8 * 1024)

JNIEXPORT jint JNICALL
Java_com_dongao_lib_tswatermark_NativeFfmpegRunner_runCommand(JNIEnv *env, jclass clazz,
                                                              jobjectArray commands) {
    (void) clazz;

    int argc = (*env)->GetArrayLength(env, commands);
    char **argv = (char **) malloc(argc * sizeof(char *));
    if (argv == NULL) {
        return -1;
    }

    for (int i = 0; i < argc; i++) {
        jstring jstr = (jstring) (*env)->GetObjectArrayElement(env, commands, i);
        const char *temp = (*env)->GetStringUTFChars(env, jstr, 0);
        argv[i] = (char *) malloc(INPUT_SIZE);
        if (argv[i] == NULL) {
            (*env)->ReleaseStringUTFChars(env, jstr, temp);
            for (int j = 0; j < i; j++) {
                free(argv[j]);
            }
            free(argv);
            return -1;
        }
        strcpy(argv[i], temp);
        (*env)->ReleaseStringUTFChars(env, jstr, temp);
    }

    int result = run(argc, argv);
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
    return result;
}

void progress_callback(int position, int duration, int state) {
    (void) position;
    (void) duration;
    (void) state;
}
