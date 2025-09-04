#ifndef ARINGQUEUE_H
#define ARINGQUEUE_H

#include <mutex>
#include <condition_variable>
#include <vector>
#include <functional>

template<typename T>
class RingQueue {
    using ReleaseCallback = std::function<void(T*)>;

public:
    RingQueue(size_t capacity) : capacity(capacity), head(0), tail(0), count(0), work(1), releaseCallback(nullptr) {
        buffer.resize(capacity);
    }

    ~RingQueue() {
        clear();
    }

    void push(T new_value) {
        std::lock_guard<std::mutex> lk(mt);
        if (work) {
            if (count < capacity) {
                buffer[tail] = new_value;
                tail = (tail + 1) % capacity;
                count++;
                cv.notify_one();
            } else {
                if (releaseCallback) {
                    releaseCallback(&new_value);
                }
            }
        } else {
            if (releaseCallback) {
                releaseCallback(&new_value);
            }
        }
    }

    int pop(T& value) {
        std::unique_lock<std::mutex> lk(mt);
        if (!work){
            return 0;
        }
        cv.wait(lk, [this] { return count > 0; });
        value = buffer[head];
        head = (head + 1) % capacity;
        count--;
        return 1;
    }

    void setWork(int work) {
        std::lock_guard<std::mutex> lk(mt);
        this->work = work;
        cv.notify_all();
    }

    int empty() {
        std::lock_guard<std::mutex> lk(mt);
        return count == 0;
    }

    int size() {
        std::lock_guard<std::mutex> lk(mt);
        return count;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(mt);
        while (count > 0) {
            T value = buffer[head];
            if (releaseCallback) {
                releaseCallback(&value);
            }
            head = (head + 1) % capacity;
            count--;
        }
    }

    void setReleaseCallback(ReleaseCallback r) {
        std::lock_guard<std::mutex> lk(mt);
        releaseCallback = r;
    }

private:
    std::mutex mt;
    std::condition_variable cv;
    std::vector<T> buffer; // 环形缓冲区
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    int work;
    ReleaseCallback releaseCallback;
};

#endif //ARINGQUEUE_H