#pragma once

#include "types.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <span>
#include <stdexcept>
#include <cstring>
#include <expected>

namespace obf::bytecode {

class BinaryReader {
public:
    explicit BinaryReader(std::span<const u1> data)
        : m_data(data), m_offset(0) {}

    explicit BinaryReader(const std::vector<u1>& data)
        : m_data(data), m_offset(0) {}

    [[nodiscard]] size_t offset() const noexcept { return m_offset; }
    [[nodiscard]] size_t size() const noexcept { return m_data.size(); }
    [[nodiscard]] size_t remaining() const noexcept {
        return m_offset <= m_data.size() ? m_data.size() - m_offset : 0;
    }
    [[nodiscard]] bool has_bytes(size_t count) const noexcept {
        return m_offset + count <= m_data.size();
    }

    void seek(size_t offset) {
        if (offset > m_data.size()) {
            throw std::out_of_range("BinaryReader seek offset out of bounds");
        }
        m_offset = offset;
    }

    void skip(size_t count) {
        if (!has_bytes(count)) {
            throw std::out_of_range("BinaryReader skip extends past end of buffer");
        }
        m_offset += count;
    }

    [[nodiscard]] u1 read_u1() {
        if (!has_bytes(1)) throw std::out_of_range("Unexpected end of data reading u1");
        return m_data[m_offset++];
    }

    [[nodiscard]] i1 read_i1() {
        return static_cast<i1>(read_u1());
    }

    [[nodiscard]] u2 read_u2() {
        if (!has_bytes(2)) throw std::out_of_range("Unexpected end of data reading u2");
        u2 val;
        std::memcpy(&val, m_data.data() + m_offset, 2);
        m_offset += 2;
        return from_big_endian(val);
    }

    [[nodiscard]] i2 read_i2() {
        return static_cast<i2>(read_u2());
    }

    [[nodiscard]] u4 read_u4() {
        if (!has_bytes(4)) throw std::out_of_range("Unexpected end of data reading u4");
        u4 val;
        std::memcpy(&val, m_data.data() + m_offset, 4);
        m_offset += 4;
        return from_big_endian(val);
    }

    [[nodiscard]] i4 read_i4() {
        return static_cast<i4>(read_u4());
    }

    [[nodiscard]] u8 read_u8() {
        if (!has_bytes(8)) throw std::out_of_range("Unexpected end of data reading u8");
        u8 val;
        std::memcpy(&val, m_data.data() + m_offset, 8);
        m_offset += 8;
        return from_big_endian(val);
    }

    [[nodiscard]] i8 read_i8() {
        return static_cast<i8>(read_u8());
    }

    [[nodiscard]] std::vector<u1> read_bytes(size_t count) {
        if (!has_bytes(count)) throw std::out_of_range("Unexpected end of data reading bytes");
        std::vector<u1> res(m_data.begin() + m_offset, m_data.begin() + m_offset + count);
        m_offset += count;
        return res;
    }

    [[nodiscard]] std::span<const u1> read_span(size_t count) {
        if (!has_bytes(count)) throw std::out_of_range("Unexpected end of data reading span");
        std::span<const u1> slice = m_data.subspan(m_offset, count);
        m_offset += count;
        return slice;
    }

    [[nodiscard]] std::string read_utf8(size_t length) {
        if (!has_bytes(length)) throw std::out_of_range("Unexpected end of data reading string");
        std::string str(reinterpret_cast<const char*>(m_data.data() + m_offset), length);
        m_offset += length;
        return str;
    }

private:
    std::span<const u1> m_data;
    size_t m_offset{0};
};

} // namespace obf::bytecode
