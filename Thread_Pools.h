#include<thread>
#include<iostream>
#include<memory>
#include<functional>
#include<vector>
#include<queue>
#include<mutex>
#include<condition_variable>
#include<atomic>
#include<future>
#include<string>
#include<stdexcept>
#include<type_traits>
#ifndef UNTITLED18_THREAD_POOLS_H
#define UNTITLED18_THREAD_POOLS_H

class Thread{
private:
    std::thread t;
    std::string name;
    int thread_id;
    bool is_occupied;
public:
    Thread(const Thread&other)=delete;
    Thread& operator=(const Thread&other)=delete;

    Thread(Thread&&other)noexcept;
    Thread& operator=(Thread&&other)noexcept;

    Thread(int id,std::function<void()>func);

    int getId()const;

    bool isOccupied()const;

    void setOccupied(bool occupied);

    std::string getName()const;

    void run();

    void setName(std::string name);

    void join();

    bool joinable()const;

    void detach();

    void stop();

    ~Thread();
};

class Thread_Pools {
private:
    std::vector<std::unique_ptr<Thread>> threads;
    std::queue<std::function<void()>> task_queue;

    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop_flag;

    size_t pool_size;

    void workerFunction(int thread_id);

public:
    explicit Thread_Pools(size_t pool_size = std::thread::hardware_concurrency());

    Thread_Pools(const Thread_Pools&) = delete;
    Thread_Pools& operator=(const Thread_Pools&) = delete;

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type>;

    void shutdown();

    size_t getPoolSize() const;

    size_t getTaskQueueSize();

    bool isRunning() const;

    ~Thread_Pools();
};

template<typename F, typename... Args>
auto Thread_Pools::submit(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {

    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        if (stop_flag.load()) {
            throw std::runtime_error("Cannot submit task to stopped ThreadPool");
        }

        task_queue.emplace([task]() {
            (*task)();
        });
    }

    condition.notify_one();
    return result;
}

#endif //UNTITLED18_THREAD_POOLS_H
