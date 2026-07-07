#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <iostream>

// ── ThreadPool ────────────────────────────────────────────────────────────
// A fixed-size pool of worker threads that process tasks from a shared queue.
// Used by UPF to handle packet classification requests concurrently.
//
// Implemented for COTS/GPP hardware deployment.

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads) : stop_(false) {
        std::cout << "[ThreadPool] Starting " << num_threads << " worker threads\n";
        for (size_t i = 0; i < num_threads; i++) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        // Wait until there is a task or we are stopping
                        condition_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                    tasks_completed_++;
                }
            });
        }
    }

    // Submit a task to the pool
    void enqueue(std::function<void()> task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.push(std::move(task));
        }
        condition_.notify_one();
        tasks_submitted_++;
    }

    // Metrics
    uint64_t submitted()  const { return tasks_submitted_.load(); }
    uint64_t completed()  const { return tasks_completed_.load(); }
    size_t   queue_size() const {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_.size();
    }
    size_t   num_threads() const { return workers_.size(); }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto& w : workers_) w.join();
        std::cout << "[ThreadPool] Stopped. Tasks completed: "
                  << tasks_completed_ << "\n";
    }

private:
    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex                queue_mutex_;
    std::condition_variable           condition_;
    std::atomic<bool>                 stop_;
    std::atomic<uint64_t>             tasks_submitted_{0};
    std::atomic<uint64_t>             tasks_completed_{0};
};
