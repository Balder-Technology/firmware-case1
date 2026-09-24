#include "platform.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <format>
#include <numbers>

namespace platform {
namespace {

using namespace std::chrono_literals;

constexpr uint8_t ENVIRONMENT_SENSOR_ADDRESS = 0x76;
constexpr uint8_t REGISTER_CHIP_ID = 0xD0;
constexpr uint8_t REGISTER_STATUS = 0xF3;
constexpr uint8_t REGISTER_CONTROL = 0xF4;
constexpr uint8_t REGISTER_TEMPERATURE = 0xFA;
constexpr uint8_t REGISTER_HUMIDITY = 0xFC;

constexpr uint8_t ACCELEROMETER_ADDRESS = 0x18;
constexpr uint8_t REGISTER_WHO_AM_I = 0x0F;
constexpr uint8_t REGISTER_ACCELERATION = 0x28;
constexpr uint8_t REGISTER_MOTION = 0x31;

/// The ship carrying the unit leaves Bergen at boot and docks in Reykjavik
/// after this many hours, where it stays.
constexpr double VOYAGE_HOURS = 72.0;

/// Simulated time. The chip only sees the low 32 bits, through Clock::now().
std::chrono::duration<uint64_t, std::milli> simulated_time{0};

/// Sensor state. temperature and humidity mirror the TEMPERATURE and HUMIDITY
/// registers exactly, so temperature stays signed and humidity unsigned.
struct EnvironmentSensorSimulation {
    bool measuring{false};
    Clock::time_point started{};
    int16_t temperature{0};
    uint16_t humidity{0};
};
EnvironmentSensorSimulation environment_sensor;

struct GnssSimulation {
    bool on{false};
    bool warm{false};
    Clock::time_point powered_at{};
};
GnssSimulation gnss;

/// When the accelerometer's MOTION register was last read, which clears it.
std::chrono::duration<uint64_t, std::milli> motion_cleared_at{0};

std::vector<std::string> cloud;

double hours(std::chrono::duration<uint64_t, std::milli> time) {
    return std::chrono::duration<double, std::ratio<3600>>(time).count();
}

double simulated_hours() {
    return hours(simulated_time);
}

/// Latches a result once a measurement has run for 10 ms. The environment
/// follows a daily cycle: -10..+6 degC, humidity moving opposite.
void update_environment_sensor() {
    if (!environment_sensor.measuring || Clock::now() - environment_sensor.started < 10ms) {
        return;
    }
    const double daily_cycle = std::sin(2 * std::numbers::pi * simulated_hours() / 24);
    environment_sensor.temperature =
        static_cast<int16_t>(std::lround((-2.0 + 8.0 * daily_cycle) * 100));
    environment_sensor.humidity =
        static_cast<uint16_t>(std::lround((75.0 - 15.0 * daily_cycle) * 100));
    environment_sensor.measuring = false;
}

bool read_environment_sensor(uint8_t register_address, std::span<uint8_t> out) {
    update_environment_sensor();
    const auto temperature_bits = std::bit_cast<uint16_t>(environment_sensor.temperature);
    for (std::size_t i = 0; i < out.size(); ++i) {
        switch (register_address + i) {
        case REGISTER_CHIP_ID: out[i] = 0x60; break;
        case REGISTER_STATUS: out[i] = environment_sensor.measuring ? 0x01 : 0x00; break;
        case REGISTER_TEMPERATURE: out[i] = static_cast<uint8_t>(temperature_bits >> 8); break;
        case REGISTER_TEMPERATURE + 1: out[i] = static_cast<uint8_t>(temperature_bits); break;
        case REGISTER_HUMIDITY:
            out[i] = static_cast<uint8_t>(environment_sensor.humidity >> 8);
            break;
        case REGISTER_HUMIDITY + 1:
            out[i] = static_cast<uint8_t>(environment_sensor.humidity);
            break;
        default: return false;
        }
    }
    return true;
}

/// X, Y and Z in mg.
struct Acceleration {
    int16_t x{};
    int16_t y{};
    int16_t z{};
};

/// A wave of the given amplitude and period, at the current time.
double wave(double amplitude, double period_seconds) {
    const double seconds = std::chrono::duration<double>(simulated_time).count();
    return amplitude * std::sin(2 * std::numbers::pi * seconds / period_seconds);
}

int16_t milli_g(double value) {
    return static_cast<int16_t>(std::lround(value));
}

/// At sea the ship rolls, pitches and heaves; in port only gravity is left.
Acceleration current_acceleration() {
    if (simulated_hours() >= VOYAGE_HOURS) {
        return {.x = 0, .y = 0, .z = 1000};
    }
    return {.x = milli_g(wave(200, 7)),
            .y = milli_g(wave(120, 11)),
            .z = milli_g(1000 + wave(60, 5))};
}

/// Whether the unit was at sea at any time since MOTION was last read. Reading
/// MOTION clears it, like the latched interrupt status of a real accelerometer.
bool read_and_clear_motion() {
    const bool moved =
        simulated_time > motion_cleared_at && hours(motion_cleared_at) < VOYAGE_HOURS;
    motion_cleared_at = simulated_time;
    return moved;
}

uint8_t high_byte(int16_t value) {
    return static_cast<uint8_t>(std::bit_cast<uint16_t>(value) >> 8);
}

uint8_t low_byte(int16_t value) {
    return static_cast<uint8_t>(std::bit_cast<uint16_t>(value));
}

bool read_accelerometer(uint8_t register_address, std::span<uint8_t> out) {
    const auto acceleration = current_acceleration();
    for (std::size_t i = 0; i < out.size(); ++i) {
        switch (register_address + i) {
        case REGISTER_WHO_AM_I: out[i] = 0x33; break;
        case REGISTER_ACCELERATION: out[i] = high_byte(acceleration.x); break;
        case REGISTER_ACCELERATION + 1: out[i] = low_byte(acceleration.x); break;
        case REGISTER_ACCELERATION + 2: out[i] = high_byte(acceleration.y); break;
        case REGISTER_ACCELERATION + 3: out[i] = low_byte(acceleration.y); break;
        case REGISTER_ACCELERATION + 4: out[i] = high_byte(acceleration.z); break;
        case REGISTER_ACCELERATION + 5: out[i] = low_byte(acceleration.z); break;
        case REGISTER_MOTION: out[i] = read_and_clear_motion() ? 0x01 : 0x00; break;
        default: return false;
        }
    }
    return true;
}

} // namespace

Clock::time_point Clock::now() {
    return time_point{duration{static_cast<rep>(simulated_time.count())}};
}

void sleep_for(Clock::duration duration) {
    simulated_time += duration;
}

void log_line(std::string_view line) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(simulated_time);
    const auto days = std::chrono::duration_cast<std::chrono::days>(elapsed);
    const auto text = std::format("[day {:2} {:%T}] {}\n", days.count(), elapsed - days, line);
    std::fputs(text.c_str(), stdout);
    std::fflush(stdout); // like a UART: each line is out before the next one
}

