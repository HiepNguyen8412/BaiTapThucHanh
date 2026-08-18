#pragma once

#include "Events/Event.h"

#include <cstddef>
#include <deque>

class EventQueue
{
public:
    explicit EventQueue(std::size_t maxSize = 256);

    bool Push(const ServiceEvent& event);
    void PushReplay(const ServiceEvent& event);
    bool TryPop(ServiceEvent& event);
    bool RemoveOldestVerbose();
    void Clear();
    std::size_t Size() const noexcept { return queue_.size(); }
    std::size_t MaxSize() const noexcept { return maxSize_; }
    bool Empty() const noexcept { return queue_.empty(); }

private:
    std::size_t maxSize_;
    std::deque<ServiceEvent> queue_;
};
