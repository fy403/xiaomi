#ifndef VIDEO_GLES_RENDER_H
#define VIDEO_GLES_RENDER_H

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <mutex>
#include <condition_variable>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

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

private:
    VideoGLESRender();

    ~VideoGLESRender();

    // 初始化EGL
    bool InitEGL();

    // 创建GLES程序
    bool CreateProgram();

    // 创建纹理
    void CreateTextures();

    // 更新纹理数据
    void UpdateTextures(AVFrame *frame);

    // 转换像素格式
    void ConvertPixelFormat(AVFrame *frame);

    static VideoGLESRender *instance_;

    ANativeWindow *native_window_;
    EGLDisplay egl_display_;
    EGLSurface egl_surface_;
    EGLContext egl_context_;
    GLuint program_;
    GLuint texture_id_;
    GLuint vertex_shader_;
    GLuint fragment_shader_;

    int width_;
    int height_;
    bool is_playing_;
    bool is_pause_;
    std::mutex render_mutex_;
    std::condition_variable render_cond_;
    SwsContext *sws_context_;
    uint8_t *dst_data_[4];
    int dst_linesize_[4];

    void (*render_callback_)(uint8_t *);

};

#endif // VIDEO_GLES_RENDER_H