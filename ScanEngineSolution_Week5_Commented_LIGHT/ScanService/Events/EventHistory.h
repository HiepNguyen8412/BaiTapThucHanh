#pragma once

#include "Events/Event.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

class EventHistory
{
public:
    explicit EventHistory(std::size_t maxSize = 1024);
    void Add(const ServiceEvent& event);
    bool GetAfter(std::uint64_t lastEventSeq, std::vector<ServiceEvent>& events) const;
    std::uint64_t OldestSequence() const noexcept;
    std::uint64_t NewestSequence() const noexcept;
    void Clear();

private:
    std::size_t maxSize_;
    std::deque<ServiceEvent> events_;
};
