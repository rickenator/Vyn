// SPDX-License-Identifier: Apache-2.0

#include "vyb/runtime/async_runtime.hpp"
#include <iostream>
#include <chrono>
#include <algorithm>

namespace vyb {
namespace runtime {

// Task Implementation
Task::Task(TaskId id, TaskFunction func, CompletionCallback callback)
    : id_(id), state_(TaskState::PENDING), function_(std::move(func)),
      completion_callback_(std::move(callback)) {
}

void Task::execute() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != TaskState::PENDING && state_ != TaskState::SUSPENDED) {
        return; // Task not in executable state
    }

    state_ = TaskState::RUNNING;

    try {
        function_();
        complete(true);
    } catch (const std::exception& e) {
        std::cerr << "Task " << id_ << " failed with exception: " << e.what() << std::endl;
        complete(false);
    } catch (...) {
        std::cerr << "Task " << id_ << " failed with unknown exception" << std::endl;
        complete(false);
    }
}

void Task::suspend() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ == TaskState::RUNNING) {
        state_ = TaskState::SUSPENDED;
    }
}

void Task::resume() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ == TaskState::SUSPENDED) {
        state_ = TaskState::PENDING; // Ready to be scheduled again
    }
}

void Task::complete(bool success) {
    state_ = success ? TaskState::COMPLETED : TaskState::FAILED;

    if (completion_callback_) {
        completion_callback_(id_, success);
    }
}

// Future Implementation
Future::Future(TaskId task_id)
    : task_id_(task_id), ready_(false) {
}

bool Future::isReady() const {
    return ready_.load();
}

void Future::wait() {
    std::unique_lock<std::mutex> lock(value_mutex_);
    ready_condition_.wait(lock, [this] { return ready_.load(); });
}

template<typename T>
T Future::getValue() const {
    // This will be specialized for actual types when we integrate with the type system
    static_assert(sizeof(T) == 0, "Future::getValue must be specialized for each type");
}

template<typename T>
void Future::setValue(T value) {
    // This will be specialized for actual types when we integrate with the type system
    std::lock_guard<std::mutex> lock(value_mutex_);
    // Store value here when we implement type system integration
    ready_.store(true);
    ready_condition_.notify_all();
}

// AsyncRuntime Implementation
AsyncRuntime::AsyncRuntime()
    : running_(false), shutdown_requested_(false), next_task_id_(1), completed_task_count_(0) {
}

AsyncRuntime::~AsyncRuntime() {
    stop();
}

AsyncRuntime& AsyncRuntime::getInstance() {
    static AsyncRuntime instance;
    return instance;
}

TaskId AsyncRuntime::createTask(TaskFunction func, CompletionCallback callback) {
    TaskId id = next_task_id_.fetch_add(1);

    // Create completion callback that updates our future
    auto enhanced_callback = [this, callback](TaskId task_id, bool success) {
        completeFuture(task_id, success);
        if (callback) {
            callback(task_id, success);
        }
    };

    auto task = std::make_unique<Task>(id, std::move(func), std::move(enhanced_callback));

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        tasks_[id] = std::move(task);
    }

    return id;
}

void AsyncRuntime::scheduleTask(TaskId task_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = tasks_.find(task_id);
    if (it != tasks_.end() && it->second->getState() == TaskState::PENDING) {
        ready_queue_.push(task_id);
        task_available_.notify_one();
    }
}

void AsyncRuntime::suspendTask(TaskId task_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        it->second->suspend();
        suspended_queue_.push(task_id);
    }
}

void AsyncRuntime::resumeTask(TaskId task_id) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = tasks_.find(task_id);
    if (it != tasks_.end() && it->second->getState() == TaskState::SUSPENDED) {
        it->second->resume();
        ready_queue_.push(task_id);
        task_available_.notify_one();
    }
}

std::shared_ptr<Future> AsyncRuntime::createFuture(TaskId task_id) {
    auto future = std::make_shared<Future>(task_id);

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        futures_[task_id] = future;
    }

    return future;
}

void AsyncRuntime::completeFuture(TaskId task_id, bool success) {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    auto it = futures_.find(task_id);
    if (it != futures_.end()) {
        // Notify the future that the task completed
        it->second->ready_.store(true);
        it->second->ready_condition_.notify_all();
        completed_task_count_.fetch_add(1);
    } else {
        // Even if no futures are waiting, count the completion
        completed_task_count_.fetch_add(1);
    }
}

void AsyncRuntime::start() {
    if (running_.load()) {
        return; // Already running
    }

    running_.store(true);
    shutdown_requested_.store(false);

    // Start worker threads (using hardware concurrency as default)
    size_t num_threads = std::max(1u, std::thread::hardware_concurrency());
    worker_threads_.reserve(num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        worker_threads_.emplace_back(&AsyncRuntime::workerLoop, this);
    }

    std::cout << "AsyncRuntime started with " << num_threads << " worker threads" << std::endl;
}

void AsyncRuntime::stop() {
    if (!running_.load()) {
        return; // Not running
    }

    shutdown_requested_.store(true);
    running_.store(false);

    // Wake up all worker threads
    task_available_.notify_all();

    // Wait for all worker threads to finish
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    worker_threads_.clear();
    std::cout << "AsyncRuntime stopped" << std::endl;
}

void AsyncRuntime::runUntilComplete() {
    // Wait until all tasks have been completed
    size_t initial_pending = getPendingTaskCount();
    if (initial_pending == 0) {
        return; // No tasks to wait for
    }

    while (getPendingTaskCount() > 0 && !shutdown_requested_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Give a little extra time for the last tasks to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

size_t AsyncRuntime::getPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return ready_queue_.size() + suspended_queue_.size();
}

size_t AsyncRuntime::getCompletedTaskCount() const {
    return completed_task_count_.load();
}

void AsyncRuntime::workerLoop() {
    while (running_.load()) {
        Task* task = getNextTask();

        if (task) {
            executeTask(task);
        } else {
            // No tasks available, wait for notification
            std::unique_lock<std::mutex> lock(queue_mutex_);
            task_available_.wait(lock, [this] {
                return !ready_queue_.empty() || shutdown_requested_.load();
            });
        }
    }
}

Task* AsyncRuntime::getNextTask() {
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (ready_queue_.empty()) {
        return nullptr;
    }

    TaskId task_id = ready_queue_.front();
    ready_queue_.pop();

    auto it = tasks_.find(task_id);
    if (it != tasks_.end()) {
        return it->second.get();
    }

    return nullptr;
}

void AsyncRuntime::executeTask(Task* task) {
    if (task) {
        task->execute();
    }
}

// Global runtime access functions
void initializeAsyncRuntime() {
    AsyncRuntime::getInstance().start();
}

void shutdownAsyncRuntime() {
    AsyncRuntime::getInstance().stop();
}

AsyncRuntime& getAsyncRuntime() {
    return AsyncRuntime::getInstance();
}

// Utility functions for integration with Vyb language
TaskId scheduleAsyncFunction(TaskFunction func) {
    auto& runtime = AsyncRuntime::getInstance();
    TaskId id = runtime.createTask(std::move(func));
    runtime.scheduleTask(id);
    return id;
}

std::shared_ptr<Future> awaitTask(TaskId task_id) {
    auto& runtime = AsyncRuntime::getInstance();
    return runtime.createFuture(task_id);
}

} // namespace runtime
} // namespace vyb