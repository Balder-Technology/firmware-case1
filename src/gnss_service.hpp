#pragma once

#include <chrono>
#include <string_view>

#include "expected.hpp"
#include "platform.hpp"
#include "scope_exit.hpp"
#include "service.hpp"

namespace balder {

/// Powers the GNSS receiver, waits for a fix, then powers it off to save
/// battery.
class GnssService {
public:
    using Reading = Position;
    static constexpr std::string_view NAME = "gnss";

    [[nodiscard]] Expected<void, Error> initialize() { return {}; }

    [[nodiscard]] Expected<Position, Error> read() {
        platform::gnss_power(true);
        const ScopeExit power_off{[] { platform::gnss_power(false); }};

        const auto start = platform::Clock::now();
        while (platform::Clock::now() - start < FIX_TIMEOUT) {
            platform::sleep_for(POLL_INTERVAL);
            if (const auto fix = platform::gnss_read_fix()) {
                return Position{.latitude = fix->latitude, .longitude = fix->longitude};
            }
        }
        return Unexpected{Error::no_fix};
    }

private:
    static constexpr std::chrono::milliseconds POLL_INTERVAL{500};
    static constexpr std::chrono::seconds FIX_TIMEOUT{60};
};

static_assert(Service<GnssService>);

} // namespace balder
