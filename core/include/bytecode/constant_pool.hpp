#pragma once

#include "types.hpp"
#include "reader.hpp"
#include "writer.hpp"
#include <variant>
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <memory>
#include <stdexcept>

namespace obf::bytecode {

enum class ConstantTag : u1 {
    Utf8               = 1,
    Integer            = 3,
    Float              = 4,
    Long               = 5,
    Double             = 6,
    Class              = 7,
    String             = 8,
    Fieldref           = 9,
    Methodref          = 10,
    InterfaceMethodref = 11,
    NameAndType        = 12,
    MethodHandle       = 15,
    MethodType         = 16,
    Dynamic            = 17,
    InvokeDynamic      = 18,
    Module             = 19,
    Package            = 20
};

// Constant pool entry structures
struct CpUtf8 {
    std::string value;
};

struct CpInteger {
    i4 value;
};

struct CpFloat {
    float value;
};

struct CpLong {
    i8 value;
};

struct CpDouble {
    double value;
};

struct CpClass {
    u2 name_index;
};

struct CpString {
    u2 string_index;
};

struct CpFieldref {
    u2 class_index;
    u2 name_and_type_index;
};

struct CpMethodref {
    u2 class_index;
    u2 name_and_type_index;
};

struct CpInterfaceMethodref {
    u2 class_index;
    u2 name_and_type_index;
};

struct CpNameAndType {
    u2 name_index;
    u2 descriptor_index;
};

struct CpMethodHandle {
    u1 reference_kind;
    u2 reference_index;
};

struct CpMethodType {
    u2 descriptor_index;
};

struct CpDynamic {
    u2 bootstrap_method_attr_index;
    u2 name_and_type_index;
};

struct CpInvokeDynamic {
    u2 bootstrap_method_attr_index;
    u2 name_and_type_index;
};

struct CpModule {
    u2 name_index;
};

struct CpPackage {
    u2 name_index;
};

// Placeholder for index following a Long or Double
struct CpUnusableSlot {};

using ConstantPoolEntry = std::variant<
    std::monostate,       // index 0 is unused
    CpUtf8,               // 1
    CpInteger,            // 3
    CpFloat,              // 4
    CpLong,               // 5
    CpDouble,             // 6
    CpClass,              // 7
    CpString,             // 8
    CpFieldref,           // 9
    CpMethodref,          // 10
    CpInterfaceMethodref, // 11
    CpNameAndType,        // 12
    CpMethodHandle,       // 15
    CpMethodType,         // 16
    CpDynamic,            // 17
    CpInvokeDynamic,      // 18
    CpModule,             // 19
    CpPackage,            // 20
    CpUnusableSlot        // slot after Long/Double
>;

class ConstantPool {
public:
    ConstantPool();

    [[nodiscard]] size_t size() const noexcept { return m_entries.size(); }
    [[nodiscard]] u2 count() const noexcept { return static_cast<u2>(m_entries.size()); }

    [[nodiscard]] const ConstantPoolEntry& get(u2 index) const;
    [[nodiscard]] ConstantPoolEntry& get_mut(u2 index);

    template <typename T>
    [[nodiscard]] const T* get_if(u2 index) const {
        if (index >= m_entries.size()) return nullptr;
        return std::get_if<T>(&m_entries[index]);
    }

    template <typename T>
    [[nodiscard]] T* get_mut_if(u2 index) {
        if (index >= m_entries.size()) return nullptr;
        return std::get_if<T>(&m_entries[index]);
    }

    // High level helpers
    [[nodiscard]] std::string_view get_utf8(u2 index) const;
    [[nodiscard]] std::string_view get_class_name(u2 class_index) const;
    [[nodiscard]] std::string_view get_string_value(u2 string_index) const;

    // Mutators / deduplicating inserters
    u2 add_entry(ConstantPoolEntry entry);
    u2 find_or_add_utf8(std::string_view value);
    u2 find_or_add_string(std::string_view value);
    u2 find_or_add_class(std::string_view class_name);
    u2 find_or_add_name_and_type(std::string_view name, std::string_view descriptor);
    u2 find_or_add_methodref(std::string_view class_name, std::string_view method_name, std::string_view descriptor);
    u2 find_or_add_interface_methodref(std::string_view class_name, std::string_view method_name, std::string_view descriptor);
    u2 find_or_add_fieldref(std::string_view class_name, std::string_view field_name, std::string_view descriptor);
    u2 find_or_add_integer(i4 value);
    u2 find_or_add_long(i8 value);
    u2 find_or_add_method_handle(u1 reference_kind, u2 reference_index);
    u2 find_or_add_method_type(std::string_view descriptor);
    u2 find_or_add_invoke_dynamic(u2 bsm_attr_index, std::string_view name, std::string_view descriptor);
    u2 find_or_add_dynamic(u2 bsm_attr_index, std::string_view name, std::string_view descriptor);
    [[nodiscard]] bool has_utf8(std::string_view value) const;
    void rebuild_utf8_lookup();

    // Serialization
    void read(BinaryReader& reader, u2 count);
    void write(BinaryWriter& writer) const;

private:
    std::vector<ConstantPoolEntry> m_entries;
    std::unordered_map<std::string, u2> m_utf8_lookup;
};

} // namespace obf::bytecode
