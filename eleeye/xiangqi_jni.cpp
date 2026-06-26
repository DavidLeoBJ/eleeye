#include <jni.h>
#include <string>
#include "search.h"  // eleeye自带的头文件，不用改

extern "C"
JNIEXPORT jstring JNICALL
Java_com_你的包名_XiangqiEngine_search(JNIEnv* env, jobject, jstring fen_j) {
    const char* fen = env->GetStringUTFChars(fen_j, nullptr);
    std::string mv = search_best_move(std::string(fen));  // 象眼原生搜索接口
    env->ReleaseStringUTFChars(fen_j, fen);
    return env->NewStringUTF(mv.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_你的包名_XiangqiEngine_init(JNIEnv*, jobject) {}  // 象眼无需特殊初始化，留空
extern "C"
JNIEXPORT void JNICALL
Java_com_你的包名_XiangqiEngine_destroy(JNIEnv*, jobject) {} // 无需特殊清理，留空
