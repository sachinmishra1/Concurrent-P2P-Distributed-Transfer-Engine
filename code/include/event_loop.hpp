#pragma once

#include <functional>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <cstdint>

class EventLoop {
public:
    using EventCallback = std::function<void(int fd, uint32_t events)>;
    using TimerCallback = std::function<void()>;

    EventLoop();
    ~EventLoop();

    // Prevent copy/move
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    EventLoop(EventLoop&&) = delete;
    EventLoop& operator=(EventLoop&&) = delete;

    // Register interest in fd events
    bool register_fd(int fd, uint32_t events, EventCallback callback);

    // Modify monitored events on fd
    bool modify_fd(int fd, uint32_t events);

    // Remove interest in fd
    bool unregister_fd(int fd);

    // Register a timer callback to execute after a delay
    uint64_t register_timer(std::chrono::milliseconds delay, TimerCallback callback);

    // Cancel a scheduled timer
    void cancel_timer(uint64_t timer_id);

    // Start polling loop
    void run();

    // Terminate polling loop
    void shutdown();

private:
    struct Timer {
        uint64_t id;
        std::chrono::steady_clock::time_point deadline;
        TimerCallback callback;

        bool operator>(const Timer& other) const {
            return deadline > other.deadline;
        }
    };

    int epoll_fd_ = -1;
    bool running_ = false;
    uint64_t next_timer_id_ = 1;

    std::unordered_map<int, EventCallback> fd_callbacks_;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timers_;
    std::unordered_set<uint64_t> cancelled_timers_;
};
