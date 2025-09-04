# Day 14： TASK2 
![img.png](img.png)

## 功能特点
- [x]  JNI接口: 实现播放，暂停，加速，停止播放。
- [x]  单例模式Player。
- [x]  封装OpenGLES渲染类，实现对frame的渲染。
- [x]  尝试使用硬件加速解码。
- [x]  使用PlayerContext实现：解复用线程，解码线程、渲染线程间元数据通讯。
- [x]  通过条件变量实现暂时时阻塞渲染线程，再次播放时发送notify通知接触渲染阻塞。
- [x]  通过记录渲染帧pts，计算播放进度；通过将播放延迟除以速度实现控制播放速度。
- [x]  支持停止时优雅回收所有分配资源，防止内存泄露。

  

### [前往源码src/cpp](app/src/main/cpp/src)


### JNI接口: 实现播放，暂停，加速，停止播放
```cpp

extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativePlay(JNIEnv *env, jobject instance, jstring dataSource_,
                                                 jobject surface) {
    const char *dataSource = env->GetStringUTFChars(dataSource_, 0);
    LOGI("dataSource %s\n", dataSource);
    strcpy(Player::dataSource, dataSource);
    auto player = Player::getInstance();
    // 获取渲染器实例
    auto eglRender = VideoGLESRender::GetInstance();
    eglRender->SetNativeWindow(ANativeWindow_fromSurface(env, surface));
    player->prepare();
    env->ReleaseStringUTFChars(dataSource_, dataSource);
    LOGI("prepare success\n");
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
    auto eglRender = VideoGLESRender::GetInstance();
    eglRender->Cleanup();
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
```
### 单例模式Player
```cpp
class Player {
private:
    // 私有化构造，拷贝构造，赋值构造，防止浅拷贝和深拷贝
    static std::mutex imutex;
    static Player* instance;
    Player(const char *dataSource);
    Player();
    Player(const Player &player);
    void operator=(const Player &player);
public:
    static char *dataSource;
    // 双重判断+锁机制的单例Player
    static Player* getInstance(){
        if (Player::instance == nullptr){
            std::lock_guard<std::mutex> lock(Player::imutex);
            if (Player::instance == nullptr){
                if (!Player::dataSource){
                    LOGE("dataSource is NULL\n");
                    return nullptr;
                }
                Player::instance = new Player(Player::dataSource);
            }
            return Player::instance;
        }
        return Player::instance;
    }
}
```

### 封装OpenGLES渲染类，实现对frame的渲染: 将图像转换为纹理，使用GPU并行处理
```cpp
class VideoGLESRender {
public:
    static VideoGLESRender *GetInstance();

    static void ReleaseInstance();

    // 初始化EGL/GLES环境
    bool Init(int width, int height);

    // 渲染一帧视频
    void RenderFrame(AVFrame *frame);

    // 清理资源
    void Cleanup();

    void SetNativeWindow(ANativeWindow *window);
}
```
#### 核心代码
```cpp
void VideoGLESRender::RenderFrame(AVFrame *frame) {
    if (!is_playing_ || !frame) return;

    std::unique_lock<std::mutex> lock(render_mutex_);
    if (is_pause_) {
        render_cond_.wait(lock, [this] { return !is_pause_ || !is_playing_; });
    }

    if (!is_playing_) return;

    ConvertPixelFormat(frame);
    UpdateTextures(frame);

    if (render_callback_) {
        render_callback_(dst_data_[0]);
    }

    // 清除颜色缓冲区
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 使用程序
    glUseProgram(program_);

    // 设置顶点坐标
    GLfloat vertices[] = {
            -1.0f, -1.0f, 0.0f,  // 左下
            1.0f, -1.0f, 0.0f,   // 右下
            -1.0f, 1.0f, 0.0f,   // 左上
            1.0f, 1.0f, 0.0f     // 右上
    };

    GLfloat tex_coords[] = {
            0.0f, 1.0f,  // 左下
            1.0f, 1.0f,  // 右下
            0.0f, 0.0f,  // 左上
            1.0f, 0.0f   // 右上
    };

    GLushort indices[] = {0, 1, 2, 1, 2, 3};

    // 传递顶点数据
    GLint position_attr = glGetAttribLocation(program_, "a_Position");
    glEnableVertexAttribArray(position_attr);
    glVertexAttribPointer(position_attr, 3, GL_FLOAT, GL_FALSE, 0, vertices);

    // 传递纹理坐标
    GLint tex_coord_attr = glGetAttribLocation(program_, "a_TexCoord");
    glEnableVertexAttribArray(tex_coord_attr);
    glVertexAttribPointer(tex_coord_attr, 2, GL_FLOAT, GL_FALSE, 0, tex_coords);

    // 绑定纹理
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    GLint texture_uniform = glGetUniformLocation(program_, "u_Texture");
    glUniform1i(texture_uniform, 0);

    // 绘制
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

    // 交换缓冲区
    eglSwapBuffers(egl_display_, egl_surface_);
}
```

