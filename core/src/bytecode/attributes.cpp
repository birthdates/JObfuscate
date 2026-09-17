#include "bytecode/attributes.hpp"
#include "bytecode/constant_pool.hpp"
#include <stdexcept>

namespace obf::bytecode {

std::string_view Attribute::name(const ConstantPool& cp) const {
    return cp.get_utf8(name_index);
}

Attribute parse_attribute(BinaryReader& reader, const ConstantPool& cp) {
    u2 name_idx = reader.read_u2();
    u4 length = reader.read_u4();
    std::string_view name = cp.get_utf8(name_idx);

    Attribute attr;
    attr.name_index = name_idx;

    if (name == "Code") {
        AttrCode code;
        code.max_stack = reader.read_u2();
        code.max_locals = reader.read_u2();
        u4 code_length = reader.read_u4();
        code.code = reader.read_bytes(code_length);

        u2 ex_count = reader.read_u2();
        code.exception_table.reserve(ex_count);
        for (u2 i = 0; i < ex_count; ++i) {
            ExceptionHandler h;
            h.start_pc = reader.read_u2();
            h.end_pc = reader.read_u2();
            h.handler_pc = reader.read_u2();
            h.catch_type = reader.read_u2();
            code.exception_table.push_back(h);
        }

        u2 sub_count = reader.read_u2();
        code.attributes.reserve(sub_count);
        for (u2 i = 0; i < sub_count; ++i) {
            code.attributes.push_back(parse_attribute(reader, cp));
        }

        attr.data = std::move(code);
    } else if (name == "LineNumberTable") {
        AttrLineNumberTable lnt;
        u2 count = reader.read_u2();
        lnt.entries.reserve(count);
        for (u2 i = 0; i < count; ++i) {
            LineNumberEntry e;
            e.start_pc = reader.read_u2();
            e.line_number = reader.read_u2();
            lnt.entries.push_back(e);
        }
        attr.data = std::move(lnt);
    } else if (name == "LocalVariableTable") {
        AttrLocalVariableTable lvt;
        u2 count = reader.read_u2();
        lvt.entries.reserve(count);
        for (u2 i = 0; i < count; ++i) {
            LocalVariableEntry e;
            e.start_pc = reader.read_u2();
            e.length = reader.read_u2();
            e.name_index = reader.read_u2();
            e.descriptor_index = reader.read_u2();
            e.index = reader.read_u2();
            lvt.entries.push_back(e);
        }
        attr.data = std::move(lvt);
    } else if (name == "LocalVariableTypeTable") {
        AttrLocalVariableTypeTable lvtt;
        u2 count = reader.read_u2();
        lvtt.entries.reserve(count);
        for (u2 i = 0; i < count; ++i) {
            LocalVariableEntry e;
            e.start_pc = reader.read_u2();
            e.length = reader.read_u2();
            e.name_index = reader.read_u2();
            e.descriptor_index = reader.read_u2();
            e.index = reader.read_u2();
            lvtt.entries.push_back(e);
        }
        attr.data = std::move(lvtt);
    } else if (name == "SourceFile") {
        AttrSourceFile sf;
        sf.sourcefile_index = reader.read_u2();
        attr.data = sf;
    } else if (name == "Signature") {
        AttrSignature sig;
        sig.signature_index = reader.read_u2();
        attr.data = sig;
    } else if (name == "Exceptions") {
        AttrExceptions ex;
        u2 count = reader.read_u2();
        ex.exception_index_table.reserve(count);
        for (u2 i = 0; i < count; ++i) {
            ex.exception_index_table.push_back(reader.read_u2());
        }
        attr.data = std::move(ex);
    } else if (name == "InnerClasses") {
        AttrInnerClasses ic;
        u2 count = reader.read_u2();
        ic.classes.reserve(count);
        for (u2 i = 0; i < count; ++i) {
            InnerClassEntry e;
            e.inner_class_info_index = reader.read_u2();
            e.outer_class_info_index = reader.read_u2();
            e.inner_name_index = reader.read_u2();
            e.inner_class_access_flags = reader.read_u2();
            ic.classes.push_back(e);
        }
        attr.data = std::move(ic);
    } else if (name == "BootstrapMethods") {
        AttrBootstrapMethods bsm;
        u2 count = reader.read_u2();
        bsm.bootstrap_methods.reserve(count);
        for (u2 i = 0; i < count; ++i) {
            BootstrapMethodEntry e;
            e.bootstrap_method_ref = reader.read_u2();
            u2 arg_count = reader.read_u2();
            e.bootstrap_arguments.reserve(arg_count);
            for (u2 a = 0; a < arg_count; ++a) {
                e.bootstrap_arguments.push_back(reader.read_u2());
            }
            bsm.bootstrap_methods.push_back(std::move(e));
        }
        attr.data = std::move(bsm);
    } else {
        // Preserves any standard or newer Java 8->25 attributes as raw bytes (e.g. Record, NestHost, PermittedSubclasses, StackMapTable)
        AttrRaw raw;
        raw.data = reader.read_bytes(length);
        attr.data = std::move(raw);
    }

    return attr;
}

void write_attribute(BinaryWriter& writer, const Attribute& attr) {
    writer.write_u2(attr.name_index);
    size_t length_offset = writer.size();
    writer.write_u4(0); // placeholder for attribute_length
    size_t start_data = writer.size();

    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, AttrCode>) {
            writer.write_u2(item.max_stack);
            writer.write_u2(item.max_locals);
            writer.write_u4(static_cast<u4>(item.code.size()));
            writer.write_bytes(item.code);

            writer.write_u2(static_cast<u2>(item.exception_table.size()));
            for (const auto& ex : item.exception_table) {
                writer.write_u2(ex.start_pc);
                writer.write_u2(ex.end_pc);
                writer.write_u2(ex.handler_pc);
                writer.write_u2(ex.catch_type);
            }

            writer.write_u2(static_cast<u2>(item.attributes.size()));
            for (const auto& sub_attr : item.attributes) {
                write_attribute(writer, sub_attr);
            }
        } else if constexpr (std::is_same_v<T, AttrLineNumberTable>) {
            writer.write_u2(static_cast<u2>(item.entries.size()));
            for (const auto& e : item.entries) {
                writer.write_u2(e.start_pc);
                writer.write_u2(e.line_number);
            }
        } else if constexpr (std::is_same_v<T, AttrLocalVariableTable>) {
            writer.write_u2(static_cast<u2>(item.entries.size()));
            for (const auto& e : item.entries) {
                writer.write_u2(e.start_pc);
                writer.write_u2(e.length);
                writer.write_u2(e.name_index);
                writer.write_u2(e.descriptor_index);
                writer.write_u2(e.index);
            }
        } else if constexpr (std::is_same_v<T, AttrLocalVariableTypeTable>) {
            writer.write_u2(static_cast<u2>(item.entries.size()));
            for (const auto& e : item.entries) {
                writer.write_u2(e.start_pc);
                writer.write_u2(e.length);
                writer.write_u2(e.name_index);
                writer.write_u2(e.descriptor_index);
                writer.write_u2(e.index);
            }
        } else if constexpr (std::is_same_v<T, AttrSourceFile>) {
            writer.write_u2(item.sourcefile_index);
        } else if constexpr (std::is_same_v<T, AttrSignature>) {
            writer.write_u2(item.signature_index);
        } else if constexpr (std::is_same_v<T, AttrExceptions>) {
            writer.write_u2(static_cast<u2>(item.exception_index_table.size()));
            for (u2 idx : item.exception_index_table) {
                writer.write_u2(idx);
            }
        } else if constexpr (std::is_same_v<T, AttrInnerClasses>) {
            writer.write_u2(static_cast<u2>(item.classes.size()));
            for (const auto& e : item.classes) {
                writer.write_u2(e.inner_class_info_index);
                writer.write_u2(e.outer_class_info_index);
                writer.write_u2(e.inner_name_index);
                writer.write_u2(e.inner_class_access_flags);
            }
        } else if constexpr (std::is_same_v<T, AttrBootstrapMethods>) {
            writer.write_u2(static_cast<u2>(item.bootstrap_methods.size()));
            for (const auto& e : item.bootstrap_methods) {
                writer.write_u2(e.bootstrap_method_ref);
                writer.write_u2(static_cast<u2>(e.bootstrap_arguments.size()));
                for (u2 arg : e.bootstrap_arguments) {
                    writer.write_u2(arg);
                }
            }
        } else if constexpr (std::is_same_v<T, AttrRaw>) {
            writer.write_bytes(item.data);
        }
    }, attr.data);

    u4 length = static_cast<u4>(writer.size() - start_data);
    writer.patch_u4(length_offset, length);
}

} // namespace obf::bytecode
