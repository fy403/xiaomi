#include "Player.h"
#include "Log.h"
extern "C"
{
#include <libavutil/time.h>
}
#include "Constant.h"
// 解复用器
void Player::demux() {
    LOGI("开始解复用");
    int ret = 0;
    while (isPlaying) {
        if (playerContext->seek_req) {
            LOGD("Seek Clear..........\n");
            std::lock_guard<std::mutex> lg(playerContext->seekMutex);
            int64_t seek_pos = playerContext->seek_pos;
            int64_t seek_rel = playerContext->seek_rel;
            int seek_flags = AVSEEK_FLAG_BACKWARD;
            seek_flags = playerContext->seek_flags;
            // seek通知包
            AVPacket *audio_seek_packet = av_packet_alloc();
            if (!audio_seek_packet) {
                LOGE("Failed to allocate seek packet");
                playerContext->seek_req = false;
                continue;
            }
            // seek容忍度
            auto min_ts = (seek_rel > 0) ? (seek_pos - seek_rel + 2) : (INT64_MIN);
            auto max_ts = (seek_rel < 0) ? (seek_pos - seek_rel - 2) : (seek_pos);
            ret = avformat_seek_file(formatContext, -1, min_ts, seek_pos, max_ts, seek_flags);
            if (ret < 0) {
                LOGE("%s: error while seeking %s\n", formatContext->url, av_err2str(ret));
            } else {
                audio_seek_packet->stream_index = FF_SEEK_PACKET_INDEX;
                audio_seek_packet->pos = seek_pos;
                if (audioChannel) {
                    audioChannel->stopWork();
                    audioChannel->clear();
                    audioChannel->startWork();
                    audioChannel->packets.push(audio_seek_packet);
                }else{
                    av_packet_free(&audio_seek_packet);
                }
                if (videoChannel) {
                    AVPacket *video_seek_packet = av_packet_clone(audio_seek_packet);
                    videoChannel->stopWork();
                    videoChannel->clear();
                    videoChannel->startWork();
                    // 必须深拷贝
                    videoChannel->packets.push(video_seek_packet);
                }
            }
            playerContext->seek_req = false;
            playerContext->seekCond.notify_all();
            LOGD("Seek Success.........\n");
            continue;
        }

        // Flow control
        if (audioChannel && audioChannel->packets.size() > 100) {
            av_usleep(1000 * 10);
            continue;
        }
        if (videoChannel && videoChannel->packets.size() > 100) {
            av_usleep(1000 * 10);
            continue;
        }

        AVPacket *packet = av_packet_alloc();
        if (!packet) {
            LOGE("Failed to allocate packet");
            av_usleep(1000 * 10);
            continue;
        }

        ret = av_read_frame(formatContext, packet);
        if (ret == 0) {
            if (audioChannel && packet->stream_index == audioChannel->id) {
                audioChannel->packets.push(packet);
            } else if (videoChannel && packet->stream_index == videoChannel->id) {
                videoChannel->packets.push(packet);
            } else {
                av_packet_free(&packet);
            }
        } else if (ret == AVERROR_EOF) {
            av_packet_free(&packet);
            if ((!audioChannel ||
                 (audioChannel->packets.empty() && audioChannel->frames.empty())) &&
                (!videoChannel ||
                 (videoChannel->packets.empty() && videoChannel->frames.empty()))) {
                LOGE("播放结束");
                break;
            }
        } else {
            av_packet_free(&packet);
            LOGE("Error reading frame: %s", av_err2str(ret));
            continue;
        }
    }

    isPlaying = 0;
    if (audioChannel) audioChannel->stop();
    if (videoChannel) videoChannel->stop();
}