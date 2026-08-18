#include "Events/EventHistory.h"

EventHistory::EventHistory(std::size_t maxSize)
    : maxSize_(maxSize == 0 ? 1 : maxSize)
{
}

// Luu event theo sequence va chi giu toi da maxSize_ phan tu gan nhat.
void EventHistory::Add(const ServiceEvent& event)
{
    events_.push_back(event);
    while (events_.size() > maxSize_) events_.pop_front();
}

// Lay cac event sau lastEventSeq de replay. Neu khoang sequence da bi mat thi tra false.
bool EventHistory::GetAfter(std::uint64_t lastEventSeq, std::vector<ServiceEvent>& events) const
{
    events.clear();
    if (!events_.empty())
    {
        if (lastEventSeq > events_.back().sequence)
        {
            return false; // The client claims an event that this session never produced.
        }
        if (lastEventSeq < events_.front().sequence &&
            events_.front().sequence - lastEventSeq > 1)
        {
            return false; // The client is older than retained history.
        }
    }
    for (const auto& event : events_)
    {
        if (event.sequence > lastEventSeq) events.push_back(event);
    }
    return true;
}

std::uint64_t EventHistory::OldestSequence() const noexcept
{
    return events_.empty() ? 0 : events_.front().sequence;
}

std::uint64_t EventHistory::NewestSequence() const noexcept
{
    return events_.empty() ? 0 : events_.back().sequence;
}

void EventHistory::Clear()
{
    events_.clear();
}
