//
// Created by Administrator on 2025/3/30.
//

#ifndef ANDROIDPLAYER_LOCKFREEQUEUE_H
#define ANDROIDPLAYER_LOCKFREEQUEUE_H
#include <atomic>
#include <stdexcept>
#include <cstdint>
#include "semaphore.h"

template <typename T>
class LockFreeQueue
{
private:
    struct TaggedPtr
    {
        uint32_t ptr;
        uint32_t tag;
    };

    T *buffer;
    std::atomic<TaggedPtr> head, tail;
    const size_t capacity;
    sem_t notEmpty;

public:
    LockFreeQueue(size_t capacity);
    ~LockFreeQueue();

    bool enqueue(const T &item);
    bool dequeue(T &item);
    bool empty() const;

    LockFreeQueue(const LockFreeQueue &) = delete;
    LockFreeQueue &operator=(const LockFreeQueue &) = delete;
};

template<typename T>
bool LockFreeQueue<T>::empty() const {
    if (head.load().ptr == tail.load().ptr) {
        return true;
    }
    return false;
}


#endif //ANDROIDPLAYER_LOCKFREEQUEUE_H
