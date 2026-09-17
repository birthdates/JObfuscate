#pragma once

#include "types.hpp"
#include "reader.hpp"
#include "writer.hpp"
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <variant>

namespace obf::bytecode {

class ConstantPool;

struct ExceptionHandler {
    u2 start_pc;
    u2 end_pc;
    u2 handler_pc;
    u2 catch_type; // 0 for any/finally
};

struct LineNumberEntry {
    u2 start_pc;
    u2 line_number;
};

struct LocalVariableEntry {
    u2 start_pc;
    u2 length;
    u2 name_index;
    u2 descriptor_index;
    u2 index;
};

struct InnerClassEntry {
    u2 inner_class_info_index;
    u2 outer_class_info_index;
    u2 inner_name_index;
    u2 inner_class_access_flags;
};

struct BootstrapMethodEntry {
    u2 bootstrap_method_ref;
    std::vector<u2> bootstrap_arguments;
};

// Forward declaration
struct Attribute;

// Specific attribute bodies
struct AttrCode {
    u2 max_stack{0};
    u2 max_locals{0};
    std::vector<u1> code;
    std::vector<ExceptionHandler> exception_table;
    std::vector<Attribute> attributes; // Sub-attributes (e.g. LineNumberTable, LocalVariableTable, StackMapTable)
};

struct AttrLineNumberTable {
    std::vector<LineNumberEntry> entries;
};

struct AttrLocalVariableTable {
    std::vector<LocalVariableEntry> entries;
};

struct AttrLocalVariableTypeTable {
    std::vector<LocalVariableEntry> entries;
};

struct AttrSourceFile {
    u2 sourcefile_index{0};
};

struct AttrSignature {
    u2 signature_index{0};
};

struct AttrExceptions {
    std::vector<u2> exception_index_table;
};

struct AttrInnerClasses {
    std::vector<InnerClassEntry> classes;
};

struct AttrBootstrapMethods {
    std::vector<BootstrapMethodEntry> bootstrap_methods;
};

// Raw/Unknown attribute (preserved as-is)
struct AttrRaw {
    std::vector<u1> data;
};

using AttributeData = std::variant<
    AttrCode,
    AttrLineNumberTable,
    AttrLocalVariableTable,
    AttrLocalVariableTypeTable,
    AttrSourceFile,
    AttrSignature,
    AttrExceptions,
    AttrInnerClasses,
    AttrBootstrapMethods,
    AttrRaw
>;

struct Attribute {
    u2 name_index{0};
    AttributeData data{AttrRaw{}};

    [[nodiscard]] std::string_view name(const ConstantPool& cp) const;
    
    template <typename T>
    [[nodiscard]] const T* get_if() const {
        return std::get_if<T>(&data);
    }

    template <typename T>
    [[nodiscard]] T* get_mut_if() {
        return std::get_if<T>(&data);
    }
};

// Parsing and serializing attributes
[[nodiscard]] Attribute parse_attribute(BinaryReader& reader, const ConstantPool& cp);
void write_attribute(BinaryWriter& writer, const Attribute& attr);

} // namespace obf::bytecode
