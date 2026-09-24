#pragma once

// The chip and its peripherals: the hardware API the firmware codes against.
// Read it like a datasheet. The implementation, in simulation/platform.cpp,
// is a software stand-in for the hardware; it behaves exactly as documented
// here and is not part of the case.

#include <chrono>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace platform {

/// The chip's millisecond uptime counter. Starts at 0 at boot.
struct Clock {
    using rep = uint32_t;
    using period = std::milli;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<Clock>;
    static constexpr bool is_steady = true;

    [[nodiscard]] static time_point now();
};

/// Sleeps the chip. In the simulation time jumps ahead instantly.
void sleep_for(Clock::duration duration);

/// Writes one log line, prefixed with the time since boot.
void log_line(std::string_view line);

template <typename... Arguments>
void log(std::format_string<Arguments...> pattern, Arguments&&... arguments) {
    log_line(std::format(pattern, std::forward<Arguments>(arguments)...));
}

/// I2C bus. Returns false if the device does not ACK.
///
/// Temperature/humidity sensor at address 0x76. Measures -40..+85 degC and
/// 0..100 %RH. Registers are big-endian:
///   0xD0 CHIP_ID      R  1 byte   always 0x60
///   0xF3 STATUS       R  1 byte   bit 0 set while measuring (takes 10 ms)
///   0xF4 CONTROL      W  1 byte   write 0x01 to start a measurement
///   0xFA TEMPERATURE  R  2 bytes  signed, 0.01 degC per LSB
///   0xFC HUMIDITY     R  2 bytes  unsigned, 0.01 %RH per LSB
///
/// Accelerometer at address 0x18. Registers are read-only and big-endian:
///   0x0F WHO_AM_I      R  1 byte   always 0x33
///   0x28 ACCELERATION  R  6 bytes  X, Y, Z: each signed, 1 mg per LSB
///   0x31 MOTION        R  1 byte   bit 0 set if the unit moved since MOTION
///                                  was last read; reading clears it
///
/// Reads and writes start at register_address; a multi-byte read continues
/// through the following registers.
[[nodiscard]] bool i2c_read(uint8_t address, uint8_t register_address, std::span<uint8_t> out);
[[nodiscard]] bool i2c_write(uint8_t address, uint8_t register_address, uint8_t value);

/// GNSS receiver. After power-on the first fix takes ~30 s, later ones ~4 s.
struct Fix {
    double latitude{};
    double longitude{};
};
void gnss_power(bool on);
[[nodiscard]] std::optional<Fix> gnss_read_fix();

/// Modem. Returns true once the cloud acknowledges the payload.
/// There is no coverage between 02:00 and 03:00 each day.
[[nodiscard]] bool modem_send(std::string_view payload);

/// Every payload the cloud has acknowledged, oldest first. For tests.
[[nodiscard]] const std::vector<std::string>& cloud_received();

} // namespace platform
