#pragma once

#include "types.hpp"

namespace ron {

enum class ErrorCode : u16 {
    Ok = 0,
    InvalidArgument,
    OutOfMemory,
    BufferTooSmall,
    CorruptData,
    CompressionFailed,
    DecompressionFailed,
    IoError,
    VersionMismatch,
    OutOfBounds,
    InvalidMagic
};

constexpr std::string_view error_code_str(ErrorCode c) noexcept {
    switch (c) {
        case ErrorCode::Ok: return "Ok";
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::OutOfMemory: return "OutOfMemory";
        case ErrorCode::BufferTooSmall: return "BufferTooSmall";
        case ErrorCode::CorruptData: return "CorruptData";
        case ErrorCode::CompressionFailed: return "CompressionFailed";
        case ErrorCode::DecompressionFailed: return "DecompressionFailed";
        case ErrorCode::IoError: return "IoError";
        case ErrorCode::VersionMismatch: return "VersionMismatch";
        case ErrorCode::OutOfBounds: return "OutOfBounds";
        case ErrorCode::InvalidMagic: return "InvalidMagic";
    }
    return "Unknown";
}

template <typename T>
struct [[nodiscard]] Result {
    static_assert(std::is_default_constructible_v<T>, "T must be default constructible");

    T value{};
    ErrorCode error{ErrorCode::Ok};

    static constexpr Result ok(T v) noexcept {
        return Result{v, ErrorCode::Ok};
    }

    static constexpr Result err(ErrorCode e) noexcept {
        return Result{T{}, e};
    }

    constexpr bool is_ok() const noexcept { return error == ErrorCode::Ok; }
    constexpr bool is_err() const noexcept { return error != ErrorCode::Ok; }
};

template <>
struct [[nodiscard]] Result<void> {
    ErrorCode error{ErrorCode::Ok};

    static constexpr Result ok() noexcept {
        return Result{ErrorCode::Ok};
    }

    static constexpr Result err(ErrorCode e) noexcept {
        return Result{e};
    }

    constexpr bool is_ok() const noexcept { return error == ErrorCode::Ok; }
    constexpr bool is_err() const noexcept { return error != ErrorCode::Ok; }
};

} // namespace ron
