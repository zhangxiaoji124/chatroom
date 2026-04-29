//
// Created by zhangxiaoji on 2026/4/29.
//

#include "Thread_Pools.h"

Thread::Thread(int id, std::function<void()> func)
    : thread_id(id), is_occupied(false), name("Thread-" + std::to_string(id)), t(func) {
}

Thread::Thread(Thread&& other) noexcept
    : t(std::move(other.t)),
      name(std::move(other.name)),
      thread_id(other.thread_id),
      is_occupied(other.is_occupied) {
    other.thread_id = -1;
    other.is_occupied = false;
}

Thread& Thread::operator=(Thread&& other) noexcept {
    if (this != &other) {
        if (t.joinable()) {
            t.join();
        }
        t = std::move(other.t);
        name = std::move(other.name);
        thread_id = other.thread_id;
        is_occupied = other.is_occupied;

        other.thread_id = -1;
        other.is_occupied = false;
    }
    return *this;
}

void Thread::run() {
    if (t.joinable()) {
        t.join();
    }
}

void Thread::setName(std::string name) {
    this->name = name;
}

std::string Thread::getName() const {
    return name;
}

int Thread::getId() const {
    return thread_id;
}

bool Thread::isOccupied() const {
    return is_occupied;
}

void Thread::setOccupied(bool occupied) {
    is_occupied = occupied;
}

void Thread::join() {
    if (t.joinable()) {
        t.join();
    }
}

bool Thread::joinable() const {
    return t.joinable();
}

void Thread::detach() {
    if (t.joinable()) {
        t.detach();
    }
}

void Thread::stop() {
    if (t.joinable()) {
        t.join();
    }
}

Thread::~Thread() {
    if (t.joinable()) {
        t.join();
    }
}

Thread_Pools::Thread_Pools(size_t pool_size)
    : stop_flag(false), pool_size(pool_size) {
    for (size_t i = 0; i < pool_size; ++i) {
        threads.emplace_back(std::make_unique<Thread>(
            static_cast<int>(i),
            [this, i]() {
                this->workerFunction(static_cast<int>(i));
            }
        ));
    }
}

void Thread_Pools::workerFunction(int thread_id) {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait(lock, [this] {
                return stop_flag.load() || !task_queue.empty();
            });

            if (stop_flag.load() && task_queue.empty()) {
                return;
            }

            task = std::move(task_queue.front());
            task_queue.pop();
        }

        threads[thread_id]->setOccupied(true);
        task();
        threads[thread_id]->setOccupied(false);
    }
}

void Thread_Pools::shutdown() {
    stop_flag.store(true);
    condition.notify_all();

    for (auto& thread : threads) {
        if (thread->joinable()) {
            thread->join();
        }
    }
}

size_t Thread_Pools::getPoolSize() const {
    return pool_size;
}

size_t Thread_Pools::getTaskQueueSize() {
    std::unique_lock<std::mutex> lock(queue_mutex);
    return task_queue.size();
}

bool Thread_Pools::isRunning() const {
    return !stop_flag.load();
}

Thread_Pools::~Thread_Pools() {
    if (!stop_flag.load()) {
        shutdown();
    }
}

