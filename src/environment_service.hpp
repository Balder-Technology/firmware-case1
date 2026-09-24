#pragma once

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "expected.hpp"
#include "platform.hpp"
#include "service.hpp"

namespace balder {

/// Driver for the temperature/humidity sensor. Register map in platform.hpp.
class EnvironmentService {
public:
    using Reading = Environment;
    static constexpr std::string_view NAME = "environment";

    [[nodiscard]] Expected<void, Error> initialize() {
        const auto chip_id = read_registers<1>(Register::chip_id);
        if (!chip_id) {
            return Unexpected{chip_id.error()};
        }
        if ((*chip_id)[0] != CHIP_ID) {
            return Unexpected{Error::unknown_chip};
        }
        return {};
    }

    [[nodiscard]] Expected<Environment, Error> read() {
        if (!platform::i2c_write(ADDRESS, static_cast<uint8_t>(Register::control), CONTROL_START)) {
            return Unexpected{Error::no_ack};
        }
        if (const auto measured = wait_for_measurement(); !measured) {
            return Unexpected{measured.error()};
        }

        // TEMPERATURE and HUMIDITY are adjacent, so one read gets both.
        const auto raw = read_registers<4>(Register::temperature);
        if (!raw) {
            return Unexpected{raw.error()};
        }
        const auto [temperature_msb, temperature_lsb, humidity_msb, humidity_lsb] = *raw;
        const auto temperature = unsigned_from_big_endian(temperature_msb, temperature_lsb);
        const auto humidity = unsigned_from_big_endian(humidity_msb, humidity_lsb);
        return Environment{.temperature_celsius = temperature / 100.0f,
                           .humidity_percent = humidity / 100.0f};
    }

private:
    enum class Register : uint8_t {
        chip_id = 0xD0,
        status = 0xF3,
        control = 0xF4,
        temperature = 0xFA
    };

    static constexpr uint8_t ADDRESS = 0x76;
    static constexpr uint8_t CHIP_ID = 0x60;
    static constexpr uint8_t STATUS_MEASURING = 0x01;
    static constexpr uint8_t CONTROL_START = 0x01;
    static constexpr std::chrono::milliseconds POLL_INTERVAL{2};
    static constexpr std::chrono::milliseconds MEASUREMENT_TIMEOUT{50};

    /// Polls STATUS until the measurement finishes, or the sensor gives up.
    [[nodiscard]] static Expected<void, Error> wait_for_measurement() {
        const auto start = platform::Clock::now();
        while (true) {
            platform::sleep_for(POLL_INTERVAL);
            const auto status = read_registers<1>(Register::status);
            if (!status) {
                return Unexpected{status.error()};
            }
            if (((*status)[0] & STATUS_MEASURING) == 0) {
                return {};
            }
            if (platform::Clock::now() - start >= MEASUREMENT_TIMEOUT) {
                return Unexpected{Error::timeout};
            }
        }
    }

    /// Reads N consecutive registers, starting at first.
    template <std::size_t N>
    [[nodiscard]] static Expected<std::array<uint8_t, N>, Error> read_registers(Register first) {
        std::array<uint8_t, N> buffer{};
        if (!platform::i2c_read(ADDRESS, static_cast<uint8_t>(first), buffer)) {
            return Unexpected{Error::no_ack};
        }
        return buffer;
    }

    /// Combines a big-endian register pair that holds an unsigned value.
    [[nodiscard]] static constexpr uint16_t unsigned_from_big_endian(uint8_t msb, uint8_t lsb) {
        return static_cast<uint16_t>(msb << 8 | lsb);
    }

    /// Combines a big-endian register pair that holds a signed value.
    [[nodiscard]] static constexpr int16_t signed_from_big_endian(uint8_t msb, uint8_t lsb) {
        return std::bit_cast<int16_t>(unsigned_from_big_endian(msb, lsb));
    }
};

static_assert(Service<EnvironmentService>);

} // namespace balder
