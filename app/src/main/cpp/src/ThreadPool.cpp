#include "ThreadPool.h"
#include <thread>
int32_t GlobalThreadPool::corePoolSize = 2;
int32_t GlobalThreadPool::maximumPoolSize = 4;
int32_t GlobalThreadPool::queueCapacity = 256;

ThreadPool::ThreadPool(size_t corePoolSize, size_t maximumPoolSize, size_t queueCapacity)
    : corePoolSize(corePoolSize),
      maximumPoolSize(maximumPoolSize),
      activeThreads(0),
      taskQueue(queueCapacity),
      isShutdown(false) {}

ThreadPool::~ThreadPool()
{
    shutdown();
}

int ThreadPool::submit(std::function<void()> task)
{
    if (isShutdown.load())
    {
        return -2;
    }

    if (activeThreads.load() < corePoolSize && addWorker(task))
    {
        return 0;
    }

    if (taskQueue.enqueue(task))
    {
        cv.notify_one();
        return 2;
    }

    if (activeThreads.load() < maximumPoolSize && addWorker(task))
    {
        return 3;
    }

    // std::cerr << "Task discarded: thread pool is full" << std::endl;
    return -1;
}

bool ThreadPool::addWorker(std::function<void()> task)
{
    std::lock_guard<std::mutex> lock(mtx);
    if (activeThreads.load() >= maximumPoolSize)
    {
        return false;
    }

    workers.emplace_back([this, task]
                         {
                             activeThreads.fetch_add(1);
                             if (task) {
                                 task();
                             }
                             workerThread(); });

    return true;
}

void ThreadPool::workerThread()
{
    while (!isShutdown.load())
    {
        std::function<void()> task;

        if (taskQueue.dequeue(task))
        {
            task();
        }
        else
        {
            std::unique_lock<std::mutex> lock(mtx);
            if (activeThreads.load() > corePoolSize)
            {
                activeThreads.fetch_sub(1);
                return;
            }
            cv.wait(lock, [this]
                    { return !taskQueue.empty() || !isShutdown.load(); });
        }
    }
    activeThreads.fetch_sub(1);
}

void ThreadPool::shutdown()
{
    isShutdown.store(true);
    LOGD("线程池停止中");
    cv.notify_all();
    for (std::thread &worker : workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
    workers.clear();
    LOGD("线程池终止");
}