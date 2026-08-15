// SPDX-License-Identifier: Apache-2.0

// Test async runtime scheduler functionality

#include "vyb/runtime/async_runtime.hpp"
#include <iostream>
#include <chrono>
#include <thread>

using namespace vyb::runtime;

int main() {
    std::cout << "Testing Async Runtime Scheduler" << std::endl;

    // Initialize the async runtime
    initializeAsyncRuntime();

    // Test 1: Simple task execution
    std::cout << "\n=== Test 1: Simple Task Execution ===" << std::endl;

    bool task1_completed = false;
    bool task2_completed = false;

    auto task1_func = [&task1_completed]() {
        std::cout << "Task 1 executing..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        task1_completed = true;
        std::cout << "Task 1 completed!" << std::endl;
    };

    auto task2_func = [&task2_completed]() {
        std::cout << "Task 2 executing..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        task2_completed = true;
        std::cout << "Task 2 completed!" << std::endl;
    };

    // Schedule tasks
    TaskId id1 = scheduleAsyncFunction(task1_func);
    TaskId id2 = scheduleAsyncFunction(task2_func);

    std::cout << "Scheduled tasks with IDs: " << id1 << ", " << id2 << std::endl;

    // Wait for tasks to complete
    auto& runtime = getAsyncRuntime();
    runtime.runUntilComplete();

    // Verify completion
    if (task1_completed && task2_completed) {
        std::cout << "✓ Both tasks completed successfully" << std::endl;
    } else {
        std::cout << "✗ Tasks did not complete" << std::endl;
    }

    // Test 2: Future creation and management
    std::cout << "\n=== Test 2: Future Management ===" << std::endl;

    auto task3_func = []() {
        std::cout << "Task 3 executing..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(75));
        std::cout << "Task 3 completed!" << std::endl;
    };

    TaskId id3 = scheduleAsyncFunction(task3_func);
    auto future3 = awaitTask(id3);

    std::cout << "Created future for task " << id3 << std::endl;
    std::cout << "Future ready before wait: " << future3->isReady() << std::endl;

    // Wait for the future
    future3->wait();
    std::cout << "Future ready after wait: " << future3->isReady() << std::endl;

    // Test 3: Runtime statistics
    std::cout << "\n=== Test 3: Runtime Statistics ===" << std::endl;
    std::cout << "Completed task count: " << runtime.getCompletedTaskCount() << std::endl;
    std::cout << "Pending task count: " << runtime.getPendingTaskCount() << std::endl;

    // Cleanup
    shutdownAsyncRuntime();
    std::cout << "\nAsync runtime test completed successfully!" << std::endl;

    return 0;
}