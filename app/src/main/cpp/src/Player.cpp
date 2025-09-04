//
// Created by Administrator on 2025/3/29.
//
#include <cstring>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

extern "C"
{
#include <libavutil/time.h>
}
#include <cstring>
#include "Log.h"
#include "Player.h"
#include "EGLRender.h"
#include "PlayerContext.h"
#include "Constant.h"
#include "ThreadPool.h"

std::mutex Player::imutex;
Player* Player::instance = nullptr;
char* Player::dataSource = nullptr;

void *demux_task(void *args) {
    Player *player = static_cast<Player *>(args);
    player->demux();
    return 0;
}


Player::Player(const char *dataSource) {
    this->dataSource = new char[strlen(dataSource) + 1];
    strcpy(this->dataSource, dataSource);
    duration = 0;
}
Player::Player(){
    duration = 0;
}

Player::~Player() {
    if (formatContext) {
        avformat_close_input(&formatContext);
        avformat_free_context(formatContext);
        formatContext = nullptr;
    }
    if (videoChannel) {
        delete videoChannel;
        videoChannel = nullptr;
    }
    // AudioChannel
    if (audioChannel) {
        delete audioChannel;
        audioChannel = nullptr;
    }
    if (Player::dataSource) {
        delete[] Player::dataSource;
        Player::dataSource = nullptr;
    }
    if (Player::instance){
        std::lock_guard<std::mutex> lock(Player::imutex);
        delete Player::instance;
        Player::instance = nullptr;
    }
}

void Player::prepare() {
    playerContext = new PlayerContext();
    formatContext = avformat_alloc_context();
    AVDictionary *options = 0;
    // 超时时间3秒
    av_dict_set(&options, "timeout", "3000000", 0);
    int ret = avformat_open_input(&formatContext, dataSource, 0, &options);
    av_dict_free(&options);
    if (ret != 0) {
        LOGE("avformat_open_input failed:%s", av_err2str(ret));
        return;
    }
    ret = avformat_find_stream_info(formatContext, 0);
    if (ret < 0) {
        LOGE("avformat_find_stream_info failed:%s", av_err2str(ret));
        return;
    }
    duration = formatContext->duration / (double)AV_TIME_BASE;
    LOGI("duration:%f\n", duration);
    if (duration <= 0) {
        duration = 0;
    }

    int video_stream_index = -1;
    int audio_stream_index = -1;
    bool is_set_width = false;

    for (int i = 0; i < formatContext->nb_streams; ++i) {
        AVStream *stream = formatContext->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;

        // Skip unsupported codec types
        if (codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            continue;
        }

        AVCodec *dec = avcodec_find_decoder(codecpar->codec_id);
        if (!dec) {
            LOGE("Unsupported codec: %s (id=%d)",
                 avcodec_get_name(codecpar->codec_id),
                 codecpar->codec_id);
            continue;  // Skip this stream instead of failing
        }
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

        // 打开解码器
        ret = avcodec_open2(context, dec, 0);
        if (ret < 0) {
            LOGE("avcodec_open2 failed: %s", av_err2str(ret));
            if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);
            return;
        }
        if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);

        AVRational time_base = stream->time_base;
        if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            AVStream *video_stream = formatContext->streams[video_stream_index];
            videoChannel = new VideoChannel(video_stream_index, context, video_stream->time_base,
                                            av_q2d(video_stream->avg_frame_rate));
            videoChannel->playerContext = playerContext;

            playerContext->video_stream = video_stream;
            playerContext->video_base_time = video_stream->time_base;

            LOGE("time_base:%d", time_base.num);
            width = context->width;
            height = context->height;
        } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            AVStream *audio_stream = formatContext->streams[audio_stream_index];
            audioChannel = new AudioChannel(audio_stream_index, context, audio_stream->time_base);
            audioChannel->initAudioRender();
            audioChannel->audio_stream = audio_stream;
            audioChannel->playerContext = playerContext;

            playerContext->audio_stream = audio_stream;
            playerContext->audio_base_time = audio_stream->time_base;
        }
    }
    if (video_stream_index == -1) {
        LOGE("Could not find video stream\n");
    }
    if (audio_stream_index == -1) {
        LOGE("Could not find audio stream\n");
    }
    if (video_stream_index == -1) {
        LOGE("video_stream_index == -1");
    }
    if (audio_stream_index == -1) {
        LOGE("audio_stream_index == -1");
    }
    double duration = formatContext->duration / (double) AV_TIME_BASE;
    playerContext->video_duration = duration;
    LOGD("视频时间： %f\n", playerContext->video_duration);
    if (duration <= 0) {
        LOGE("duration <= 0");
    }
}

void Player::start() {
    LOGD("创建解复用线程");
    if (isPlaying) {
        return;
    }
    isPlaying = 1;
    if (videoChannel) {
        videoChannel->play();
    }
    if (audioChannel){
        audioChannel->play();
    }
    bool ret = GlobalThreadPool::getInstance().submit([this]() {
        this->demux();
    });
    if (ret<0) {
        LOGE("submit to thread pool failed 1");
    }
}

void Player::seek(float abs_position) {
    if (abs_position < 0 || abs_position >= 1) {
        return;
    }

    if (!videoChannel || !formatContext) {
        return;
    }
    playerContext->doSeekAbsolute(abs_position);
}


void Player::stop() {
    if (!isPlaying) {
        return;
    }
    isPlaying = 0;
    if (videoChannel){
        videoChannel->stop();
    }
    if(audioChannel){
        audioChannel->stop();
    }
}

void Player::pause() {
    if (audioChannel) {
        audioChannel->pause();
    }
    if (videoChannel) {
        videoChannel->pause();
    }
};

void Player::resume() {
    if (audioChannel) {
        audioChannel->resume();
    }

    if (videoChannel) {
        videoChannel->resume();
    }
}

void Player::speed(float speed) {
    if (speed <= 0) {
        return;
    }
//     设置音频播放速度
    if (audioChannel) {
        audioChannel->setSpeed(speed);
    }

    // 设置视频播放速度
    if (videoChannel) {
        videoChannel->setSpeed(speed);
    }
}


double Player::getPosition() {
    std::unique_lock<std::mutex> lock(playerContext->seekMutex);
    playerContext->seekCond.wait(lock, [this] { return !playerContext->seek_req.load() || !isPlaying; }); // 等待条件
    double position = 0;
    if (audioChannel) {
        LOGD("audio Time: %f, duration: %f", playerContext->getAudioRelaTime(), playerContext->video_duration);
        position = playerContext->getAudioRelaTime() / playerContext->video_duration;
    }
    return position;
}