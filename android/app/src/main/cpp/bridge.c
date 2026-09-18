#include "engine.h"
#include <jni.h>

JNIEXPORT jlong JNICALL Java_org_haxcess_turmite_NativeEngine_create(
    JNIEnv *env, jclass type, jint width, jint height)
{
    (void)env; (void)type;
    return (jlong)(intptr_t)android_engine_create(width, height, 0);
}

JNIEXPORT jboolean JNICALL Java_org_haxcess_turmite_NativeEngine_frame(
    JNIEnv *env, jclass type, jlong handle, jintArray pixels)
{
    (void)type;
    if (!handle || !pixels) return JNI_FALSE;
    jsize capacity = (*env)->GetArrayLength(env, pixels);
    jint *output = (*env)->GetIntArrayElements(env, pixels, NULL);
    if (!output) return JNI_FALSE;
    bool ok = android_engine_frame((AndroidEngine *)(intptr_t)handle, (uint32_t *)output, (size_t)capacity);
    (*env)->ReleaseIntArrayElements(env, pixels, output, ok ? 0 : JNI_ABORT);
    return ok ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_org_haxcess_turmite_NativeEngine_destroy(
    JNIEnv *env, jclass type, jlong handle)
{
    (void)env; (void)type;
    android_engine_destroy((AndroidEngine *)(intptr_t)handle);
}
