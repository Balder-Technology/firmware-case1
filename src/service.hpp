#pragma once

#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>

#include "expected.hpp"
#include "platform.hpp"

namespace balder {

/// Why a service has no reading this cycle.
enum class Error : uint8_t {
    no_ack,       ///< the peripheral did not answer on the bus
    unknown_chip, ///< CHIP_ID did not match the expected part
    timeout,      ///< a measurement did not finish in time
    no_fix,       ///< the GNSS found no position in time
};

[[nodiscard]] constexpr std::string_view to_string(Error error) {
    switch (error) {
    case Error::no_ack: return "no ack";
    case Error::unknown_chip: return "unknown chip";
    case Error::timeout: return "timeout";
    case Error::no_fix: return "no fix";
    }
    return "unknown error";
}

struct Environment {
    float temperature_celsius{};
    float humidity_percent{};
};

struct Position {
    double latitude{};
    double longitude{};
};

/// Everything sampled in one wake cycle. A field is empty if its service had
/// no reading.
struct Record {
    platform::Clock::time_point time{};
    std::optional<Environment> environment{};
    std::optional<Position> position{};
};

/// A service owns one peripheral and produces one Reading per cycle.
///
/// Tracker default-constructs its services and calls initialize() on each, so the
/// concept requires default construction too.
template <typename S>
concept Service = std::default_initializable<S> && requires(S& service) {
    typename S::Reading;
    { S::NAME } -> std::convertible_to<std::string_view>;
    { service.initialize() } -> std::same_as<Expected<void, Error>>;
    { service.read() } -> std::same_as<Expected<typename S::Reading, Error>>;
};

} // namespace balder
