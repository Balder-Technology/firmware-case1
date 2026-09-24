// Acceptance tests. Each one checks a rule from the spec in README.md.
// Each test runs in its own process, and a test that hangs counts as a failure.

#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <optional>
#include <source_location>
#include <string.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

#include "environment_service.hpp"
#include "platform.hpp"
#include "tracker.hpp"

namespace {

using namespace std::chrono_literals;
using platform::Clock;

/// Writes one formatted line to stream: the C++20 counterpart of std::println.
template <typename... Arguments>
void print_line(std::FILE* stream, std::format_string<Arguments...> pattern,
                Arguments&&... arguments) {
    std::fputs(std::format(pattern, std::forward<Arguments>(arguments)...).c_str(), stream);
    std::fputc('\n', stream);
}

/// Fails the running test if ok is false, pointing at the caller.
void check(bool ok, std::string_view what,
           std::source_location where = std::source_location::current()) {
    if (!ok) {
        print_line(stderr, "         {}:{}: {}", where.file_name(), where.line(), what);
        std::exit(1);
    }
}

/// Every cycle has a temperature/humidity reading and a position.
void reads_both_sensors_every_cycle() {
    balder::Tracker tracker;
    for (uint32_t cycle = 1; cycle <= 12; ++cycle) {
        const auto record = tracker.run_cycle();
        check(record.environment.has_value(),
              std::format("cycle {} has no temperature/humidity reading", cycle));
        check(record.position.has_value(), std::format("cycle {} has no position", cycle));
    }
}

/// Readings stay within the sensor's range: -40..+85 degC, 0..100 %RH.
void readings_within_sensor_range() {
    balder::EnvironmentService environment;
    check(environment.initialize().has_value(), "sensor did not initialise");
    for (uint32_t i = 0; i < 48; ++i) { // a day, every 30 minutes
        const auto reading = environment.read();
        check(reading.has_value(), "no reading");
        check(reading->temperature_celsius >= -40 && reading->temperature_celsius <= 85,
              std::format("temperature {:.2f} C", reading->temperature_celsius));
        check(reading->humidity_percent >= 0 && reading->humidity_percent <= 100,
              std::format("humidity {:.1f} %RH", reading->humidity_percent));
        platform::sleep_for(30min);
    }
}

/// The tracker wakes every 5 minutes, however long it has been running.
void wakes_every_5_minutes_after_months() {
    balder::Tracker tracker;
    platform::sleep_for(Clock::duration{4'294'000'000u}); // skip ahead to ~day 49
    auto previous = tracker.run_cycle().time;
    for (uint32_t i = 0; i < 12; ++i) {
        const auto wake = tracker.run_cycle().time;
        const auto gap = std::chrono::duration_cast<std::chrono::seconds>(wake - previous);
        check(gap == 5min, std::format("woke {} after the previous cycle, expected {}", gap, 5min));
        previous = wake;
    }
}

/// The "t" field of a cloud payload.
std::optional<Clock::rep> timestamp_of(std::string_view payload) {
    constexpr std::string_view prefix = R"({"t":)";
    if (!payload.starts_with(prefix)) {
        return std::nullopt;
    }
    payload.remove_prefix(prefix.size());
    Clock::rep timestamp{};
    const auto result = std::from_chars(payload.data(), payload.data() + payload.size(), timestamp);
    return result.ec == std::errc{} ? std::optional{timestamp} : std::nullopt;
}

/// Every record reaches the cloud exactly once, in order.
void every_record_reaches_cloud_once_in_order() {
    balder::Tracker tracker;
    std::vector<Clock::rep> sampled;
    for (uint32_t i = 0; i < 100; ++i) {
        sampled.push_back(tracker.run_cycle().time.time_since_epoch().count());
    }
    const auto& cloud = platform::cloud_received();
    // The last few records may still be waiting for the next send.
    check(cloud.size() <= sampled.size() && cloud.size() + 10 >= sampled.size(),
          std::format("{} records sampled, {} reached the cloud", sampled.size(), cloud.size()));
    for (std::size_t i = 0; i < cloud.size(); ++i) {
        const auto timestamp = timestamp_of(cloud[i]);
        check(timestamp == sampled[i], std::format("cloud record {} has t={}, expected t={}", i,
                                                   timestamp.value_or(0), sampled[i]));
    }
}

struct Test {
    std::string_view name;
    void (*run)();
};

constexpr std::array TESTS{
    Test{"reads_both_sensors_every_cycle", reads_both_sensors_every_cycle},
    Test{"readings_within_sensor_range", readings_within_sensor_range},
    Test{"wakes_every_5_minutes_after_months", wakes_every_5_minutes_after_months},
    Test{"every_record_reaches_cloud_once_in_order", every_record_reaches_cloud_once_in_order},
};

enum class Outcome { passed, failed, hung, crashed };

struct TestResult {
    Outcome outcome{Outcome::passed};
    int signal_number{0}; ///< the signal that ended a crashed test
};

/// Runs a test in a child process, so a crash or hang fails only that test.
TestResult run_isolated(const Test& test) {
    std::fflush(stdout);
    if (fork() == 0) {
        // Hide the tracker's own log. A failed freopen closes stdout, so the
        // test could not be trusted to run as written: fail it instead.
        if (std::freopen("/dev/null", "w", stdout) == nullptr) {
            print_line(stderr, "         could not redirect stdout to /dev/null");
            std::exit(1);
        }
        alarm(5);
        test.run();
        std::exit(0);
    }
    int status = 0;
    wait(&status);
    if (WIFSIGNALED(status)) {
        const int signal_number = WTERMSIG(status);
        if (signal_number == SIGALRM) {
            return {.outcome = Outcome::hung};
        }
        return {.outcome = Outcome::crashed, .signal_number = signal_number};
    }
    return {.outcome = WEXITSTATUS(status) == 0 ? Outcome::passed : Outcome::failed};
}

std::string suffix(const TestResult& result) {
    switch (result.outcome) {
    case Outcome::hung: return " (hung: still running after 5 s)";
    case Outcome::crashed: return std::format(" (crashed: {})", strsignal(result.signal_number));
    default: return "";
    }
}

} // namespace

int main() {
    uint32_t failed = 0;
    for (const Test& test : TESTS) {
        print_line(stdout, "[ RUN  ] {}", test.name);
        const auto result = run_isolated(test);
        const auto verdict = result.outcome == Outcome::passed ? "PASS" : "FAIL";
        print_line(stdout, "[ {} ] {}{}", verdict, test.name, suffix(result));
        failed += result.outcome == Outcome::passed ? 0 : 1;
    }
    print_line(stdout, "{} of {} failed", failed, TESTS.size());
    return failed == 0 ? 0 : 1;
}
