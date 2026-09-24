#include "tracker.hpp"

#include <chrono>
#include <format>
#include <optional>
#include <string>

#include "platform.hpp"

namespace balder {
namespace {

using namespace std::chrono_literals;
using platform::Clock;

constexpr Clock::duration CYCLE = 5min;
constexpr std::size_t SEND_THRESHOLD = 6;

template <Service S>
void initialize(S& service) {
    if (const auto result = service.initialize(); !result) {
        platform::log("{}: initialization failed ({})", S::NAME, to_string(result.error()));
    }
}

/// Takes one reading, or logs why there is none.
template <Service S>
std::optional<typename S::Reading> sample(S& service) {
    const auto reading = service.read();
    if (!reading) {
        platform::log("{}: {}", S::NAME, to_string(reading.error()));
        return std::nullopt;
    }
    return *reading;
}

/// One field of a reading with fixed decimals, or null if there is no reading.
template <typename Reading, typename Field>
std::string json_number(const std::optional<Reading>& reading, Field Reading::*field,
                        int decimals) {
    return reading ? std::format("{:.{}f}", (*reading).*field, decimals) : "null";
}

/// A reading as it appears in the cycle log, or "--" when there is none.
std::string log_field(const std::optional<Environment>& environment) {
    return environment ? std::format("{:6.2f} C {:5.1f} %RH", environment->temperature_celsius,
                                     environment->humidity_percent)
                       : "--";
}

std::string log_field(const std::optional<Position>& position) {
    return position ? std::format("{:9.5f}, {:9.5f}", position->latitude, position->longitude)
                    : "--";
}

/// The record as the JSON object the cloud expects. The key names are the
/// cloud's wire format, so they stay as they are.
std::string encode(const Record& record) {
    return std::format(R"({{"t":{},"temp":{},"rh":{},"lat":{},"lon":{}}})",
                       record.time.time_since_epoch().count(),
                       json_number(record.environment, &Environment::temperature_celsius, 2),
                       json_number(record.environment, &Environment::humidity_percent, 2),
                       json_number(record.position, &Position::latitude, 5),
                       json_number(record.position, &Position::longitude, 5));
}

} // namespace

Tracker::Tracker() {
    initialize(environment_);
    initialize(gnss_);
}

Record Tracker::run_cycle() {
    const auto wake = Clock::now();
    ++statistics_.cycles;

    sample(environment_);
    const Record record{.time = wake, .environment = {}, .position = sample(gnss_)};
    if (!buffer_.push(record)) {
        platform::log("buffer full, oldest record lost");
        ++statistics_.lost;
    }
    log_cycle(record);
    if (buffer_.size() >= SEND_THRESHOLD) {
        send_buffered_records();
    }

    const auto next_wake = wake + CYCLE;
    if (Clock::now() < next_wake) {
        platform::sleep_for(next_wake - Clock::now());
    }
    return record;
}

/// Sends buffered records to the cloud, oldest first. A record leaves the
/// buffer only once the cloud has acknowledged it, and sending stops at the
/// first failure, so the rest wait, still in order, for the next try.
void Tracker::send_buffered_records() {
    while (!buffer_.empty()) {
        if (!platform::modem_send(encode(buffer_.front()))) {
            platform::log("send failed, {} records waiting", buffer_.size());
            return;
        }
        buffer_.pop();
        ++statistics_.sent;
    }
}

/// One line per cycle. "--" marks a missing reading.
void Tracker::log_cycle(const Record& record) const {
    platform::log("#{:<5} {:<18}  {:<20}  buffered {}", statistics_.cycles,
                  log_field(record.environment), log_field(record.position), buffer_.size());
}

} // namespace balder
