#include "event_loop.hpp"
#include <sys/epoll.h>
#include <unistd.h>
#include <stdexcept>
#include <errno.h>

EventLoop::EventLoop() {
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw std::runtime_error("EventLoop: failed to create epoll instance");
    }
}

EventLoop::~EventLoop() {
    if (epoll_fd_ != -1) {
        ::close(epoll_fd_);
    }
}

bool EventLoop::register_fd(int fd, uint32_t events, EventCallback callback) {
    if (fd < 0 || !callback) return false;

    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        return false;
    }

    fd_callbacks_[fd] = std::move(callback);
    return true;
}

bool EventLoop::modify_fd(int fd, uint32_t events) {
    if (fd < 0 || fd_callbacks_.find(fd) == fd_callbacks_.end()) return false;

    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) >= 0;
}

bool EventLoop::unregister_fd(int fd) {
    if (fd < 0) return false;

    auto it = fd_callbacks_.find(fd);
    if (it == fd_callbacks_.end()) return false;

    // Ignore failure since the fd might already be closed (which auto-removes it from epoll)
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    fd_callbacks_.erase(it);
    return true;
}

uint64_t EventLoop::register_timer(std::chrono::milliseconds delay, TimerCallback callback) {
    if (!callback) return 0;

    uint64_t id = next_timer_id_++;
    auto deadline = std::chrono::steady_clock::now() + delay;
    timers_.push(Timer{
        .id = id,
        .deadline = deadline,
        .callback = std::move(callback)
    });

    return id;
}

void EventLoop::cancel_timer(uint64_t timer_id) {
    if (timer_id > 0) {
        cancelled_timers_.insert(timer_id);
    }
}

void EventLoop::run() {
    running_ = true;
    constexpr int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];

    while (running_) {
        auto now = std::chrono::steady_clock::now();
        int timeout_ms = -1;

        // Process expired timers and prune cancelled/expired timers
        while (!timers_.empty() && running_) {
            // Check if top timer is cancelled
            if (cancelled_timers_.find(timers_.top().id) != cancelled_timers_.end()) {
                cancelled_timers_.erase(timers_.top().id);
                timers_.pop();
                continue;
            }

            auto next_timer = timers_.top();
            if (next_timer.deadline <= now) {
                // Pop it before calling the callback so that any registration inside the callback is safe
                timers_.pop();
                next_timer.callback();
                // Update now since callback execution could take time
                now = std::chrono::steady_clock::now();
            } else {
                auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(next_timer.deadline - now);
                timeout_ms = static_cast<int>(delay.count());
                break;
            }
        }

        if (!running_) break;

        // Poll for events
        int nfds = ::epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t epoll_evs = events[i].events;

            auto it = fd_callbacks_.find(fd);
            if (it != fd_callbacks_.end()) {
                // Copy/invoke callback safely
                auto callback = it->second;
                callback(fd, epoll_evs);
            }
        }
    }
}

void EventLoop::shutdown() {
    running_ = false;
}
