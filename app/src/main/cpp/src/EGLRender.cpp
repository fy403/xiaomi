#include "EGLRender.h"
#include <android/native_window.h>
#include <android/log.h>

extern "C"
{
#include "libavutil/imgutils.h"
}

#define LOG_TAG "VideoGLESRender"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// 饿汉模式单例全局渲染类
VideoGLESRender *VideoGLESRender::instance_ = new VideoGLESRender();

VideoGLESRender::VideoGLESRender()
        : native_window_(nullptr),
          egl_display_(EGL_NO_DISPLAY),
          egl_surface_(EGL_NO_SURFACE),
          egl_context_(EGL_NO_CONTEXT),
          program_(0),
          texture_id_(0),
          vertex_shader_(0),
          fragment_shader_(0),
          width_(0),
          height_(0),
          is_playing_(false),
          is_pause_(false),
          sws_context_(nullptr),
          render_callback_(nullptr) {
    memset(dst_data_, 0, sizeof(dst_data_));
    memset(dst_linesize_, 0, sizeof(dst_linesize_));
}

VideoGLESRender::~VideoGLESRender() {
    Cleanup();
}

VideoGLESRender *VideoGLESRender::GetInstance() {
    return instance_;
}

void VideoGLESRender::SetNativeWindow(ANativeWindow *window) {
    if (instance_) {
        instance_->native_window_ = window;
    }
}

void VideoGLESRender::ReleaseInstance() {
    if (instance_) {
        delete instance_;
        instance_ = nullptr;
    }
}

bool VideoGLESRender::Init(int width, int height) {
    width_ = width;
    height_ = height;

    if (!InitEGL()) {
        LOGE("Failed to initialize EGL");
        return false;
    }

    if (!CreateProgram()) {
        LOGE("Failed to create GLES program");
        return false;
    }

    CreateTextures();

    // 初始化SWSContext用于像素格式转换
    sws_context_ = sws_getContext(
            width, height, AV_PIX_FMT_YUV420P,
            width, height, AV_PIX_FMT_RGBA,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    av_image_alloc(dst_data_, dst_linesize_, width, height, AV_PIX_FMT_RGBA, 1);

    is_playing_ = true;
    return true;
}

bool VideoGLESRender::InitEGL() {
    // 获取EGL Display
    egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display_ == EGL_NO_DISPLAY) {
        LOGE("Failed to get EGL display");
        return false;
    }

    // 初始化EGL
    if (!eglInitialize(egl_display_, nullptr, nullptr)) {
        LOGE("Failed to initialize EGL");
        return false;
    }

    // 配置属性
    const EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE
    };

    EGLConfig config;
    EGLint num_configs;
    if (!eglChooseConfig(egl_display_, attribs, &config, 1, &num_configs)) {
        LOGE("Failed to choose EGL config");
        return false;
    }

    // 创建EGL Surface
    EGLint format;
    eglGetConfigAttrib(egl_display_, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(native_window_, width_, height_, format);
    egl_surface_ = eglCreateWindowSurface(egl_display_, config, native_window_, nullptr);
    if (egl_surface_ == EGL_NO_SURFACE) {
        LOGE("Failed to create EGL surface");
        return false;
    }

    // 创建EGL Context
    const EGLint context_attribs[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    egl_context_ = eglCreateContext(egl_display_, config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) {
        LOGE("Failed to create EGL context");
        return false;
    }

    // 绑定上下文
    if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
        LOGE("Failed to make EGL context current");
        return false;
    }

    return true;
}

bool VideoGLESRender::CreateProgram() {
    // 顶点着色器
    const char *vertex_shader_source =
            "attribute vec4 a_Position;\n"
            "attribute vec2 a_TexCoord;\n"
            "varying vec2 v_TexCoord;\n"
            "void main() {\n"
            "   gl_Position = a_Position;\n"
            "   v_TexCoord = a_TexCoord;\n"
            "}\n";

    vertex_shader_ = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader_, 1, &vertex_shader_source, nullptr);
    glCompileShader(vertex_shader_);

    GLint compiled;
    glGetShaderiv(vertex_shader_, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(vertex_shader_, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = new char[info_len];
            glGetShaderInfoLog(vertex_shader_, info_len, nullptr, info_log);
            LOGE("Error compiling vertex shader: %s", info_log);
            delete[] info_log;
        }
        return false;
    }

    // 片段着色器
    const char *fragment_shader_source =
            "precision mediump float;\n"
            "varying vec2 v_TexCoord;\n"
            "uniform sampler2D u_Texture;\n"
            "void main() {\n"
            "   gl_FragColor = texture2D(u_Texture, v_TexCoord);\n"
            "}\n";

    fragment_shader_ = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader_, 1, &fragment_shader_source, nullptr);
    glCompileShader(fragment_shader_);

    glGetShaderiv(fragment_shader_, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint info_len = 0;
        glGetShaderiv(fragment_shader_, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = new char[info_len];
            glGetShaderInfoLog(fragment_shader_, info_len, nullptr, info_log);
            LOGE("Error compiling fragment shader: %s", info_log);
            delete[] info_log;
        }
        return false;
    }

    // 创建程序
    program_ = glCreateProgram();
    glAttachShader(program_, vertex_shader_);
    glAttachShader(program_, fragment_shader_);
    glLinkProgram(program_);

    GLint linked;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint info_len = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &info_len);
        if (info_len > 1) {
            char *info_log = new char[info_len];
            glGetProgramInfoLog(program_, info_len, nullptr, info_log);
            LOGE("Error linking program: %s", info_log);
            delete[] info_log;
        }
        return false;
    }

    return true;
}

void VideoGLESRender::CreateTextures() {
    glGenTextures(1, &texture_id_);
    glBindTexture(GL_TEXTURE_2D, texture_id_);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
}

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

void VideoGLESRender::UpdateTextures(AVFrame *frame) {
    glBindTexture(GL_TEXTURE_2D, texture_id_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE,
                    dst_data_[0]);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void VideoGLESRender::ConvertPixelFormat(AVFrame *frame) {
    if (sws_context_ && frame) {
        sws_scale(sws_context_,
                  frame->data, frame->linesize, 0,
                  frame->height,
                  dst_data_, dst_linesize_);
    }
}

void VideoGLESRender::Cleanup() {
    if (sws_context_) {
        sws_freeContext(sws_context_);
        sws_context_ = nullptr;
    }

    if (dst_data_[0]) {
        av_freep(&dst_data_[0]);
    }

    if (texture_id_) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }

    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }

    if (vertex_shader_) {
        glDeleteShader(vertex_shader_);
        vertex_shader_ = 0;
    }

    if (fragment_shader_) {
        glDeleteShader(fragment_shader_);
        fragment_shader_ = 0;
    }

    if (egl_display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

        if (egl_context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(egl_display_, egl_context_);
            egl_context_ = EGL_NO_CONTEXT;
        }

        if (egl_surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(egl_display_, egl_surface_);
            egl_surface_ = EGL_NO_SURFACE;
        }

        eglTerminate(egl_display_);
        egl_display_ = EGL_NO_DISPLAY;
    }

    if (native_window_) {
        ANativeWindow_release(native_window_);
        native_window_ = nullptr;
    }

    is_playing_ = false;
    is_pause_ = false;
}