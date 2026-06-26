#include <jni.h>
#include <string>
#include "search.h"  // 象眼自带搜索头文件，里面有SetSearchDepth和search_best_move
#include "pregen.h"  // 象眼预生成数据头文件，避免链接报错

// 对应你Java层的：com.example.chinesechessspectator.engine.ChessEngine.nativeSearch(String fen, int depth)
extern "C"
JNIEXPORT jstring JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeSearch(
    JNIEnv* env, jobject thiz, jstring fen_str, jint depth) {
    
    // 1. 转换Java字符串为C++字符串
    jboolean isCopy;
    const char* fen = env->GetStringUTFChars(fen_str, &isCopy);
    std::string fen_cpp(fen);

    // 2. 用你传入的depth设置象眼搜索深度（象眼原生支持）
    SetSearchDepth(depth);

    // 3. 调用象眼原生搜索接口，返回最佳着法（ICCS格式，比如"h2e2"）
    std::string best_move = search_best_move(fen_cpp);

    // 4. 释放Java字符串内存，避免泄漏
    if (isCopy == JNI_TRUE) {
        env->ReleaseStringUTFChars(fen_str, fen);
    }

    // 5. 转换C++字符串为Java字符串返回
    return env->NewStringUTF(best_move.c_str());
}

// 如果你Java类里有native init方法，加这个；没有的话可以删掉
extern "C"
JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeInit(
    JNIEnv* env, jobject thiz) {
    // 象眼首次调用search会自动初始化，这里可以留空，也可以加开局库加载逻辑
}

// 如果你Java类里有native destroy方法，加这个；没有的话可以删掉
extern "C"
JNIEXPORT void JNICALL
Java_com_example_chinesechessspectator_engine_ChessEngine_nativeDestroy(
    JNIEnv* env, jobject thiz) {
    // 象眼无特殊资源需要释放，留空即可
}
