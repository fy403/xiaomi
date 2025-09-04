extern "C"
{
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#include "libavcodec/avcodec.h"
}
#include "VideoChannel.h"
#include "Log.h"
#include "Constant.h"
#include "ThreadPool.h"

void *decode_task(void *args) {
    VideoChannel *channel = static_cast<VideoChannel *>(args);
    channel->decode();
    return 0;
}

void *render_task(void *args) {
    VideoChannel *channel = static_cast<VideoChannel *>(args);
    channel->render();
    return 0;
}

void dropAvFrame(Queue<AVFrame *> &q) {
    if (!q.empty()) {
        AVFrame *frame = 0;
        q.pop(frame);
        BaseChannel::releaseAvFrame(&frame);
    }
}

VideoChannel::VideoChannel(int id, AVCodecContext *avCodecContext,
                           AVRational time_base, int fps)
        : BaseChannel(id, avCodecContext, time_base) {

    this->fps = fps;
//    converter = new Converter("/sdcard/Download/output2.yuv"); // 创建转换器实例
    frames.setDropHandle(dropAvFrame);
}

VideoChannel::~VideoChannel() {
//    delete converter; // 释放转换器实例
}

void VideoChannel::play() {
    startWork();
    isPlaying = 1;
    // 解码
    bool ret = GlobalThreadPool::getInstance().submit([this]() {
        decode_task(this);
    });
    if (ret<0) {
        LOGE("submit to thread pool failed 2");
    }
    LOGD("创建视频解码线程");
    // 渲染
     ret = GlobalThreadPool::getInstance().submit([this]() {
         render_task(this);
    });
    if (ret<0) {
        LOGE("submit to thread pool failed 3");
    }
    LOGD("创建渲染线程");
}

// 解码
void VideoChannel::decode() {
    AVPacket *packet = 0;
    int target_seek_pos_avtimebase = 0;
    bool seeking_flag = false;
    while (isPlaying) {
        int ret = packets.pop(packet);
        if (!isPlaying) {
            break;
        }
        // 取出失败
        if (!ret) {
            continue;
        }
        // seek clean
        if (packet->stream_index == FF_SEEK_PACKET_INDEX) {
            LOGD("视频seeking");
            avcodec_flush_buffers(avCodecContext);
            frames.setWork(0);
            frames.clear();
            frames.setWork(1);
            target_seek_pos_avtimebase = packet->pos;
            releaseAvPacket(&packet);
            seeking_flag = true;
            LOGD("视频seeked");
            continue;
        }
        // 把包丢给解码器
        ret = avcodec_send_packet(avCodecContext, packet);
        releaseAvPacket(&packet);
        if (ret == AVERROR(EAGAIN)) {
            // 需要更多数据
            continue;
        } else if (ret < 0) {
            // 失败
            break;
        }
        AVFrame *frame = av_frame_alloc();
        ret = avcodec_receive_frame(avCodecContext, frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret < 0) {
            // 失败
            break;
        }
        while (frames.size() > 100 && isPlaying) {
            av_usleep(1000 * 10);
            continue;
        }

        // 精准seek
        if (seeking_flag) {
            auto cur_frame_pts_avtimebase =
                    av_rescale_q(frame->pts, time_base, AV_TIME_BASE_Q);
            if (cur_frame_pts_avtimebase < target_seek_pos_avtimebase) {
                // 释放frame
                releaseAvFrame(&frame);
                continue;
            } else {
                seeking_flag = false;
                LOGD("视频精准seeked");
            }
        }
        frames.push(frame);
    }
    releaseAvPacket(&packet);
    LOGD("视频解码线程推出");
}

void VideoChannel::pause() {
    isPause = 1;
}

void VideoChannel::resume() {
    isPause = 0;
    cond.notify_one();
}

void VideoChannel::stop() {
    isPlaying = 0;
    frames.setWork(0);
    frames.clear();
    packets.setWork(0);
    packets.clear();
}


void VideoChannel::setSpeed(float ss) {
    speed = ss;
}

float VideoChannel::getSpeed() {
    return speed;
}
