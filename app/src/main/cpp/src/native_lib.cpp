#include <jni.h>
#include "Convert.h"
#include "Log.h"
#include <jni.h>
#include <string>
#include "Log.h"
#include <android/native_window.h>
#include "EGLRender.h"
#include "Player.h"
#include "ThreadPool.h"
// 文件格式转YUV
//extern "C"
//JNIEXPORT jint JNICALL
//Java_com_example_androidplayer_Player_nativePlay(JNIEnv *env, jobject thiz, jstring file,
//                                                 jobject surface) {
//    const char *dataSource = env->GetStringUTFChars(file, 0);
//    LOGI("dataSource %s\n", dataSource);
//
//    player = new Player(dataSource);
//    player->prepare();
//    env->ReleaseStringUTFChars(file, dataSource);
//
//    LOGI("prepare success\n");
//    player->start();
//    return 0;
//}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativePlay(JNIEnv *env, jobject instance, jstring dataSource_,
                                                 jobject surface) {
    const char *dataSource = env->GetStringUTFChars(dataSource_, 0);
    LOGI("dataSource %s\n", dataSource);
    Player::dataSource = new char[strlen(dataSource) + 1];
    Player::dataSource[strlen(dataSource)] = 0;
    strcpy(Player::dataSource, dataSource);
    auto player = Player::getInstance();
    // 获取渲染器实例
    auto eglRender = VideoGLESRender::GetInstance();
    eglRender->SetNativeWindow(ANativeWindow_fromSurface(env, surface));
    player->prepare();
    env->ReleaseStringUTFChars(dataSource_, dataSource);
    LOGI("prepare success\n");
    // 初始化线程池
    unsigned int numCores = std::thread::hardware_concurrency();
    int cores = 5;
    int queue_size = 256;
    int _max = fmax(8, numCores*2-1);
    GlobalThreadPool::initThreadPool(cores, _max, queue_size);
    LOGI("global thread pool init success! cores: %d, max: %d, queue_size: %d", cores, _max, queue_size);
    player->start();
    return 0;
}


extern "C"
JNIEXPORT int JNICALL
Java_com_example_androidplayer_Player_nativeStop(JNIEnv *env, jobject instance) {
    // 清除播放器
    auto player = Player::getInstance();
    if (player) {
        player->stop();
        player = 0;
    }
    // 清除渲染器
    VideoGLESRender::GetInstance()->ReleaseInstance();
    // 回收线程池
    GlobalThreadPool::getInstance().shutdown();
    return 0;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_androidplayer_Player_nativePause(JNIEnv *env, jobject instance, jboolean p) {
    bool state = p;
    auto player = Player::getInstance();
    if (player) {
        if (state) {
            player->pause();
            LOGI("暂停\n");
        } else {
            player->resume();
            LOGI("继续播放\n");
        }
    }
}

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativeSetSpeed(JNIEnv *env, jobject thiz, jfloat speed) {
    auto player = Player::getInstance();
    if (player) {
        player->speed(speed);
        LOGD("调速：%f\n", speed);
    } else {
        LOGD("没有找到player");
    }
    return 0;
}
extern "C"
JNIEXPORT jdouble JNICALL
Java_com_example_androidplayer_Player_nativeGetPosition(JNIEnv *env, jobject thiz) {
    return Player::getInstance()->getPosition();
}
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativeSeek(JNIEnv *env, jobject thiz, jdouble abs_position) {
    auto player = Player::getInstance();
    player->seek(abs_position);
    return 0;
}