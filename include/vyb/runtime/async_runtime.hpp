// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <queue>
#include <vector>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <thread>

namespace vyb {
namespace runtime {

// Forward declarations
class Task;
class Future;
class AsyncRuntime;

// Task state enumeration
enum class TaskState {
    PENDING,    // Task created but not started
    RUNNING,    // Task currently executing
    SUSPENDED,  // Task suspended on await
    COMPLETED,  // Task finished successfully
    FAILED      // Task failed with error
};

// Unique identifier for tasks
using TaskId = std::uint64_t;

// Task execution function type
using TaskFunction = std::function<void()>;

// Task completion callback type
using CompletionCallback = std::function<void(TaskId, bool success)>;

/**
 * Represents an asynchronous task that can be executed by the runtime
 */
class Task {
public:
    Task(TaskId id, TaskFunction func, CompletionCallback callback = nullptr);
    ~Task() = default;

    // Non-copyable but movable
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    Task(Task&&) = default;
    Task& operator=(Task&&) = default;

    TaskId getId() const { return id_; }
    TaskState getState() const { return state_; }

    void execute();
    void suspend();
    void resume();
    void complete(bool success = true);

private:
    TaskId id_;
    TaskState state_;
    TaskFunction function_;
    CompletionCallback completion_callback_;
    std::mutex state_mutex_;
};

/**
 * Represents a future value that may not be available yet
 */
class Future {
public:
    Future(TaskId task_id);
    ~Future() = default;

    // Non-copyable but movable
    Future(const Future&) = delete;
    Future& operator=(const Future&) = delete;
    Future(Future&&) = default;
    Future& operator=(Future&&) = default;

    bool isReady() const;
    void wait();

    // Generic value accessors - will be specialized for actual types
    template<typename T>
    T getValue() const;

    template<typename T>
    void setValue(T value);

    // Make AsyncRuntime a friend so it can access private members
    friend class AsyncRuntime;

private:
    TaskId task_id_;
    std::atomic<bool> ready_;
    mutable std::mutex value_mutex_;
    mutable std::condition_variable ready_condition_;
    // Note: Actual value storage will be added when we implement type system integration
};

/**
 * Core async runtime scheduler
 */
class AsyncRuntime {
public:
    static AsyncRuntime& getInstance();

    ~AsyncRuntime();

    // Non-copyable and non-movable (singleton)
    AsyncRuntime(const AsyncRuntime&) = delete;
    AsyncRuntime& operator=(const AsyncRuntime&) = delete;
    AsyncRuntime(AsyncRuntime&&) = delete;
    AsyncRuntime& operator=(AsyncRuntime&&) = delete;

    // Task management
    TaskId createTask(TaskFunction func, CompletionCallback callback = nullptr);
    void scheduleTask(TaskId task_id);
    void suspendTask(TaskId task_id);
    void resumeTask(TaskId task_id);

    // Future management
    std::shared_ptr<Future> createFuture(TaskId task_id);
    void completeFuture(TaskId task_id, bool success = true);

    // Runtime control
    void start();
    void stop();
    void runUntilComplete();
    bool isRunning() const { return running_; }

    // Statistics
    size_t getPendingTaskCount() const;
    size_t getCompletedTaskCount() const;

private:
    AsyncRuntime();

    void workerLoop();
    Task* getNextTask();
    void executeTask(Task* task);

    // Task storage and queues
    std::unordered_map<TaskId, std::unique_ptr<Task>> tasks_;
    std::queue<TaskId> ready_queue_;
    std::queue<TaskId> suspended_queue_;

    // Future storage
    std::unordered_map<TaskId, std::shared_ptr<Future>> futures_;

    // Threading and synchronization
    std::vector<std::thread> worker_threads_;
    mutable std::mutex queue_mutex_;
    std::condition_variable task_available_;
    std::atomic<bool> running_;
    std::atomic<bool> shutdown_requested_;

    // Task ID generation
    std::atomic<TaskId> next_task_id_;

    // Statistics
    std::atomic<size_t> completed_task_count_;
};

// Global runtime access functions
void initializeAsyncRuntime();
void shutdownAsyncRuntime();
AsyncRuntime& getAsyncRuntime();

// Utility functions for integration with Vyb language
TaskId scheduleAsyncFunction(TaskFunction func);
std::shared_ptr<Future> awaitTask(TaskId task_id);

} // namespace runtime
} // namespace vyb