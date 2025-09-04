#ifndef BASECHANNEL_H
#define BASECHANNEL_H

#include "Queue.h"

extern "C"
{
#include <libavcodec/avcodec.h>
};

class BaseChannel
{
public:
    BaseChannel(int id, AVCodecContext *avCodecContext,
                AVRational time_base) : id(id),
                                        avCodecContext(avCodecContext),
                                        time_base(time_base)
    {
        frames.setReleaseCallback(releaseAvFrame);
        packets.setReleaseCallback(releaseAvPacket);
    }

    // virtual
    virtual ~BaseChannel()
    {
        frames.clear();
        packets.clear();
        if (avCodecContext)
        {
            avcodec_close(avCodecContext);
            avcodec_free_context(&avCodecContext);
            avCodecContext = 0;
        }
    }

    static void releaseAvPacket(AVPacket **packet)
    {
        if (packet)
        {
            av_packet_free(packet);
            *packet = 0;
        }
    }

    static void releaseAvFrame(AVFrame **frame)
    {
        if (frame)
        {
            av_frame_free(frame);
            *frame = 0;
        }
    }

    void clear()
    {
        packets.clear();
        frames.clear();
    }

    void stopWork()
    {
        packets.setWork(0);
        frames.setWork(0);
    }

    void startWork()
    {
        packets.setWork(1);
        frames.setWork(1);
    }

    virtual void play() = 0;


    virtual void stop() = 0;

    int id;
    bool isPlaying;
    bool isPause = 0;

    // 编码数据包队列
    Queue<AVPacket *> packets;
    // 解码数据包队列
    Queue<AVFrame *> frames;
    AVCodecContext *avCodecContext;
    AVRational time_base;

public:
    double clock;
};

#endif // BASECHANNEL_H