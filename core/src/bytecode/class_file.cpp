#include "bytecode/class_file.hpp"
#include "bytecode/instructions.hpp"
#include <stdexcept>

namespace obf::bytecode {

const Attribute* MemberInfo::find_attribute(std::string_view attr_name, const ConstantPool& cp) const {
    for (const auto& attr : attributes) {
        if (attr.name(cp) == attr_name) return &attr;
    }
    return nullptr;
}

Attribute* MemberInfo::find_attribute_mut(std::string_view attr_name, const ConstantPool& cp) {
    for (auto& attr : attributes) {
        if (attr.name(cp) == attr_name) return &attr;
    }
    return nullptr;
}

void MemberInfo::remove_attribute(std::string_view attr_name, const ConstantPool& cp) {
    std::erase_if(attributes, [&](const Attribute& a) {
        return a.name(cp) == attr_name;
    });
}

const AttrCode* MethodInfo::code() const {
    for (const auto& attr : attributes) {
        if (const auto* c = attr.get_if<AttrCode>()) return c;
    }
    return nullptr;
}

AttrCode* MethodInfo::code_mut() {
    for (auto& attr : attributes) {
        if (auto* c = attr.get_mut_if<AttrCode>()) return c;
    }
    return nullptr;
}

ClassFile ClassFile::from_bytes(std::span<const u1> data) {
    BinaryReader reader(data);
    ClassFile cf;
    cf.read(reader);
    return cf;
}

std::vector<u1> ClassFile::to_bytes() const {
    BinaryWriter writer;
    write(writer);
    return std::move(writer.data());
}

void ClassFile::read(BinaryReader& reader) {
    m_magic = reader.read_u4();
    if (m_magic != JVM_CLASS_MAGIC) {
        throw std::runtime_error("Invalid JVM class magic: 0x" + std::to_string(m_magic));
    }

    m_minor_version = reader.read_u2();
    m_major_version = reader.read_u2();

    u2 cp_count = reader.read_u2();
    m_constant_pool.read(reader, cp_count);

    m_access_flags = reader.read_u2();
    m_this_class = reader.read_u2();
    m_super_class = reader.read_u2();

    u2 ifaces_count = reader.read_u2();
    m_interfaces.clear();
    m_interfaces.reserve(ifaces_count);
    for (u2 i = 0; i < ifaces_count; ++i) {
        m_interfaces.push_back(reader.read_u2());
    }

    u2 fields_count = reader.read_u2();
    m_fields.clear();
    m_fields.reserve(fields_count);
    for (u2 i = 0; i < fields_count; ++i) {
        FieldInfo f;
        f.access_flags = reader.read_u2();
        f.name_index = reader.read_u2();
        f.descriptor_index = reader.read_u2();
        u2 attr_count = reader.read_u2();
        f.attributes.reserve(attr_count);
        for (u2 a = 0; a < attr_count; ++a) {
            f.attributes.push_back(parse_attribute(reader, m_constant_pool));
        }
        m_fields.push_back(std::move(f));
    }

    u2 methods_count = reader.read_u2();
    m_methods.clear();
    m_methods.reserve(methods_count);
    for (u2 i = 0; i < methods_count; ++i) {
        MethodInfo m;
        m.access_flags = reader.read_u2();
        m.name_index = reader.read_u2();
        m.descriptor_index = reader.read_u2();
        u2 attr_count = reader.read_u2();
        m.attributes.reserve(attr_count);
        for (u2 a = 0; a < attr_count; ++a) {
            m.attributes.push_back(parse_attribute(reader, m_constant_pool));
        }
        m_methods.push_back(std::move(m));
    }

    u2 attributes_count = reader.read_u2();
    m_attributes.clear();
    m_attributes.reserve(attributes_count);
    for (u2 a = 0; a < attributes_count; ++a) {
        m_attributes.push_back(parse_attribute(reader, m_constant_pool));
    }
}

void ClassFile::write(BinaryWriter& writer) const {
    writer.write_u4(m_magic);
    writer.write_u2(m_minor_version);
    writer.write_u2(m_major_version);

    writer.write_u2(m_constant_pool.count());
    m_constant_pool.write(writer);

    writer.write_u2(m_access_flags);
    writer.write_u2(m_this_class);
    writer.write_u2(m_super_class);

    writer.write_u2(static_cast<u2>(m_interfaces.size()));
    for (u2 iface : m_interfaces) {
        writer.write_u2(iface);
    }

    writer.write_u2(static_cast<u2>(m_fields.size()));
    for (const auto& f : m_fields) {
        writer.write_u2(f.access_flags);
        writer.write_u2(f.name_index);
        writer.write_u2(f.descriptor_index);
        writer.write_u2(static_cast<u2>(f.attributes.size()));
        for (const auto& attr : f.attributes) {
            write_attribute(writer, attr);
        }
    }

    writer.write_u2(static_cast<u2>(m_methods.size()));
    for (const auto& m : m_methods) {
        writer.write_u2(m.access_flags);
        writer.write_u2(m.name_index);
        writer.write_u2(m.descriptor_index);
        writer.write_u2(static_cast<u2>(m.attributes.size()));
        for (const auto& attr : m.attributes) {
            write_attribute(writer, attr);
        }
    }

    writer.write_u2(static_cast<u2>(m_attributes.size()));
    for (const auto& attr : m_attributes) {
        write_attribute(writer, attr);
    }
}

std::string_view ClassFile::name() const {
    return m_constant_pool.get_class_name(m_this_class);
}

std::string_view ClassFile::super_name() const {
    if (m_super_class == 0) return "";
    return m_constant_pool.get_class_name(m_super_class);
}

MethodInfo* ClassFile::find_method(std::string_view name, std::string_view desc) {
    for (auto& m : m_methods) {
        if (m.name(m_constant_pool) == name && m.descriptor(m_constant_pool) == desc) {
            return &m;
        }
    }
    return nullptr;
}

const MethodInfo* ClassFile::find_method(std::string_view name, std::string_view desc) const {
    for (const auto& m : m_methods) {
        if (m.name(m_constant_pool) == name && m.descriptor(m_constant_pool) == desc) {
            return &m;
        }
    }
    return nullptr;
}

FieldInfo* ClassFile::find_field(std::string_view name, std::string_view desc) {
    for (auto& f : m_fields) {
        if (f.name(m_constant_pool) == name && f.descriptor(m_constant_pool) == desc) {
            return &f;
        }
    }
    return nullptr;
}

const FieldInfo* ClassFile::find_field(std::string_view name, std::string_view desc) const {
    for (const auto& f : m_fields) {
        if (f.name(m_constant_pool) == name && f.descriptor(m_constant_pool) == desc) {
            return &f;
        }
    }
    return nullptr;
}

const Attribute* ClassFile::find_attribute(std::string_view attr_name) const {
    for (const auto& attr : m_attributes) {
        if (attr.name(m_constant_pool) == attr_name) return &attr;
    }
    return nullptr;
}

Attribute* ClassFile::find_attribute_mut(std::string_view attr_name) {
    for (auto& attr : m_attributes) {
        if (attr.name(m_constant_pool) == attr_name) return &attr;
    }
    return nullptr;
}

void ClassFile::remove_attribute(std::string_view attr_name) {
    std::erase_if(m_attributes, [&](const Attribute& a) {
        return a.name(m_constant_pool) == attr_name;
    });
}

void ClassFile::sanitize_unused_constants() {
    auto& cp = m_constant_pool;
    if (cp.size() <= 1) return;

    std::vector<bool> used(cp.size(), false);

    auto mark_idx = [&](u2 idx) {
        if (idx > 0 && idx < cp.size()) {
            used[idx] = true;
        }
    };

    // 1. Class-level roots
    mark_idx(m_this_class);
    if (m_super_class != 0) mark_idx(m_super_class);
    for (u2 iface : m_interfaces) mark_idx(iface);

    // Lambda to scan an attribute for CP references
    auto scan_attribute = [&](const Attribute& attr, auto& self) -> void {
        mark_idx(attr.name_index);
        std::string_view attr_name = attr.name(cp);

        if (const auto* code = attr.get_if<AttrCode>()) {
            auto instrs = bytecode::disassemble(code->code);
            for (const auto& inst : instrs) {
                if (const auto* cp_op = std::get_if<CpIndexOperand>(&inst.operand)) {
                    mark_idx(cp_op->index);
                } else if (const auto* cpw_op = std::get_if<CpIndexWideOperand>(&inst.operand)) {
                    mark_idx(cpw_op->index);
                } else if (const auto* ic_op = std::get_if<InterfaceCallOperand>(&inst.operand)) {
                    mark_idx(ic_op->index);
                } else if (const auto* id_op = std::get_if<InvokeDynamicOperand>(&inst.operand)) {
                    mark_idx(id_op->index);
                } else if (const auto* mana_op = std::get_if<MultiANewArrayOperand>(&inst.operand)) {
                    mark_idx(mana_op->index);
                }
            }
            for (const auto& handler : code->exception_table) {
                if (handler.catch_type != 0) {
                    mark_idx(handler.catch_type);
                }
            }
            for (const auto& sub_attr : code->attributes) {
                self(sub_attr, self);
            }
        } else if (const auto* lvt = attr.get_if<AttrLocalVariableTable>()) {
            for (const auto& e : lvt->entries) {
                mark_idx(e.name_index);
                mark_idx(e.descriptor_index);
            }
        } else if (const auto* lvtt = attr.get_if<AttrLocalVariableTypeTable>()) {
            for (const auto& e : lvtt->entries) {
                mark_idx(e.name_index);
                mark_idx(e.descriptor_index);
            }
        } else if (const auto* sf = attr.get_if<AttrSourceFile>()) {
            mark_idx(sf->sourcefile_index);
        } else if (const auto* sig = attr.get_if<AttrSignature>()) {
            mark_idx(sig->signature_index);
        } else if (const auto* ex = attr.get_if<AttrExceptions>()) {
            for (u2 e_idx : ex->exception_index_table) {
                mark_idx(e_idx);
            }
        } else if (const auto* ic = attr.get_if<AttrInnerClasses>()) {
            for (const auto& e : ic->classes) {
                if (e.inner_class_info_index != 0) mark_idx(e.inner_class_info_index);
                if (e.outer_class_info_index != 0) mark_idx(e.outer_class_info_index);
                if (e.inner_name_index != 0) mark_idx(e.inner_name_index);
            }
        } else if (const auto* bsm = attr.get_if<AttrBootstrapMethods>()) {
            for (const auto& e : bsm->bootstrap_methods) {
                mark_idx(e.bootstrap_method_ref);
                for (u2 arg : e.bootstrap_arguments) {
                    mark_idx(arg);
                }
            }
        } else if (const auto* raw = attr.get_if<AttrRaw>()) {
            if (attr_name == "StackMapTable") {
                const auto& bytes = raw->data;
                size_t off = 0;
                if (bytes.size() >= 2) {
                    u2 num_frames = (static_cast<u2>(bytes[off]) << 8) | bytes[off + 1];
                    off += 2;
                    auto scan_verification_type = [&](size_t& o) {
                        if (o >= bytes.size()) return;
                        u1 tag = bytes[o++];
                        if (tag == 7) { // Object_variable_info
                            if (o + 2 <= bytes.size()) {
                                u2 cpidx = (static_cast<u2>(bytes[o]) << 8) | bytes[o + 1];
                                mark_idx(cpidx);
                                o += 2;
                            }
                        } else if (tag == 8) { // Uninitialized_variable_info
                            o += 2;
                        }
                    };
                    for (u2 f = 0; f < num_frames && off < bytes.size(); ++f) {
                        u1 frame_type = bytes[off++];
                        if (frame_type <= 63) {
                            // same_frame
                        } else if (frame_type <= 127) {
                            // same_locals_1_stack_item_frame
                            scan_verification_type(off);
                        } else if (frame_type == 247) {
                            // same_locals_1_stack_item_frame_extended
                            off += 2;
                            scan_verification_type(off);
                        } else if (frame_type >= 248 && frame_type <= 250) {
                            // chop_frame
                            off += 2;
                        } else if (frame_type == 251) {
                            // same_frame_extended
                            off += 2;
                        } else if (frame_type >= 252 && frame_type <= 254) {
                            // append_frame
                            off += 2;
                            u2 k = frame_type - 251;
                            for (u2 i = 0; i < k; ++i) scan_verification_type(off);
                        } else if (frame_type == 255) {
                            // full_frame
                            off += 2;
                            if (off + 2 <= bytes.size()) {
                                u2 num_locals = (static_cast<u2>(bytes[off]) << 8) | bytes[off + 1];
                                off += 2;
                                for (u2 i = 0; i < num_locals; ++i) scan_verification_type(off);
                            }
                            if (off + 2 <= bytes.size()) {
                                u2 num_stack = (static_cast<u2>(bytes[off]) << 8) | bytes[off + 1];
                                off += 2;
                                for (u2 i = 0; i < num_stack; ++i) scan_verification_type(off);
                            }
                        }
                    }
                }
            } else if (attr_name.find("Annotation") != std::string_view::npos) {
                const auto& bytes = raw->data;
                for (size_t o = 0; o + 2 <= bytes.size(); ++o) {
                    u2 val = (static_cast<u2>(bytes[o]) << 8) | bytes[o + 1];
                    if (val > 0 && val < cp.size()) {
                        if (cp.get_if<CpUtf8>(val) || cp.get_if<CpClass>(val) || cp.get_if<CpString>(val) ||
                            cp.get_if<CpInteger>(val) || cp.get_if<CpLong>(val) || cp.get_if<CpFloat>(val) || cp.get_if<CpDouble>(val)) {
                            mark_idx(val);
                        }
                    }
                }
            } else if (attr_name == "ConstantValue") {
                if (raw->data.size() >= 2) {
                    u2 cpidx = (static_cast<u2>(raw->data[0]) << 8) | raw->data[1];
                    mark_idx(cpidx);
                }
            }
        }
    };

    // 2. Field roots
    for (const auto& field : m_fields) {
        mark_idx(field.name_index);
        mark_idx(field.descriptor_index);
        for (const auto& attr : field.attributes) {
            scan_attribute(attr, scan_attribute);
        }
    }

    // 3. Method roots
    for (const auto& method : m_methods) {
        mark_idx(method.name_index);
        mark_idx(method.descriptor_index);
        for (const auto& attr : method.attributes) {
            scan_attribute(attr, scan_attribute);
        }
    }

    // 4. Class attribute roots
    for (const auto& attr : m_attributes) {
        scan_attribute(attr, scan_attribute);
    }

    // 5. Transitive closure of referenced constant pool entries
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 1; i < cp.size(); ++i) {
            if (!used[i]) continue;
            u2 idx = static_cast<u2>(i);
            if (const auto* c = cp.get_if<CpClass>(idx)) {
                if (!used[c->name_index]) { mark_idx(c->name_index); changed = true; }
            } else if (const auto* s = cp.get_if<CpString>(idx)) {
                if (!used[s->string_index]) { mark_idx(s->string_index); changed = true; }
            } else if (const auto* fr = cp.get_if<CpFieldref>(idx)) {
                if (!used[fr->class_index]) { mark_idx(fr->class_index); changed = true; }
                if (!used[fr->name_and_type_index]) { mark_idx(fr->name_and_type_index); changed = true; }
            } else if (const auto* mr = cp.get_if<CpMethodref>(idx)) {
                if (!used[mr->class_index]) { mark_idx(mr->class_index); changed = true; }
                if (!used[mr->name_and_type_index]) { mark_idx(mr->name_and_type_index); changed = true; }
            } else if (const auto* imr = cp.get_if<CpInterfaceMethodref>(idx)) {
                if (!used[imr->class_index]) { mark_idx(imr->class_index); changed = true; }
                if (!used[imr->name_and_type_index]) { mark_idx(imr->name_and_type_index); changed = true; }
            } else if (const auto* nat = cp.get_if<CpNameAndType>(idx)) {
                if (!used[nat->name_index]) { mark_idx(nat->name_index); changed = true; }
                if (!used[nat->descriptor_index]) { mark_idx(nat->descriptor_index); changed = true; }
            } else if (const auto* mh = cp.get_if<CpMethodHandle>(idx)) {
                if (!used[mh->reference_index]) { mark_idx(mh->reference_index); changed = true; }
            } else if (const auto* mt = cp.get_if<CpMethodType>(idx)) {
                if (!used[mt->descriptor_index]) { mark_idx(mt->descriptor_index); changed = true; }
            } else if (const auto* dyn = cp.get_if<CpDynamic>(idx)) {
                if (!used[dyn->name_and_type_index]) { mark_idx(dyn->name_and_type_index); changed = true; }
            } else if (const auto* indy = cp.get_if<CpInvokeDynamic>(idx)) {
                if (!used[indy->name_and_type_index]) { mark_idx(indy->name_and_type_index); changed = true; }
            } else if (const auto* mod = cp.get_if<CpModule>(idx)) {
                if (!used[mod->name_index]) { mark_idx(mod->name_index); changed = true; }
            } else if (const auto* pkg = cp.get_if<CpPackage>(idx)) {
                if (!used[pkg->name_index]) { mark_idx(pkg->name_index); changed = true; }
            }
        }
    }

    // 6. Find or add java/lang/Object
    u2 obj_utf8_idx = cp.find_or_add_utf8("java/lang/Object");

    // 7. Sanitize unused class entries to prevent decompiler import pollution
    for (size_t i = 1; i < used.size(); ++i) {
        if (used[i]) continue;
        u2 idx = static_cast<u2>(i);
        if (auto* c = cp.get_mut_if<CpClass>(idx)) {
            c->name_index = obj_utf8_idx;
        }
    }

    cp.rebuild_utf8_lookup();
}

} // namespace obf::bytecode
