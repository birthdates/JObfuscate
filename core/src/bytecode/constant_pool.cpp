#include "bytecode/constant_pool.hpp"
#include <stdexcept>
#include <cstring>

namespace obf::bytecode {

ConstantPool::ConstantPool() {
    // Index 0 is unused in JVM specification
    m_entries.emplace_back(std::monostate{});
}

const ConstantPoolEntry& ConstantPool::get(u2 index) const {
    if (index == 0 || index >= m_entries.size()) {
        throw std::out_of_range("ConstantPool index out of bounds: " + std::to_string(index));
    }
    return m_entries[index];
}

ConstantPoolEntry& ConstantPool::get_mut(u2 index) {
    if (index == 0 || index >= m_entries.size()) {
        throw std::out_of_range("ConstantPool index out of bounds: " + std::to_string(index));
    }
    return m_entries[index];
}

std::string_view ConstantPool::get_utf8(u2 index) const {
    if (const auto* utf8 = get_if<CpUtf8>(index)) {
        return utf8->value;
    }
    throw std::runtime_error("ConstantPool entry at index " + std::to_string(index) + " is not Utf8");
}

std::string_view ConstantPool::get_class_name(u2 class_index) const {
    if (const auto* cls = get_if<CpClass>(class_index)) {
        return get_utf8(cls->name_index);
    }
    throw std::runtime_error("ConstantPool entry at index " + std::to_string(class_index) + " is not Class");
}

std::string_view ConstantPool::get_string_value(u2 string_index) const {
    if (const auto* str = get_if<CpString>(string_index)) {
        return get_utf8(str->string_index);
    }
    throw std::runtime_error("ConstantPool entry at index " + std::to_string(string_index) + " is not String");
}

u2 ConstantPool::add_entry(ConstantPoolEntry entry) {
    u2 idx = static_cast<u2>(m_entries.size());
    bool is_wide = std::holds_alternative<CpLong>(entry) || std::holds_alternative<CpDouble>(entry);
    
    if (const auto* utf8 = std::get_if<CpUtf8>(&entry)) {
        m_utf8_lookup[utf8->value] = idx;
    }

    m_entries.push_back(std::move(entry));

    if (is_wide) {
        // JVM 4.4.5: All 8-byte constants take up two entries in the constant_pool table
        m_entries.emplace_back(CpUnusableSlot{});
    }

    return idx;
}

u2 ConstantPool::find_or_add_utf8(std::string_view value) {
    std::string key(value);
    if (auto it = m_utf8_lookup.find(key); it != m_utf8_lookup.end()) {
        return it->second;
    }
    return add_entry(CpUtf8{std::move(key)});
}

u2 ConstantPool::find_or_add_string(std::string_view value) {
    u2 utf8_idx = find_or_add_utf8(value);
    for (size_t i = 1; i < m_entries.size(); ++i) {
        if (const auto* s = std::get_if<CpString>(&m_entries[i])) {
            if (s->string_index == utf8_idx) return static_cast<u2>(i);
        }
    }
    return add_entry(CpString{utf8_idx});
}

u2 ConstantPool::find_or_add_class(std::string_view class_name) {
    u2 name_idx = find_or_add_utf8(class_name);
    for (size_t i = 1; i < m_entries.size(); ++i) {
        if (const auto* c = std::get_if<CpClass>(&m_entries[i])) {
            if (c->name_index == name_idx) return static_cast<u2>(i);
        }
    }
    return add_entry(CpClass{name_idx});
}

u2 ConstantPool::find_or_add_name_and_type(std::string_view name, std::string_view descriptor) {
    u2 name_idx = find_or_add_utf8(name);
    u2 desc_idx = find_or_add_utf8(descriptor);
    for (size_t i = 1; i < m_entries.size(); ++i) {
        if (const auto* nat = std::get_if<CpNameAndType>(&m_entries[i])) {
            if (nat->name_index == name_idx && nat->descriptor_index == desc_idx) {
                return static_cast<u2>(i);
            }
        }
    }
    return add_entry(CpNameAndType{name_idx, desc_idx});
}

u2 ConstantPool::find_or_add_methodref(std::string_view class_name, std::string_view method_name, std::string_view descriptor) {
    u2 class_idx = find_or_add_class(class_name);
    u2 nat_idx = find_or_add_name_and_type(method_name, descriptor);
    for (size_t i = 1; i < m_entries.size(); ++i) {
        if (const auto* mr = std::get_if<CpMethodref>(&m_entries[i])) {
            if (mr->class_index == class_idx && mr->name_and_type_index == nat_idx) {
                return static_cast<u2>(i);
            }
        }
    }
    return add_entry(CpMethodref{class_idx, nat_idx});
}

u2 ConstantPool::find_or_add_interface_methodref(std::string_view class_name, std::string_view method_name, std::string_view descriptor) {
    u2 class_idx = find_or_add_class(class_name);
    u2 nat_idx = find_or_add_name_and_type(method_name, descriptor);
    for (size_t i = 1; i < m_entries.size(); ++i) {
        if (const auto* imr = std::get_if<CpInterfaceMethodref>(&m_entries[i])) {
            if (imr->class_index == class_idx && imr->name_and_type_index == nat_idx) {
                return static_cast<u2>(i);
            }
        }
    }
    return add_entry(CpInterfaceMethodref{class_idx, nat_idx});
}

u2 ConstantPool::find_or_add_fieldref(std::string_view class_name, std::string_view field_name, std::string_view descriptor) {
    u2 class_idx = find_or_add_class(class_name);
    u2 nat_idx = find_or_add_name_and_type(field_name, descriptor);
    for (size_t i = 1; i < m_entries.size(); ++i) {
        if (const auto* fr = std::get_if<CpFieldref>(&m_entries[i])) {
            if (fr->class_index == class_idx && fr->name_and_type_index == nat_idx) {
                return static_cast<u2>(i);
            }
        }
    }
    return add_entry(CpFieldref{class_idx, nat_idx});
}

u2 ConstantPool::find_or_add_integer(i4 value) {
    for (size_t i = 1; i < m_entries.size(); ++i) {
        if (const auto* item = std::get_if<CpInteger>(&m_entries[i])) {
            if (item->value == value) return static_cast<u2>(i);
        }
    }
    return add_entry(CpInteger{value});
}

u2 ConstantPool::find_or_add_long(i8 value) {
    for (size_t i = 1; i < m_entries.size(); ++i) {
        if (const auto* item = std::get_if<CpLong>(&m_entries[i])) {
            if (item->value == value) return static_cast<u2>(i);
        }
    }
    return add_entry(CpLong{value});
}

void ConstantPool::rebuild_utf8_lookup() {
    m_utf8_lookup.clear();
    for (size_t i = 1; i < m_entries.size(); ++i) {
        if (const auto* u = std::get_if<CpUtf8>(&m_entries[i])) {
            m_utf8_lookup[u->value] = static_cast<u2>(i);
        }
    }
}

void ConstantPool::read(BinaryReader& reader, u2 count) {
    m_entries.clear();
    m_utf8_lookup.clear();
    m_entries.emplace_back(std::monostate{}); // index 0

    for (u2 i = 1; i < count; ++i) {
        u1 tag_byte = reader.read_u1();
        auto tag = static_cast<ConstantTag>(tag_byte);

        switch (tag) {
            case ConstantTag::Utf8: {
                u2 len = reader.read_u2();
                std::string s = reader.read_utf8(len);
                m_utf8_lookup[s] = i;
                m_entries.emplace_back(CpUtf8{std::move(s)});
                break;
            }
            case ConstantTag::Integer: {
                m_entries.emplace_back(CpInteger{reader.read_i4()});
                break;
            }
            case ConstantTag::Float: {
                u4 raw = reader.read_u4();
                float val;
                std::memcpy(&val, &raw, 4);
                m_entries.emplace_back(CpFloat{val});
                break;
            }
            case ConstantTag::Long: {
                m_entries.emplace_back(CpLong{reader.read_i8()});
                m_entries.emplace_back(CpUnusableSlot{});
                ++i;
                break;
            }
            case ConstantTag::Double: {
                u8 raw = reader.read_u8();
                double val;
                std::memcpy(&val, &raw, 8);
                m_entries.emplace_back(CpDouble{val});
                m_entries.emplace_back(CpUnusableSlot{});
                ++i;
                break;
            }
            case ConstantTag::Class: {
                m_entries.emplace_back(CpClass{reader.read_u2()});
                break;
            }
            case ConstantTag::String: {
                m_entries.emplace_back(CpString{reader.read_u2()});
                break;
            }
            case ConstantTag::Fieldref: {
                u2 cls = reader.read_u2();
                u2 nat = reader.read_u2();
                m_entries.emplace_back(CpFieldref{cls, nat});
                break;
            }
            case ConstantTag::Methodref: {
                u2 cls = reader.read_u2();
                u2 nat = reader.read_u2();
                m_entries.emplace_back(CpMethodref{cls, nat});
                break;
            }
            case ConstantTag::InterfaceMethodref: {
                u2 cls = reader.read_u2();
                u2 nat = reader.read_u2();
                m_entries.emplace_back(CpInterfaceMethodref{cls, nat});
                break;
            }
            case ConstantTag::NameAndType: {
                u2 name = reader.read_u2();
                u2 desc = reader.read_u2();
                m_entries.emplace_back(CpNameAndType{name, desc});
                break;
            }
            case ConstantTag::MethodHandle: {
                u1 kind = reader.read_u1();
                u2 ref = reader.read_u2();
                m_entries.emplace_back(CpMethodHandle{kind, ref});
                break;
            }
            case ConstantTag::MethodType: {
                m_entries.emplace_back(CpMethodType{reader.read_u2()});
                break;
            }
            case ConstantTag::Dynamic: {
                u2 bsm = reader.read_u2();
                u2 nat = reader.read_u2();
                m_entries.emplace_back(CpDynamic{bsm, nat});
                break;
            }
            case ConstantTag::InvokeDynamic: {
                u2 bsm = reader.read_u2();
                u2 nat = reader.read_u2();
                m_entries.emplace_back(CpInvokeDynamic{bsm, nat});
                break;
            }
            case ConstantTag::Module: {
                m_entries.emplace_back(CpModule{reader.read_u2()});
                break;
            }
            case ConstantTag::Package: {
                m_entries.emplace_back(CpPackage{reader.read_u2()});
                break;
            }
            default:
                throw std::runtime_error("Unknown constant pool tag: " + std::to_string(tag_byte));
        }
    }
}

void ConstantPool::write(BinaryWriter& writer) const {
    for (size_t i = 1; i < m_entries.size(); ++i) {
        const auto& entry = m_entries[i];

        std::visit([&](const auto& item) {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, CpUtf8>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Utf8));
                writer.write_u2(static_cast<u2>(item.value.size()));
                writer.write_utf8(item.value);
            } else if constexpr (std::is_same_v<T, CpInteger>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Integer));
                writer.write_i4(item.value);
            } else if constexpr (std::is_same_v<T, CpFloat>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Float));
                u4 raw;
                std::memcpy(&raw, &item.value, 4);
                writer.write_u4(raw);
            } else if constexpr (std::is_same_v<T, CpLong>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Long));
                writer.write_i8(item.value);
            } else if constexpr (std::is_same_v<T, CpDouble>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Double));
                u8 raw;
                std::memcpy(&raw, &item.value, 8);
                writer.write_u8(raw);
            } else if constexpr (std::is_same_v<T, CpClass>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Class));
                writer.write_u2(item.name_index);
            } else if constexpr (std::is_same_v<T, CpString>) {
                writer.write_u1(static_cast<u1>(ConstantTag::String));
                writer.write_u2(item.string_index);
            } else if constexpr (std::is_same_v<T, CpFieldref>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Fieldref));
                writer.write_u2(item.class_index);
                writer.write_u2(item.name_and_type_index);
            } else if constexpr (std::is_same_v<T, CpMethodref>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Methodref));
                writer.write_u2(item.class_index);
                writer.write_u2(item.name_and_type_index);
            } else if constexpr (std::is_same_v<T, CpInterfaceMethodref>) {
                writer.write_u1(static_cast<u1>(ConstantTag::InterfaceMethodref));
                writer.write_u2(item.class_index);
                writer.write_u2(item.name_and_type_index);
            } else if constexpr (std::is_same_v<T, CpNameAndType>) {
                writer.write_u1(static_cast<u1>(ConstantTag::NameAndType));
                writer.write_u2(item.name_index);
                writer.write_u2(item.descriptor_index);
            } else if constexpr (std::is_same_v<T, CpMethodHandle>) {
                writer.write_u1(static_cast<u1>(ConstantTag::MethodHandle));
                writer.write_u1(item.reference_kind);
                writer.write_u2(item.reference_index);
            } else if constexpr (std::is_same_v<T, CpMethodType>) {
                writer.write_u1(static_cast<u1>(ConstantTag::MethodType));
                writer.write_u2(item.descriptor_index);
            } else if constexpr (std::is_same_v<T, CpDynamic>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Dynamic));
                writer.write_u2(item.bootstrap_method_attr_index);
                writer.write_u2(item.name_and_type_index);
            } else if constexpr (std::is_same_v<T, CpInvokeDynamic>) {
                writer.write_u1(static_cast<u1>(ConstantTag::InvokeDynamic));
                writer.write_u2(item.bootstrap_method_attr_index);
                writer.write_u2(item.name_and_type_index);
            } else if constexpr (std::is_same_v<T, CpModule>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Module));
                writer.write_u2(item.name_index);
            } else if constexpr (std::is_same_v<T, CpPackage>) {
                writer.write_u1(static_cast<u1>(ConstantTag::Package));
                writer.write_u2(item.name_index);
            } else if constexpr (std::is_same_v<T, CpUnusableSlot> || std::is_same_v<T, std::monostate>) {
                // Do not serialize unusable slot or monostate
            }
        }, entry);
    }
}

} // namespace obf::bytecode
