//
// Created by Administrator on 2025/3/30.
//

#include "LockFreeQueue.h"
#include <functional>

template <typename T>
LockFreeQueue<T>::LockFreeQueue(size_t capacity)
        : buffer(new T[capacity]), head({0, 0}), tail({0, 0}), capacity(capacity) {
            sem_init(&notEmpty, 0, 0);
        }

template <typename T>
LockFreeQueue<T>::~LockFreeQueue()
{
    sem_destroy(&notEmpty);
    delete[] buffer;
}

template <typename T>
bool LockFreeQueue<T>::enqueue(const T &item)
{
    TaggedPtr currTail = tail.load();
    uint32_t newTailPtr = (currTail.ptr + 1) % capacity;
    if (newTailPtr == head.load().ptr)
    {
        return false;
    }
    TaggedPtr newTail = {newTailPtr, currTail.tag + 1};
    while (true)
    {
        if (tail.compare_exchange_weak(currTail, newTail))
        {
            buffer[currTail.ptr] = item;
            sem_post(&notEmpty);
            return true;
        }
        currTail = tail.load();
        newTailPtr = (currTail.ptr + 1) % capacity;
        if (newTailPtr == head.load().ptr)
        {
            return false;
        }
        newTail = {newTailPtr, currTail.tag + 1};
    }
}

template <typename T>
bool LockFreeQueue<T>::dequeue(T &item)
{
    sem_wait(&notEmpty);
    TaggedPtr currHead = head.load();
    if (currHead.ptr == tail.load().ptr)
    {
        return false;
    }
    uint32_t newHeadPtr = (currHead.ptr + 1) % capacity;
    TaggedPtr newHead = {newHeadPtr, currHead.tag + 1};
    while (true)
    {
        if (head.compare_exchange_weak(currHead, newHead))
        {
            item = buffer[currHead.ptr];
            return true;
        }
        currHead = head.load();
        if (currHead.ptr == tail.load().ptr)
        {
            return false;
        }
        newHeadPtr = (currHead.ptr + 1) % capacity;
        newHead = {newHeadPtr, currHead.tag + 1};
    }
};

// cmake必须模板具体化声明
template class LockFreeQueue<std::function<void()>>;