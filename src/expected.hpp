#pragma once

// A C++20 stand-in for the part of C++23's std::expected this code uses.
// It behaves like std::expected for that subset and is not part of the case.
// To move to C++23, replace Expected with std::expected and Unexpected with
// std::unexpected, then delete this file.

#include <optional>
#include <utility>
#include <variant>

namespace balder {

/// An error, wrapped so it can be returned from a function returning Expected.
template <typename E>
class Unexpected {
public:
    constexpr explicit Unexpected(E error) : error_(std::move(error)) {}

    [[nodiscard]] constexpr const E& error() const { return error_; }

private:
    E error_;
};

/// Either a value of type T or an error of type E.
///
/// operator* and operator-> require has_value(), and error() requires
/// !has_value(). Unlike std::expected, breaking that throws instead of being
/// undefined behaviour.
template <typename T, typename E>
class [[nodiscard]] Expected {
public:
    constexpr Expected(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
    constexpr Expected(Unexpected<E> unexpected)
        : storage_(std::in_place_index<1>, unexpected.error()) {}

    [[nodiscard]] constexpr bool has_value() const { return storage_.index() == 0; }
    constexpr explicit operator bool() const { return has_value(); }

    [[nodiscard]] constexpr const T& operator*() const { return std::get<0>(storage_); }
    [[nodiscard]] constexpr const T* operator->() const { return &std::get<0>(storage_); }
    [[nodiscard]] constexpr const E& error() const { return std::get<1>(storage_); }

private:
    std::variant<T, E> storage_;
};

/// Success with no value, or an error of type E. `return {};` means success.
template <typename E>
class [[nodiscard]] Expected<void, E> {
public:
    constexpr Expected() = default;
    constexpr Expected(Unexpected<E> unexpected) : error_(unexpected.error()) {}

    [[nodiscard]] constexpr bool has_value() const { return !error_.has_value(); }
    constexpr explicit operator bool() const { return has_value(); }

    [[nodiscard]] constexpr const E& error() const { return error_.value(); }

private:
    std::optional<E> error_;
};

} // namespace balder
