#include "AudioChannel.h"

extern "C"
{
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}

#include "Constant.h"
#include "AudioChannel.h"
#include "ThreadPool.h"
#include "AAudioRender.h"

void *audio_decode(void *args) {
    AudioChannel *audioChannel = static_cast<AudioChannel *>(args);
    audioChannel->decode();
    return 0;
}

void *audio_play(void *args) {
    AudioChannel *audioChannel = static_cast<AudioChannel *>(args);
    LOGD("音频播放\n");
    audioChannel->start();
    return 0;
}

AudioChannel::AudioChannel(int id, AVCodecContext *avCodecContext,
                           AVRational time_base)
        : BaseChannel(id, avCodecContext, time_base) {
    out_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    out_samplesize = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    out_sample_rate = 44100;
    data = static_cast<uint8_t *>(malloc(out_sample_rate * out_channels * out_samplesize));
    memset(data, 0, out_sample_rate * out_channels * out_samplesize);
    ringQueueFrames = new RingQueue<AVFrame *>(1024);
    audioRender = new AAudioRender();
    ringQueueFrames->setReleaseCallback(releaseAvFrame);
}

AudioChannel::~AudioChannel() {
    if (data) {
        free(data);
        data = 0;
    }
    delete ringQueueFrames;
}

void AudioChannel::play() {
    swrContext = swr_alloc_set_opts(0, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, out_sample_rate,
                                    avCodecContext->channel_layout, avCodecContext->sample_fmt,
                                    avCodecContext->sample_rate, 0, 0);
    swr_init(swrContext);
    startWork();
    isPlaying = 1;
    // 音频解码
    bool ret = GlobalThreadPool::getInstance().submit([this]() {
        audio_decode(this);
    });
    if (ret<0) {
        LOGE("submit to thread pool failed 2");
    }
    LOGD("创建音频解码线程");
    ret = GlobalThreadPool::getInstance().submit([this]() {
        audio_play(this);
    });
    if (ret<0) {
        LOGE("submit to thread pool failed 3");
    }
    LOGD("创建音频播放线程");
}

void AudioChannel::startWork(){
        packets.setWork(1);
        frames.setWork(1);
        ringQueueFrames->setWork(1);
}

void AudioChannel::decode() {
    AVPacket *packet = 0;
    int target_seek_pos_avtimebase = 0;
    bool seeking_flag = false;
    while (isPlaying) {
        int ret = packets.pop(packet);
        if (!ret) {
            continue;
        }
        // seek
        if (packet->stream_index == FF_SEEK_PACKET_INDEX) {
            avcodec_flush_buffers(avCodecContext);
            ringQueueFrames->setWork(0);
            ringQueueFrames->clear();
            ringQueueFrames->setWork(1);
            seeking_flag = true;
            target_seek_pos_avtimebase = packet->pos;
            releaseAvPacket(&packet);
            continue;
        }
        ret = avcodec_send_packet(avCodecContext, packet);
        releaseAvPacket(&packet);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret < 0) {
            break;
        }
        AVFrame *frame = av_frame_alloc();
        ret = avcodec_receive_frame(avCodecContext, frame);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret < 0) {
            break;
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
                LOGD("音频精准seeked");
            }
        }
        ringQueueFrames->push(frame);
    }
    if (packet) {
        releaseAvPacket(&packet);
    }
    LOGD("音频解码线程推出");
};

int AudioChannel::resampleAudio(void **buf) {
    int data_size = 0;
    int sound_size = 0;
    AVFrame *frame = 0;
   std::unique_lock <std::mutex> lock(mutex);
    while (isPlaying) {
        if (isPause) {
            cond.wait(lock, [this] { return !this->isPause; });
        }

//        int ret = frames.pop(frame);
        int ret = ringQueueFrames->pop(frame);
        if (!ret) {
            LOGE("重采样环形队列出队失败:%d", ret);
            continue;
        }
        int64_t delays = swr_get_delay(swrContext, frame->sample_rate);
        float nowSpeed = getSpeed();

        // 根据速度调整采样数量
        int64_t max_samples = av_rescale_rnd(delays + frame->nb_samples, out_sample_rate,
                                             frame->sample_rate, AV_ROUND_UP);

        int samples = swr_convert(swrContext, &data, max_samples, (const uint8_t **) frame->data,
                                  frame->nb_samples);
        data_size = samples * out_samplesize * out_channels;

        // 变速处理部分修改为：
        if (getSpeed() != 1.0) {
            if (!soundtouch || !soundTouchBuffer) {
                initSoundTouch();
            }

            // 确保输入数据足够大
            if (data_size >= 1024) { // 最小处理块大小
                int processed_size = adjustPitchTempo(data, data_size, soundTouchBuffer);
                if (processed_size > 0) {
                    *buf = soundTouchBuffer;
                    data_size = processed_size;
                }
            }
        }
        // 记录音频当前播放时间
        playerContext->setClock(playerContext->audio_clock_t,
                                frame->pts * av_q2d(audio_stream->time_base) +
                                playerContext->audio_hw_delay);

        updatePCM16bitDB(reinterpret_cast<char *>(*buf), data_size);
        clock = frame->best_effort_timestamp * av_q2d(time_base);
        break;
    }

    if (frame) {
        releaseAvFrame(&frame);
    }
    return data_size;
}

