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

    /**
     * Seeking.
     */
    std::atomic<bool> seek_req{false};
    int seek_flags{0};
    int64_t seek_pos{0};
    int64_t seek_rel{0};
    int64_t last_pos = 0;
    std::mutex seekMutex;
    std::condition_variable seekCond;

    std::chrono::steady_clock::time_point last_seek_time;
    static constexpr double SEEK_THROTTLE_INTERVAL = 0.05; // 设置节流间

    void setClockAt(Clock &clock, double pts, double time) {
        clock.pts = pts;
        clock.last_updated = time;
    }

    void setClock(Clock &clock, double pts_plus) {
        setClockAt(clock, pts_plus, (double) av_gettime() / AV_TIME_BASE);
    }

    double getAudioClock() const { return getClock(audio_clock_t); }

    double getVideoClock() const { return getClock(video_clock_t); }

    double getAudioRelaTime() const {
        return audio_clock_t.pts;
    }

    double getVideoRelaTime() const {
        return video_clock_t.pts;
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

    void doSeekAbsolute(double abs_position) {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - last_seek_time;
        LOGD("seek 间隔时间：%f", elapsed.count());
        if (elapsed.count() < SEEK_THROTTLE_INTERVAL) {
            return; // 如果距离上次调用的时间小于阈值，则不执行
        }

        last_seek_time = now;

        if (!seek_req && video_stream) {
            std::unique_lock<std::mutex> lg(seekMutex);
            double pos = abs_position * video_duration; // 计算实际时间变化量（秒）
            if (pos < 0) {
                pos = 0;
            } else if (video_duration > 0 && pos > video_duration) {
                pos = video_duration; // 防止超过视频长度
            }
            seek_rel = (int64_t) (pos - last_pos) * AV_TIME_BASE; // 相对 seek 量（微秒）
            seek_pos = (int64_t) (pos * AV_TIME_BASE);  // 绝对 seek 位置（微秒）
            seek_flags = (seek_rel < 0) ? AVSEEK_FLAG_BACKWARD : 0;
            LOGD("正在跳转到: seek_pos: %f秒， seek_rel: %f秒\n", pos, pos - last_pos);
            last_pos = pos;
            seek_req = true;
            seekCond.wait(lg, [this] { return !seek_req; });
        }
    }
};

#endif