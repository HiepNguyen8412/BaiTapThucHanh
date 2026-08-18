#include "Events/EventQueue.h"

EventQueue::EventQueue(std::size_t maxSize)
    : maxSize_(maxSize == 0 ? 1 : maxSize)
{
}

// Khi queue day, uu tien xoa event verbose cu nhat de giu cac event ket qua/loi quan trong.
bool EventQueue::RemoveOldestVerbose()
{
    for (auto it = queue_.begin(); it != queue_.end(); ++it)
    {
        if (it->importance == EventImportance::Verbose)
        {
            queue_.erase(it);
            return true;
        }
    }
    return false;
}

// Backpressure policy: co gioi han queue; neu day thi thu drop verbose thay vi tang RAM vo han.
bool EventQueue::Push(const ServiceEvent& event)
{
    if (queue_.size() < maxSize_)
    {
        queue_.push_back(event);
        return true;
    }

    if (event.importance == EventImportance::Verbose)
    {
        return false;
    }

    // Critical events are never dropped. Prefer evicting an old verbose event.
    if (!RemoveOldestVerbose())
    {
        // All queued events are critical: temporarily exceed the soft limit.
        queue_.push_back(event);
        return true;
    }
    queue_.push_back(event);
    return true;
}

// Event replay duoc dua vao queue khi RESUME; van ton trong gioi han de bao ve bo nho.
void EventQueue::PushReplay(const ServiceEvent& event)
{
    // RESUME must not silently drop already-generated events.
    queue_.push_back(event);
}

// Lay event dau queue theo FIFO cho RunEventLoop gui ra client.
bool EventQueue::TryPop(ServiceEvent& event)
{
    if (queue_.empty()) return false;
    event = queue_.front();
    queue_.pop_front();
    return true;
}

void EventQueue::Clear()
{
    queue_.clear();
}
