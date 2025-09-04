//
// Created by Administrator on 2025/3/29.
//

#ifndef PLAYER_H
#define PLAYER_H
#include "VideoChannel.h"
#include "ANWRender.h"
#include "PlayerContext.h"
#include "AudioChannel.h"

extern "C"
{
#include "libavformat/avformat.h"
}

class Player {
private:
    // 私有化构造，拷贝构造，赋值构造，防止浅拷贝和深拷贝
    static std::mutex imutex;
    static Player* instance;
    Player(const char *dataSource);
    Player();
    Player(const Player &player);
    void operator=(const Player &player);
public:
    static char *dataSource;
    // 双重判断+锁机制的单例Player
    static Player* getInstance(){
        if (Player::instance == nullptr){
            std::lock_guard<std::mutex> lock(Player::imutex);
            if (Player::instance == nullptr){
                if (!Player::dataSource){
                    LOGE("dataSource is NULL\n");
                    return nullptr;
                }
                Player::instance = new Player(Player::dataSource);
            }
            return Player::instance;
        }
        return Player::instance;
    }
public:
    pthread_t pid_player=0;
    pthread_t pid_demux = 0;
    pthread_t pid_stop = 0;

    AVFormatContext *formatContext = 0;
    VideoChannel *videoChannel = 0;
    AudioChannel *audioChannel = 0;
    bool isPlaying = 0;
    double duration = 0;
    int width;
    int height;
    PlayerContext* playerContext = 0;

    ~Player();
    void prepare();
    void start();
    void stop();
    void demux();
    void pause();
    void resume();
    double getPosition();
    void speed(float speed);
    double getDuration() {
        return duration;
    }

    void seek(float abs_position);

};
#endif //PLAYER_H