bool i2c_read(uint8_t address, uint8_t register_address, std::span<uint8_t> out) {
    switch (address) {
    case ENVIRONMENT_SENSOR_ADDRESS: return read_environment_sensor(register_address, out);
    case ACCELEROMETER_ADDRESS: return read_accelerometer(register_address, out);
    default: return false;
    }
}

bool i2c_write(uint8_t address, uint8_t register_address, uint8_t value) {
    if (address != ENVIRONMENT_SENSOR_ADDRESS || register_address != REGISTER_CONTROL) {
        return false;
    }
    update_environment_sensor();
    if (value == 0x01 && !environment_sensor.measuring) {
        environment_sensor.measuring = true;
        environment_sensor.started = Clock::now();
    }
    return true;
}

void gnss_power(bool on) {
    if (on && !gnss.on) {
        gnss.powered_at = Clock::now();
    }
    gnss.on = on;
}

/// Follows the voyage in a straight line from Bergen to Reykjavik.
std::optional<Fix> gnss_read_fix() {
    const auto time_to_first_fix = gnss.warm ? 4s : 32s;
    if (!gnss.on || Clock::now() - gnss.powered_at < time_to_first_fix) {
        return std::nullopt;
    }
    gnss.warm = true;
    const double voyage = std::min(1.0, simulated_hours() / VOYAGE_HOURS);
    return Fix{.latitude = 60.3913 + (64.1466 - 60.3913) * voyage,
               .longitude = 5.3221 + (-21.9426 - 5.3221) * voyage};
}

bool modem_send(std::string_view payload) {
    if (std::chrono::duration_cast<std::chrono::hours>(simulated_time).count() % 24 == 2) {
        return false;
    }
    cloud.emplace_back(payload);
    log("cloud <- {}", payload);
    return true;
}

const std::vector<std::string>& cloud_received() {
    return cloud;
}

} // namespace platform
