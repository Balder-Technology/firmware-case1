#pragma once

#include <concepts>
#include <utility>

namespace balder {

/// Runs a callable when it goes out of scope, on every return path.
///
/// Bind it to a named variable: a discarded temporary would run its callable
/// immediately, at the end of the full expression. [[nodiscard]] catches that.
template <std::invocable F>
class [[nodiscard]] ScopeExit {
public:
    explicit ScopeExit(F on_exit) : on_exit_(std::move(on_exit)) {}
    ~ScopeExit() { on_exit_(); }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit(ScopeExit&&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    ScopeExit& operator=(ScopeExit&&) = delete;

private:
    F on_exit_;
};

} // namespace balder
