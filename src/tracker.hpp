#pragma once

#include <cstddef>
#include <cstdint>

#include "environment_service.hpp"
#include "gnss_service.hpp"
#include "ring_buffer.hpp"
#include "service.hpp"

namespace balder {

/// Counts since boot, reported in the final log line.
struct Statistics {
    uint32_t cycles{0};
    uint32_t sent{0};
    uint32_t lost{0};
};

/// The application. Owns the services and the buffer of unsent records.
class Tracker {
public:
    /// Initialises every service.
    Tracker();

    /// One wake cycle: sample each service into a Record, buffer it, send the
    /// buffer every few cycles, sleep until the next cycle. Returns the Record.
    Record run_cycle();

    [[nodiscard]] const Statistics& statistics() const { return statistics_; }
    [[nodiscard]] std::size_t buffered() const { return buffer_.size(); }

private:
    /// Records held while the cloud is unreachable.
    static constexpr std::size_t BUFFER_CAPACITY = 32;

    void send_buffered_records();
    void log_cycle(const Record& record) const;

    EnvironmentService environment_;
    GnssService gnss_;
    RingBuffer<Record, BUFFER_CAPACITY> buffer_;
    Statistics statistics_;
};

} // namespace balder
