#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <bit>
#include <concepts>

namespace obf::bytecode {

// JVM specification standard primitive types
using u1 = uint8_t;
using u2 = uint16_t;
using u4 = uint32_t;
using u8 = uint64_t;

using i1 = int8_t;
using i2 = int16_t;
using i4 = int32_t;
using i8 = int64_t;

// JVM Class File Magic Number
inline constexpr u4 JVM_CLASS_MAGIC = 0xCAFEBABE;

// Standard Java Class major versions
enum class JavaMajorVersion : u2 {
    Java1_1 = 45,
    Java1_2 = 46,
    Java1_3 = 47,
    Java1_4 = 48,
    Java5   = 49,
    Java6   = 50,
    Java7   = 51,
    Java8   = 52,
    Java9   = 53,
    Java10  = 54,
    Java11  = 55,
    Java17  = 61,
    Java21  = 65,
    Java22  = 66,
    Java23  = 67
};

// Access Flags according to JVMS 4.1, 4.5, 4.6
namespace access_flags {
    inline constexpr u2 ACC_PUBLIC       = 0x0001;
    inline constexpr u2 ACC_PRIVATE      = 0x0002;
    inline constexpr u2 ACC_PROTECTED    = 0x0004;
    inline constexpr u2 ACC_STATIC       = 0x0008;
    inline constexpr u2 ACC_FINAL        = 0x0010;
    inline constexpr u2 ACC_SUPER        = 0x0020;
    inline constexpr u2 ACC_SYNCHRONIZED = 0x0020;
    inline constexpr u2 ACC_VOLATILE     = 0x0040;
    inline constexpr u2 ACC_BRIDGE       = 0x0040;
    inline constexpr u2 ACC_TRANSIENT    = 0x0080;
    inline constexpr u2 ACC_VARARGS      = 0x0080;
    inline constexpr u2 ACC_NATIVE       = 0x0100;
    inline constexpr u2 ACC_INTERFACE    = 0x0200;
    inline constexpr u2 ACC_ABSTRACT     = 0x0400;
    inline constexpr u2 ACC_STRICT       = 0x0800;
    inline constexpr u2 ACC_SYNTHETIC    = 0x1000;
    inline constexpr u2 ACC_ANNOTATION   = 0x2000;
    inline constexpr u2 ACC_ENUM         = 0x4000;
    inline constexpr u2 ACC_MODULE       = 0x8000;
}

// Byte-swapping utilities for JVM Big-Endian format using C++23 std::byteswap
template <std::integral T>
[[nodiscard]] constexpr T to_big_endian(T value) noexcept {
    if constexpr (std::endian::native == std::endian::big) {
        return value;
    } else {
        return std::byteswap(value);
    }
}

template <std::integral T>
[[nodiscard]] constexpr T from_big_endian(T value) noexcept {
    return to_big_endian(value); // Symmetric operation
}

} // namespace obf::bytecode