### 尝试使用硬件加速解码
```cpp
    // 获得解码器上下文
        AVCodecContext *context = avcodec_alloc_context3(dec);
        if (context == NULL) {
            LOGE("avcodec_alloc_context3 failed:%s", av_err2str(ret));
            return;
        }
        ret = avcodec_parameters_to_context(context, codecpar);
        if (ret < 0) {
            LOGE("avcodec_parameters_to_context failed:%s", av_err2str(ret));
            return;
        }

        // 尝试自动检测可用的硬件加速类型
        AVBufferRef *hw_device_ctx = NULL;
        enum AVHWDeviceType hw_type = AV_HWDEVICE_TYPE_NONE;
        while ((hw_type = av_hwdevice_iterate_types(hw_type)) != AV_HWDEVICE_TYPE_NONE) {
            if (av_hwdevice_ctx_create(&hw_device_ctx, hw_type, NULL, NULL, 0) == 0) {
                LOGI("Using hardware acceleration: %s", av_hwdevice_get_type_name(hw_type));
                break;
            }
        }

        // 设置硬件解码（如果可用）
        context->get_format = avcodec_default_get_format;
        if (hw_device_ctx) {
            context->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        } else {
            LOGI("No hardware acceleration available");
        }
```


### 使用PlayerContext实现解复用线程，解码线程、渲染线程间元数据通讯。
```cpp
#ifndef PLAYERCONTEXT_H
#define PLAYERCONTEXT_H

#include <atomic>
#include <algorithm>
#include <cmath>
#include <queue>
#include <stdio.h>
#include <thread>
#include "Clock.h"
#include  "Log.h"

extern "C"
{
#include "libavformat/avformat.h"
#include "libavutil/time.h"
#include "libavutil/rational.h"
}

class PlayerContext {
public:
    PlayerContext() = default;

    double audio_hw_delay = 0;
    Clock audio_clock_t;
    Clock video_clock_t;
    double video_duration = 0;
    AVRational video_base_time;
    AVRational audio_base_time;
    AVStream *video_stream = 0;
    AVStream *audio_stream = 0;

    void setClockAt(Clock &clock, double pts, double time) {
        clock.pts = pts;
        clock.last_updated = time;
    }

    void setClock(Clock &clock, double pts) {
        setClockAt(clock, pts, (double) av_gettime() / AV_TIME_BASE);
    }

    double getAudioClock() const { return getClock(audio_clock_t); }

    double getVideoClock() const { return getClock(video_clock_t); }

    double getAudioRelaTime() const {
        return audio_clock_t.pts * audio_base_time.num;
    }

    double getVideoRelaTime() const {
        return video_clock_t.pts * video_base_time.num;
    }

    double getClock(const Clock &c) const {
        double time = (double) av_gettime() / AV_TIME_BASE;
        return c.pts + time - c.last_updated;
    }
    ~PlayerContext() {
        delete video_stream;
        delete audio_stream;
        video_stream = 0;
        audio_stream = 0;
    }
};
#endif
```

### 通过条件变量实现暂时时阻塞渲染线程，再次播放时发送notify通知接触渲染阻塞。
### 通过记录渲染帧pts，计算播放进度；通过将延迟除以速度实现控制播放速度。
```cpp
void VideoChannel::pause() {
    isPause = 1;
}

void VideoChannel::resume() {
    isPause = 0;
    cond.notify_one();
}
-----------------------------------------
void VideoChannel::render() {
    std::unique_lock<std::mutex> lock(mutex);
    auto v_time_base = playerContext->video_stream->time_base;
    double last_pts = 0;
    double frame_delay = 1.0 / fps;
    auto deleter = [](AVFrame *frame) {
        AVFrame *framePtr = frame;
        releaseAvFrame(&framePtr);
    };
    auto render = VideoGLESRender::GetInstance();
    if (!render->Init(avCodecContext->width, avCodecContext->height)) {
        LOGE("Failed to initialize OpenGL render");
        return;
    }
    while (isPlaying) {
        if (isPause) {
            cond.wait(lock); # 阻塞等待
        }

        AVFrame *frame = nullptr;
        if (!frames.pop(frame)) {
            continue;
        }
        // 使用unique_ptr确保frame释放
        std::unique_ptr<AVFrame, decltype(deleter)> frameGuard(frame, deleter);
        // 计算时间戳
        clock = frame->pts * av_q2d(v_time_base);
        // 计算帧延迟
        frame_delay = (last_pts > 0 && clock > 0) ? clock - last_pts: v_time_base.num;
        last_pts = clock;
        // 利用延迟控制播放速度
        double delays = frame_delay / speed;
        av_usleep(delays * AV_TIME_BASE);
        // 渲染帧
        render->RenderFrame(frame);
        // 更新时钟
        playerContext->setClock(playerContext->video_clock_t,
                                frame->pts * av_q2d(v_time_base) +
                                playerContext->audio_hw_delay);
    }

    render->Cleanup();
    VideoGLESRender::ReleaseInstance();
    isPlaying = false;
}
```

### 支持停止时优雅回收所有分配资源，防止内存泄露。
```cpp

void *async_stop(void *args) {
    Player *player = static_cast<Player *>(args);
    if (player->pid_player) {
        pthread_join(player->pid_player, 0);
    }
    player->pid_player = 0;
    player->isPlaying = 0;
    if (player->pid_demux) {
        pthread_join(player->pid_demux, 0);
    }
    player->pid_demux = 0;
    DELETE(player);
    return 0;
}

void Player::stop() {
    if (!isPlaying) {
        return;
    }
    isPlaying = 0;
    VideoGLESRender::GetInstance()->ReleaseInstance();
    pthread_create(&pid_stop, 0, async_stop, this);
}
```
## ![最终效果: 点击下载gif](day13-1.gif)
![day13-1.gif](day13-1.gif)