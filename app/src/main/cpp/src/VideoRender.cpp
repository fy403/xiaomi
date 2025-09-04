//
// Created by Administrator on 2025/3/30.
//
extern "C"
{
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#include "libavcodec/avcodec.h"
}
#include "VideoChannel.h"
#include "Constant.h"
#include "EGLRender.h"

void VideoChannel::render() {
    LOGD("开始渲染");
    std::unique_lock<std::mutex> lock(mutex);
    auto v_time_base = playerContext->video_stream->time_base;
    double last_pts = 0;
    double frame_delay = v_time_base.num;
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
            cond.wait(lock, [this] { return !this->isPause; });
        }

        AVFrame *frame = nullptr;
        if (!frames.pop(frame)) {
            continue;
        }

        // 使用unique_ptr确保frame释放
        std::unique_ptr<AVFrame, decltype(deleter)> frameGuard(frame, deleter);
        // 计算时间戳
        clock = (frame->best_effort_timestamp == AV_NOPTS_VALUE) ? 0
                                                                 : frame->best_effort_timestamp *
                                                                   av_q2d(v_time_base);
//        clock = frame->pts * av_q2d(v_time_base);
        // 计算帧延迟
        frame_delay = (last_pts > 0 && clock > 0) ? clock - last_pts
                                                  : v_time_base.num;
        last_pts = clock;
        double delays = frame_delay / speed;
//        LOGD("speed: %f\t delays: %f\n", speed, delays);
        // 音视频同步
        handleSync(clock, delays);
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
    LOGD("视频渲染线程退出");
}


void VideoChannel::handleSync(double videoClock, double frameDelay) {
    if (videoClock == 0) {
        av_usleep(frameDelay * AV_TIME_BASE);
        return;
    }

    if (!playerContext->audio_stream) {
        av_usleep(frameDelay * AV_TIME_BASE);
        return;
    }

    double audioClock = playerContext->getAudioRelaTime();
    double diff = videoClock - audioClock;
    const double SYNC_THRESHOLD = 1.0;
    const double SYNC_MIN_THRESHOLD = 0.05;

    if (diff > 0) {
        // 视频快于音频，加一帧延迟
        av_usleep((frameDelay + std::min(diff, SYNC_THRESHOLD)) * AV_TIME_BASE);
    } else if (diff < -SYNC_THRESHOLD) {
        // 视频远慢于音频，丢弃帧
        frames.drop();
    } else if (diff < -SYNC_MIN_THRESHOLD) {
        // 视频稍慢于音频，不做处理让视频追赶
    }
}
