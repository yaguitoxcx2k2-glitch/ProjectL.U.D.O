#pragma once
#include <chrono>
#include <cstdint>

namespace ludo::core {

using MonotonicTime = std::chrono::steady_clock::time_point;
using Duration = std::chrono::nanoseconds;

class IClock {
public:
    virtual ~IClock() = default;
    virtual MonotonicTime now() const noexcept = 0;
};

class SteadyClock final : public IClock {
public:
    MonotonicTime now() const noexcept override { return std::chrono::steady_clock::now(); }
};

class ManualClock final : public IClock {
public:
    MonotonicTime now() const noexcept override { return current_; }
    void advance(Duration amount) noexcept { current_ += amount; }
    void set(MonotonicTime time) noexcept { current_ = time; }
private:
    MonotonicTime current_{};
};

} // namespace ludo::core
