#pragma once

#include "types.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <span>
#include <cstring>
#include <stdexcept>

namespace obf::bytecode {

class BinaryWriter {
public:
    BinaryWriter() = default;
    explicit BinaryWriter(size_t reserve_capacity) {
        m_buffer.reserve(reserve_capacity);
    }

    [[nodiscard]] size_t size() const noexcept { return m_buffer.size(); }
    [[nodiscard]] const std::vector<u1>& data() const noexcept { return m_buffer; }
    [[nodiscard]] std::vector<u1>& data() noexcept { return m_buffer; }
    [[nodiscard]] std::span<const u1> span() const noexcept { return m_buffer; }

    void clear() noexcept { m_buffer.clear(); }

    void write_u1(u1 val) {
        m_buffer.push_back(val);
    }

    void write_i1(i1 val) {
        write_u1(static_cast<u1>(val));
    }

    void write_u2(u2 val) {
        u2 be = to_big_endian(val);
        const u1* bytes = reinterpret_cast<const u1*>(&be);
        m_buffer.insert(m_buffer.end(), bytes, bytes + 2);
    }

    void write_i2(i2 val) {
        write_u2(static_cast<u2>(val));
    }

    void write_u4(u4 val) {
        u4 be = to_big_endian(val);
        const u1* bytes = reinterpret_cast<const u1*>(&be);
        m_buffer.insert(m_buffer.end(), bytes, bytes + 4);
    }

    void write_i4(i4 val) {
        write_u4(static_cast<u4>(val));
    }

    void write_u8(u8 val) {
        u8 be = to_big_endian(val);
        const u1* bytes = reinterpret_cast<const u1*>(&be);
        m_buffer.insert(m_buffer.end(), bytes, bytes + 8);
    }

    void write_i8(i8 val) {
        write_u8(static_cast<u8>(val));
    }

    void write_bytes(std::span<const u1> bytes) {
        m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
    }

    void write_bytes(const std::vector<u1>& bytes) {
        m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
    }

    void write_utf8(std::string_view str) {
        const u1* bytes = reinterpret_cast<const u1*>(str.data());
        m_buffer.insert(m_buffer.end(), bytes, bytes + str.size());
    }

    // Patching methods for length placeholders
    void patch_u2(size_t offset, u2 val) {
        if (offset + 2 > m_buffer.size()) {
            throw std::out_of_range("BinaryWriter patch_u2 out of range");
        }
        u2 be = to_big_endian(val);
        std::memcpy(m_buffer.data() + offset, &be, 2);
    }

    void patch_u4(size_t offset, u4 val) {
        if (offset + 4 > m_buffer.size()) {
            throw std::out_of_range("BinaryWriter patch_u4 out of range");
        }
        u4 be = to_big_endian(val);
        std::memcpy(m_buffer.data() + offset, &be, 4);
    }

private:
    std::vector<u1> m_buffer;
};

} // namespace obf::bytecode
