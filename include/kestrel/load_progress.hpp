#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>

namespace kestrel
{
    enum class LoadPhase : uint8_t
    {
        Opening,
        IndexingLines,
        IndexingTimestamps,
        Complete,
        Cancelled,
        Failed
    };

    struct LoadProgress
    {
        std::atomic<LoadPhase> phase{LoadPhase::Opening};
        std::atomic<std::size_t> completed{0};
        std::atomic<std::size_t> total{0};
    };

    using ProgressCallback = std::function<bool(std::size_t, std::size_t)>;

    class LoadCancelled : public std::runtime_error
    {
    public:
        LoadCancelled() : std::runtime_error("loading cancelled") {}
    };
} // namespace kestrel