int audioCallback(AAudioStream *stream, void *user_data, void *audioData, int32_t numFrames) {
    AudioChannel *audioChannel = static_cast<AudioChannel *>(user_data);
    void *buf;
    int dataSize = audioChannel->resampleAudio(&buf);
    //    LOGD("numFrames %d\n", numFrames);
    if (dataSize <= 0) {
        return 1;
    }
    // LOGD("dataSize %d\n", dataSize);
    memcpy(audioData, audioChannel->data, dataSize);
    //    memcpy(audioData, buf, dataSize);
    return 0;
}

int AudioChannel::adjustPitchTempo(uint8_t *in, int inSize, soundtouch::SAMPLETYPE *out) {
    // 确保输入数据足够大（至少包含一个完整的音频帧）
    const int minRequiredSize = 1024; // 最小处理块大小
    if (inSize < minRequiredSize || in == nullptr || out == nullptr) {
        LOGW("音频数据块太小或无效: %d bytes", inSize);
        return 0;
    }

    // 计算16-bit样本数
    const int sampleCount = inSize / 2;
    const int framesNeeded = soundtouch->numUnprocessedSamples() +
                             (sampleCount / out_channels);

    // 确保SoundTouch有足够的处理能力
    if (framesNeeded * out_channels > out_sample_rate) {
        soundtouch->flush();
        LOGW("SoundTouch缓冲区溢出，已刷新");
    }

    // 转换16-bit PCM到float
    const int16_t *pcmIn = reinterpret_cast<const int16_t *>(in);
    for (int i = 0; i < sampleCount; i++) {
        out[i] = pcmIn[i] / 32768.0f;
    }

    // 分块处理以避免断言错误
    int processedTotal = 0;
    const int chunkSize = out_sample_rate / 2; // 每次处理半秒音频

    for (int pos = 0; pos < sampleCount; pos += chunkSize) {
        int samplesToProcess = std::min(chunkSize, sampleCount - pos);
        int framesToProcess = samplesToProcess / out_channels;

        soundtouch->putSamples(out + pos, framesToProcess);

        // 获取处理后的样本
        int received = 0;
        do {
            received = soundtouch->receiveSamples(out + processedTotal,
                                                  (sampleCount - processedTotal) / out_channels);
            processedTotal += received * out_channels;
        } while (received > 0);
    }

    // 转换回16-bit PCM
    if (processedTotal > 0) {
        int16_t *pcmOut = reinterpret_cast<int16_t *>(in);
        for (int i = 0; i < processedTotal; i++) {
            float sample = std::max(-1.0f, std::min(1.0f, out[i]));
            pcmOut[i] = static_cast<int16_t>(sample * 32767.0f);
        }
        return processedTotal * 2; // 返回字节数
    }

    return 0;
}


void AudioChannel::updatePCM16bitDB(char *data, int dataBytes) {
    if (dataBytes <= 0) {
        amplitudeAvg = 0;
        soundDecibels = 0;
        return;
    }

    // 因此这里要把两个 8 位转成 16 位的数据再计算振幅
    short int amplitude = 0; // 16bit 采样值
    double amplitudeSum = 0; // 16bit 采样值加和
    for (int i = 0; i < dataBytes; i += 2) {
        memcpy(&amplitude, data + i, 2); // 把 char（8位） 转为 short int（16位）
        amplitudeSum += abs(amplitude);  // 把这段时间的所有采样值加和
    }
    // 更新振幅平均值
    amplitudeAvg = amplitudeSum / (dataBytes / 2); // 除数是 16 位采样点个数

    // 更新分贝值：分贝 = 20 * log10(振幅)
    if (amplitudeAvg > 0) {
        soundDecibels = 20 * log10(amplitudeAvg);
    } else {
        soundDecibels = 0;
    }
    // LOGD("amplitudeAvg %lf, soundDecibels %lf", amplitudeAvg, soundDecibels);
}

void AudioChannel::initSoundTouch() {
    if (soundTouchBuffer == NULL) {
        soundTouchBuffer = static_cast<soundtouch::SAMPLETYPE *>(malloc(44100 * 2 * 2)); // 增加缓冲区大小
    }

    if (soundtouch == NULL) {
        soundtouch = new soundtouch::SoundTouch();
        soundtouch->setSampleRate(out_sample_rate);
        soundtouch->setPitch(1.0); // 保持音调不变
        soundtouch->setChannels(out_channels);
        soundtouch->setTempo(getSpeed()); // 设置当前速度
    }
}

void AudioChannel::setSpeed(float s) {
    speed = s;
    if (soundtouch) {
        // 确保SoundTouch使用最新速度并清空旧数据
        soundtouch->setTempo(speed);
        if (soundtouch->numUnprocessedSamples() > out_sample_rate) {
            soundtouch->flush();
        }
    }
}

void AudioChannel::pause() {
    isPause = 1;
    audioRender->pause(isPause);
}

void AudioChannel::resume() {
    isPause = 0;
    audioRender->pause(isPause);
    cond.notify_all();
}

void AudioChannel::stop() {
    isPlaying = 0;
    packets.setWork(0);
    frames.setWork(0);
    ringQueueFrames->setWork(0);
    if (swrContext) {
        swr_free(&swrContext);
        swrContext = 0;
    }
}

void AudioChannel::initAudioRender() {
    audioRender = new AAudioRender();
}

void AudioChannel::start() {
    LOGD("playing......\n");
    LOGD("audioRender: %p \n", audioRender);
    audioRender->setCallback(audioCallback, this);
    audioRender->start();
}

float AudioChannel::getSpeed() {
    return speed;
}