//
// Created by Administrator on 2025/3/30.
//
#ifndef THREADPOOL_H
#define THREADPOOL_H
#include <atomic>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include "LockFreeQueue.h"
#include <iostream>
#include <pthread.h>
#include "Log.h"

class ThreadPool
{
public:
    ThreadPool(size_t corePoolSize, size_t maximumPoolSize, size_t queueCapacity);
    ~ThreadPool();

    int submit(std::function<void()> task);
    void shutdown();

private:
    void workerThread();

    const size_t corePoolSize;
    const size_t maximumPoolSize;
    std::atomic<size_t> activeThreads;

    LockFreeQueue<std::function<void()>> taskQueue; // 无锁任务队列
    std::vector<std::thread> workers;

    std::mutex mtx; // 多线程提交任务需要开启
    std::condition_variable cv;
    std::atomic<bool> isShutdown;

    bool addWorker(std::function<void()> task);
};


class GlobalThreadPool
{
public:
    static int32_t corePoolSize;
    static int32_t maximumPoolSize;
    static int32_t queueCapacity;
    static void initThreadPool(int32_t corePoolSize, int32_t maximumPoolSize, int32_t queueCapacity)
    {
        GlobalThreadPool::corePoolSize = corePoolSize;
        GlobalThreadPool::maximumPoolSize = maximumPoolSize;
        GlobalThreadPool::queueCapacity = queueCapacity;
    }
    static ThreadPool &getInstance()
    {
        static ThreadPool instance(GlobalThreadPool::corePoolSize, GlobalThreadPool::maximumPoolSize, GlobalThreadPool::queueCapacity);
        return instance;
    }

private:
    GlobalThreadPool() = delete;
    GlobalThreadPool(const GlobalThreadPool &) = delete;
    GlobalThreadPool &operator=(const GlobalThreadPool &) = delete;
};

#endif