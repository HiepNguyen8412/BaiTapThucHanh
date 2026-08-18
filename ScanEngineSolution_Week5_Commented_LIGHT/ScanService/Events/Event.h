#pragma once

#include "Jobs/Job.h"

#include <cstdint>

enum class ServiceEventKind : std::uint32_t
{
    Job = 0,
    FlowControl = 1
};

enum class EventImportance : std::uint32_t
{
    Verbose = 0,
    Critical = 1
};

// Event noi bo co sequence tang dan. Job event mang JobSnapshot; FlowControl mang thong tin backpressure.
struct ServiceEvent
{
    std::uint64_t sequence{};
    ServiceEventKind kind{ServiceEventKind::Job};
    EventImportance importance{EventImportance::Verbose};
    JobSnapshot snapshot{};
    std::uint64_t droppedVerbose{};
    std::uint32_t queueDepth{};
};
