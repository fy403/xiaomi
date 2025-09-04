#ifndef AUDIOCHANNEL_H
#define AUDIOCHANNEL_H

#include "BaseChannel.h"
#include "STTypes.h"
#include "SoundTouch.h"
#include "FIFOSampleBuffer.h"
#include "FIFOSamplePipe.h"
#include "BPMDetect.h"
#include "AAudioRender.h"
#include <shared_mutex>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include "PlayerContext.h"
#include "RingQueue.h"

extern "C"
{
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
#include "libavformat/avformat.h"
};
class AudioChannel : public BaseChannel {
public:
    AudioChannel(int id, AVCodecContext *avCodecContext, AVRational time_base);

    ~AudioChannel();

    void play();

    void pause();

    void resume();

    void stop();

    void decode();

    void start();

    void startWork();

    int resampleAudio(void **buf);

    void setSpeed(float speed);

    float getSpeed();

    void initAudioRender();

    void initSoundTouch();

    int adjustPitchTempo(uint8_t *in, int inSize, soundtouch::SAMPLETYPE *out);

    void updatePCM16bitDB(char *data, int dataBytes);

public:
    uint8_t *data = 0;
    int out_channels;
    int out_samplesize;
    int out_sample_rate;
    AVStream *audio_stream = 0;
    PlayerContext *playerContext;

private:
    RingQueue<AVFrame*>* ringQueueFrames=0;
    float speed = 1;

    soundtouch::SoundTouch *soundtouch = 0;
    soundtouch::SAMPLETYPE *soundTouchBuffer = 0;
    SwrContext *swrContext = 0;
    AAudioRender *audioRender = 0;
    std::mutex mutex;
    std::condition_variable cond;


    double amplitudeAvg = 0;  // 当前播放声音振幅平均值，即当前所有 16bit 采样值大小平均值
    double soundDecibels = 0; // 当前播放声音分贝值，单位：dB
};

#endif // AUDIOCHANNEL_H
