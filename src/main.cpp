// Battery-powered tracker. Wakes every 5 minutes, samples its sensors and sends
// the readings to the cloud. See tracker.cpp for one wake cycle.
//
// Usage: tracker [cycles]

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string_view>
#include <system_error>

#include "platform.hpp"
#include "tracker.hpp"

int main(int argc, char** argv) {
    const std::span arguments{argv, static_cast<std::size_t>(argc)};
    uint32_t cycles = 48;
    if (arguments.size() > 1) {
        const std::string_view argument{arguments[1]};
        if (std::from_chars(argument.data(), argument.data() + argument.size(), cycles).ec !=
            std::errc{}) {
            std::fputs("usage: tracker [cycles]\n", stderr);
            return 1;
        }
    }

    balder::Tracker tracker;
    for (uint32_t i = 0; i < cycles; ++i) {
        tracker.run_cycle();
    }

    const auto& statistics = tracker.statistics();
    platform::log("done: {} cycles, {} sent, {} lost, {} still buffered", statistics.cycles,
                  statistics.sent, statistics.lost, tracker.buffered());
    return 0;
}
