//
// Created by Administrator on 2025/3/29.
//

#ifndef QUEUE_H
#define QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

template<typename T>
class Queue {
    using ReleaseCallback = std::function<void(T*)>;
    using DropHandle = std::function<void(Queue<T>&)>;
public:
    Queue() : work(0), releaseCallback(nullptr) {}
    ~Queue() {
        clear();
    }

    void push(T new_value) {
        std::lock_guard<std::mutex> lk(mt);
        if (work) {
            q.push(new_value);
            cv.notify_one();
        } else {
            if (releaseCallback) {
                releaseCallback(&new_value);
            }
        }
    }

    int pop(T& value) {
        std::unique_lock<std::mutex> lk(mt);
        cv.wait(lk, [this] { return !work || !q.empty(); });
        if (!q.empty()) {
            value = q.front();
            q.pop();
            return 1;
        }
        return 0; // Return failure flag
    }

    void setWork(int work) {
        std::lock_guard<std::mutex> lk(mt);
        this->work = work;
        cv.notify_all();
    }

    int empty() {
        std::lock_guard<std::mutex> lk(mt);
        return q.empty();
    }

    int size() {
        std::lock_guard<std::mutex> lk(mt);
        return q.size();
    }

    void drop(){
       dropHandle(*this);
    }


    void setDropHandle(DropHandle s) {
        dropHandle = s;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mt);
        while (!q.empty()) {
            T value = q.front();
            if (releaseCallback) {
                releaseCallback(&value);
            }
            q.pop();
        }
    }

    void setReleaseCallback(ReleaseCallback r) {
        std::lock_guard<std::mutex> lk(mt);
        releaseCallback = r;
    }

private:
    std::mutex mt;
    std::condition_variable cv;
    std::queue<T> q;
    int work;
    ReleaseCallback releaseCallback;
    DropHandle  dropHandle;
};

#endif //QUEUE_H