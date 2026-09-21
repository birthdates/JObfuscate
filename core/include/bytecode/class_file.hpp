#pragma once

#include "types.hpp"
#include "constant_pool.hpp"
#include "attributes.hpp"
#include "reader.hpp"
#include "writer.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <optional>
#include <span>

namespace obf::bytecode {

struct MemberInfo {
    u2 access_flags{0};
    u2 name_index{0};
    u2 descriptor_index{0};
    std::vector<Attribute> attributes;

    [[nodiscard]] std::string_view name(const ConstantPool& cp) const {
        return cp.get_utf8(name_index);
    }

    [[nodiscard]] std::string_view descriptor(const ConstantPool& cp) const {
        return cp.get_utf8(descriptor_index);
    }

    [[nodiscard]] bool is_public() const noexcept { return (access_flags & access_flags::ACC_PUBLIC) != 0; }
    [[nodiscard]] bool is_private() const noexcept { return (access_flags & access_flags::ACC_PRIVATE) != 0; }
    [[nodiscard]] bool is_protected() const noexcept { return (access_flags & access_flags::ACC_PROTECTED) != 0; }
    [[nodiscard]] bool is_static() const noexcept { return (access_flags & access_flags::ACC_STATIC) != 0; }
    [[nodiscard]] bool is_final() const noexcept { return (access_flags & access_flags::ACC_FINAL) != 0; }
    [[nodiscard]] bool is_synthetic() const noexcept { return (access_flags & access_flags::ACC_SYNTHETIC) != 0; }
    [[nodiscard]] bool is_native() const noexcept { return (access_flags & access_flags::ACC_NATIVE) != 0; }
    [[nodiscard]] bool is_abstract() const noexcept { return (access_flags & access_flags::ACC_ABSTRACT) != 0; }

    [[nodiscard]] const Attribute* find_attribute(std::string_view attr_name, const ConstantPool& cp) const;
    [[nodiscard]] Attribute* find_attribute_mut(std::string_view attr_name, const ConstantPool& cp);
    void remove_attribute(std::string_view attr_name, const ConstantPool& cp);
};

struct FieldInfo : public MemberInfo {};

struct MethodInfo : public MemberInfo {
    [[nodiscard]] const AttrCode* code() const;
    [[nodiscard]] AttrCode* code_mut();
};

class ClassFile {
public:
    ClassFile() = default;

    // Factory methods
    [[nodiscard]] static ClassFile from_bytes(std::span<const u1> data);
    [[nodiscard]] std::vector<u1> to_bytes() const;

    // Deserialization / Serialization
    void read(BinaryReader& reader);
    void write(BinaryWriter& writer) const;

    // Accessors
    [[nodiscard]] u4 magic() const noexcept { return m_magic; }
    [[nodiscard]] u2 minor_version() const noexcept { return m_minor_version; }
    [[nodiscard]] u2 major_version() const noexcept { return m_major_version; }
    [[nodiscard]] u2 access_flags() const noexcept { return m_access_flags; }
    [[nodiscard]] bool is_interface() const noexcept { return (m_access_flags & access_flags::ACC_INTERFACE) != 0; }
    [[nodiscard]] bool is_annotation() const noexcept { return (m_access_flags & access_flags::ACC_ANNOTATION) != 0; }
    [[nodiscard]] u2 this_class_index() const noexcept { return m_this_class; }
    [[nodiscard]] u2 super_class_index() const noexcept { return m_super_class; }

    [[nodiscard]] const ConstantPool& constant_pool() const noexcept { return m_constant_pool; }
    [[nodiscard]] ConstantPool& constant_pool() noexcept { return m_constant_pool; }

    [[nodiscard]] const std::vector<u2>& interfaces() const noexcept { return m_interfaces; }
    [[nodiscard]] std::vector<u2>& interfaces() noexcept { return m_interfaces; }

    [[nodiscard]] const std::vector<FieldInfo>& fields() const noexcept { return m_fields; }
    [[nodiscard]] std::vector<FieldInfo>& fields() noexcept { return m_fields; }

    [[nodiscard]] const std::vector<MethodInfo>& methods() const noexcept { return m_methods; }
    [[nodiscard]] std::vector<MethodInfo>& methods() noexcept { return m_methods; }

    [[nodiscard]] const std::vector<Attribute>& attributes() const noexcept { return m_attributes; }
    [[nodiscard]] std::vector<Attribute>& attributes() noexcept { return m_attributes; }

    // High level helpers
    [[nodiscard]] std::string_view name() const;
    [[nodiscard]] std::string_view super_name() const;
    
    [[nodiscard]] MethodInfo* find_method(std::string_view name, std::string_view desc);
    [[nodiscard]] const MethodInfo* find_method(std::string_view name, std::string_view desc) const;

    [[nodiscard]] FieldInfo* find_field(std::string_view name, std::string_view desc);
    [[nodiscard]] const FieldInfo* find_field(std::string_view name, std::string_view desc) const;

    [[nodiscard]] const Attribute* find_attribute(std::string_view attr_name) const;
    [[nodiscard]] Attribute* find_attribute_mut(std::string_view attr_name);
    void remove_attribute(std::string_view attr_name);
    void sanitize_unused_constants();

    void set_access_flags(u2 flags) noexcept { m_access_flags = flags; }
    void set_version(u2 major, u2 minor = 0) noexcept { m_major_version = major; m_minor_version = minor; }
    void set_this_class(u2 index) noexcept { m_this_class = index; }
    void set_super_class(u2 index) noexcept { m_super_class = index; }

private:
    u4 m_magic{JVM_CLASS_MAGIC};
    u2 m_minor_version{0};
    u2 m_major_version{static_cast<u2>(JavaMajorVersion::Java8)};
    ConstantPool m_constant_pool;
    u2 m_access_flags{0};
    u2 m_this_class{0};
    u2 m_super_class{0};
    std::vector<u2> m_interfaces;
    std::vector<FieldInfo> m_fields;
    std::vector<MethodInfo> m_methods;
    std::vector<Attribute> m_attributes;
};

} // namespace obf::bytecode
