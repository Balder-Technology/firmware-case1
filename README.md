# Balder firmware interview case

A small C++20 simulation of a battery-powered tracker. Every 5 minutes it wakes, reads a temperature/humidity sensor and a GNSS receiver, buffers the readings and sends them to the "cloud". Hardware and time are simulated, so days of device time run in under a second. The unit rides a ship from Bergen to Reykjavik: a 72-hour voyage, after which it stays in port.

You don't need to know Zephyr or anything about Balder. We're interested in how you read unfamiliar code, reason about problems and use the tools available, AI included.

## Get started

Clone the repository and open it in VS Code, then choose **Reopen in Container**; the first build of the container can take a few minutes. Or use your own toolchain: gcc 13+, clang 18+ or Apple clang 17+, and CMake 3.25+. Without the container, accept the extensions VS Code recommends; code navigation starts working once you have run the build command below.

```
cmake --workflow --preset build     # configure and build
cmake --workflow --preset test      # configure, build and run the tests
./build/debug/tracker [cycles]      # run the tracker; the default 48 cycles is 4 hours
```

The tests fail at first. Making them pass is where we start.

## What the tracker must do

1. Read the temperature/humidity sensor and the GNSS receiver every cycle.
2. Report readings within the sensor's range: -40 to +85 °C and 0 to 100 %RH.
3. Wake every 5 minutes, however long it has been running.
4. Deliver every record to the cloud exactly once, in order. There is no coverage from 02:00 to 03:00 each day, so records wait in a buffer until coverage returns.

`tests/tests.cpp` has one test per rule, in this order.

## What working output looks like

```
[day  0 00:20:04] #5      -1.30 C  73.7 %RH   60.40874,   5.19545  buffered 5
[day  0 00:25:04] #6      -1.13 C  73.4 %RH   60.41309,   5.16390  buffered 6
[day  0 00:25:04] cloud <- {"t":0,"temp":-2.00,"rh":75.00,"lat":60.39176,"lon":5.31873}
...
[day  0 04:00:00] done: 48 cycles, 43 sent, 0 lost, 5 still buffered
```

One line per cycle, a `cloud <-` line for each record the cloud acknowledged, and a summary at the end.

## Code

| File | What |
|---|---|
| `src/main.cpp` | Entry point: runs N cycles |
| `src/tracker.hpp/.cpp` | `Tracker`: one wake cycle is sample, buffer, send, sleep |
| `src/service.hpp` | `Record`, `Error` and the `Service` concept every sensor satisfies |
| `src/environment_service.hpp` | Temperature/humidity driver over I2C registers |
| `src/gnss_service.hpp` | Power on GNSS, wait for a fix, power off |
| `src/ring_buffer.hpp` | Fixed-size FIFO for records waiting to be sent |
| `src/scope_exit.hpp` | RAII guard that runs a lambda on scope exit |
| `src/expected.hpp` | C++20 stand-in for C++23's `std::expected`; behaves like the standard one |
| `src/platform.hpp` | The hardware API and register map. Read it like a datasheet |
| `simulation/platform.cpp` | The simulated hardware behind `platform.hpp`. Assumed correct; you can skip it |
| `tests/tests.cpp` | One test per rule. Each runs in its own process, and a hang or crash counts as a failure |

## Other builds

To build with the address and undefined-behaviour sanitizers:

```
cmake --preset sanitizers && cmake --build --preset sanitizers
./build/sanitizers/tracker
```

`cmake --list-presets=all` lists the rest, such as `release` and `strict` (warnings as errors).
