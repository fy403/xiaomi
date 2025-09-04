#ifndef VIDEOCHANNEL_H
#define VIDEOCHANNEL_H


#include "BaseChannel.h"
#include "Convert.h"
#include "Queue.h"
#include "PlayerContext.h"

extern "C" {
#include "libswscale/swscale.h"
};

class VideoChannel : public BaseChannel {
public:
    VideoChannel(int id, AVCodecContext *avCodecContext, AVRational time_base, int fps);

    ~VideoChannel();

    void play();

    void pause();

    void resume();

    void stop();

    void decode();

    void render();

    double getPosition();

    void setSpeed(float speed);

    float getSpeed();

    void handleSync(double videoClock, double frameDelay);

public:
    PlayerContext *playerContext;
private:
    pthread_t pid_decode;
    pthread_t pid_render;
    int fps;
    double position = 0;
    float speed = 1.0f;

    std::condition_variable cond;
    std::mutex mutex;

};


#endif // VIDEOCHANNEL_H
