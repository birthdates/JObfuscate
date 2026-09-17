#include "passes/rename_pass.hpp"
#include "jar/jar_archive.hpp"
#include "bytecode/instructions.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace obf::passes {

static bool is_reserved_method_name(std::string_view name, std::string_view desc) {
    if (name == "<init>" || name == "<clinit>") return true;
    if (name == "main" && desc == "([Ljava/lang/String;)V") return true;
    if (name == "toString" && desc == "()Ljava/lang/String;") return true;
    if (name == "hashCode" && desc == "()I") return true;
    if (name == "equals" && desc == "(Ljava/lang/Object;)Z") return true;
    if (name == "clone" && desc == "()Ljava/lang/Object;") return true;
    if (name == "values" || name == "valueOf") return true;
    if (name == "annotationType" && desc == "()Ljava/lang/Class;") return true;
    if (name.starts_with("_proxy_") || name.starts_with("_cat_")) return true;
    return false;
}

static std::string remap_descriptor(std::string_view desc, const std::unordered_map<std::string, std::string>& class_renames) {
    if (class_renames.empty()) return std::string(desc);
    std::string res;
    res.reserve(desc.size());
    size_t i = 0;
    while (i < desc.size()) {
        if (desc[i] == 'L') {
            size_t end = desc.find_first_of(";<(.", i + 1);
            if (end != std::string_view::npos && (desc[end] == ';' || desc[end] == '<')) {
                std::string cls_name = std::string(desc.substr(i + 1, end - i - 1));
                auto it = class_renames.find(cls_name);
                res += 'L';
                if (it != class_renames.end()) {
                    res += it->second;
                } else {
                    res += cls_name;
                }
                i = end;
                continue;
            }
        }
        res += desc[i++];
    }
    return res;
}

static uint64_t splitmix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

static std::string resolve_method_rename(const std::string& class_name,
                                         const std::string& method_name,
                                         const std::string& method_desc,
                                         const PassContext& ctx,
                                         std::unordered_set<std::string>& visited) {
    if (class_name.empty() || class_name == "java/lang/Object") return "";
    if (visited.count(class_name)) return "";
    visited.insert(class_name);

    std::string key = class_name + "." + method_name + method_desc;
    if (auto it = ctx.method_renames.find(key); it != ctx.method_renames.end()) {
        return it->second;
    }

    const auto* node = ctx.hierarchy.get_node(class_name);
    if (!node) return "";

    // Check superclass
    if (!node->super_class.empty()) {
        std::string res = resolve_method_rename(node->super_class, method_name, method_desc, ctx, visited);
        if (!res.empty()) return res;
    }

    // Check interfaces
    for (const auto& iface : node->interfaces) {
        std::string res = resolve_method_rename(iface, method_name, method_desc, ctx, visited);
        if (!res.empty()) return res;
    }

    // Check subclasses
    for (const auto& sub : node->subclasses) {
        std::string res = resolve_method_rename(sub, method_name, method_desc, ctx, visited);
        if (!res.empty()) return res;
    }

    return "";
}

static std::string resolve_method_rename(const std::string& class_name,
                                         const std::string& method_name,
                                         const std::string& method_desc,
                                         const PassContext& ctx) {
    std::unordered_set<std::string> visited;
    return resolve_method_rename(class_name, method_name, method_desc, ctx, visited);
}

static std::string resolve_field_rename(const std::string& class_name,
                                        const std::string& field_name,
                                        const PassContext& ctx,
                                        std::unordered_set<std::string>& visited) {
    if (class_name.empty() || class_name == "java/lang/Object") return "";
    if (visited.count(class_name)) return "";
    visited.insert(class_name);

    std::string key = class_name + "." + field_name;
    if (auto it = ctx.field_renames.find(key); it != ctx.field_renames.end()) {
        return it->second;
    }

    const auto* node = ctx.hierarchy.get_node(class_name);
    if (!node) return "";

    if (!node->super_class.empty()) {
        std::string res = resolve_field_rename(node->super_class, field_name, ctx, visited);
        if (!res.empty()) return res;
    }

    for (const auto& iface : node->interfaces) {
        std::string res = resolve_field_rename(iface, field_name, ctx, visited);
        if (!res.empty()) return res;
    }

    return "";
}

static std::string resolve_field_rename(const std::string& class_name,
                                        const std::string& field_name,
                                        const PassContext& ctx) {
    std::unordered_set<std::string> visited;
    return resolve_field_rename(class_name, field_name, ctx, visited);
}

static void assign_method_name_to_hierarchy(const std::string& class_name,
                                            const std::string& method_name,
                                            const std::string& method_desc,
                                            const std::string& obf_name,
                                            PassContext& ctx,
                                            std::unordered_set<std::string>& visited) {
    if (class_name.empty() || class_name == "java/lang/Object") return;
    if (class_name.starts_with("java/") || class_name.starts_with("javax/") || class_name.starts_with("jdk/")) return;
    if (visited.count(class_name)) return;
    visited.insert(class_name);

    std::string key = class_name + "." + method_name + method_desc;
    ctx.method_renames[key] = obf_name;

    const auto* node = ctx.hierarchy.get_node(class_name);
    if (!node) return;

    if (!node->super_class.empty()) {
        assign_method_name_to_hierarchy(node->super_class, method_name, method_desc, obf_name, ctx, visited);
    }
    for (const auto& iface : node->interfaces) {
        assign_method_name_to_hierarchy(iface, method_name, method_desc, obf_name, ctx, visited);
    }
    for (const auto& sub : node->subclasses) {
        assign_method_name_to_hierarchy(sub, method_name, method_desc, obf_name, ctx, visited);
    }
}

static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t len = data.size();
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b0 = data[i];
        uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0;
        uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0;
        uint32_t n = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(tbl[(n >> 18) & 0x3F]);
        out.push_back(tbl[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < len) ? tbl[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < len) ? tbl[n & 0x3F] : '=');
    }
    return out;
}

static bytecode::u2 get_or_create_ann_decryptor(bytecode::ClassFile& cls) {
    auto& cp = cls.constant_pool();
    std::string current_class(cls.name());
    std::string helper_name = "_dec_ann";

    if (cls.find_method(helper_name, "(Ljava/lang/String;)Ljava/lang/String;")) {
        return cp.find_or_add_methodref(current_class, helper_name, "(Ljava/lang/String;)Ljava/lang/String;");
    }

    bytecode::MethodInfo m;
    m.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                     bytecode::access_flags::ACC_STATIC | 
                     bytecode::access_flags::ACC_SYNTHETIC;
    m.name_index = cp.find_or_add_utf8(helper_name);
    m.descriptor_index = cp.find_or_add_utf8("(Ljava/lang/String;)Ljava/lang/String;");

    std::vector<bytecode::Instruction> instrs;
    bytecode::u2 str_cls = cp.find_or_add_class("java/lang/String");
    instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::new_op, .offset = 0, .operand = bytecode::CpIndexOperand{str_cls}});
    instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::dup, .offset = 1, .operand = bytecode::NoOperand{}});
    bytecode::u2 get_dec = cp.find_or_add_methodref("java/util/Base64", "getDecoder", "()Ljava/util/Base64$Decoder;");
    instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::invokestatic, .offset = 2, .operand = bytecode::CpIndexOperand{get_dec}});
    instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::aload_0, .offset = 3, .operand = bytecode::NoOperand{}});
    bytecode::u2 b64_decode = cp.find_or_add_methodref("java/util/Base64$Decoder", "decode", "(Ljava/lang/String;)[B");
    instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::invokevirtual, .offset = 4, .operand = bytecode::CpIndexOperand{b64_decode}});
    bytecode::u2 utf8_f = cp.find_or_add_fieldref("java/nio/charset/StandardCharsets", "UTF_8", "Ljava/nio/charset/Charset;");
    instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::getstatic, .offset = 5, .operand = bytecode::CpIndexOperand{utf8_f}});
    bytecode::u2 str_init = cp.find_or_add_methodref("java/lang/String", "<init>", "([BLjava/nio/charset/Charset;)V");
    instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::invokespecial, .offset = 6, .operand = bytecode::CpIndexOperand{str_init}});
    instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::areturn, .offset = 7, .operand = bytecode::NoOperand{}});

    bytecode::AttrCode code;
    code.max_stack = 4;
    code.max_locals = 1;
    bytecode::update_code(code, instrs, cp);

    bytecode::Attribute attr;
    attr.name_index = cp.find_or_add_utf8("Code");
    attr.data = std::move(code);
    m.attributes.push_back(std::move(attr));
    cls.methods().push_back(std::move(m));

    return cp.find_or_add_methodref(current_class, helper_name, "(Ljava/lang/String;)Ljava/lang/String;");
}

static bool skip_element_value(
    size_t& offset,
    std::vector<bytecode::u1>& bytes,
    bytecode::ConstantPool& cp,
    const std::unordered_map<std::string, std::string>& elem_renames,
    const std::unordered_map<std::string, std::string>& reverse_class_renames,
    const std::unordered_map<std::string, std::string>& field_renames,
    std::string_view orig_cls,
    PassContext& ctx);

static bool rewrite_annotation(
    size_t& offset,
    std::vector<bytecode::u1>& bytes,
    bytecode::ConstantPool& cp,
    const std::unordered_map<std::string, std::string>& elem_renames,
    const std::unordered_map<std::string, std::string>& reverse_class_renames,
    const std::unordered_map<std::string, std::string>& field_renames,
    PassContext& ctx) {
    if (offset + 4 > bytes.size()) return false;
    bytecode::u2 type_index = (static_cast<bytecode::u2>(bytes[offset]) << 8) | bytes[offset + 1];
    offset += 2;
    bytecode::u2 num_pairs = (static_cast<bytecode::u2>(bytes[offset]) << 8) | bytes[offset + 1];
    offset += 2;

    std::string_view type_desc = cp.get_utf8(type_index);
    std::string orig_cls;
    if (type_desc.size() >= 3 && type_desc.front() == 'L' && type_desc.back() == ';') {
        std::string internal_name(type_desc.substr(1, type_desc.size() - 2));
        orig_cls = internal_name;
        if (auto rit = reverse_class_renames.find(internal_name); rit != reverse_class_renames.end()) {
            orig_cls = rit->second;
        }
    }

    for (bytecode::u2 p = 0; p < num_pairs; ++p) {
        if (offset + 2 > bytes.size()) return false;
        bytecode::u2 elem_name_idx = (static_cast<bytecode::u2>(bytes[offset]) << 8) | bytes[offset + 1];
        std::string_view elem_name = cp.get_utf8(elem_name_idx);

        if (!orig_cls.empty()) {
            std::string elem_key = orig_cls + "." + std::string(elem_name);
            auto it = elem_renames.find(elem_key);
            if (it == elem_renames.end()) {
                it = elem_renames.find(std::string(type_desc) + "." + std::string(elem_name));
            }
            if (it != elem_renames.end()) {
                bytecode::u2 new_idx = cp.find_or_add_utf8(it->second);
                bytes[offset] = static_cast<bytecode::u1>((new_idx >> 8) & 0xFF);
                bytes[offset + 1] = static_cast<bytecode::u1>(new_idx & 0xFF);
            }
        }
        offset += 2;

        if (!skip_element_value(offset, bytes, cp, elem_renames, reverse_class_renames, field_renames, orig_cls, ctx)) {
            return false;
        }
    }
    return true;
}

static bool skip_element_value(
    size_t& offset,
    std::vector<bytecode::u1>& bytes,
    bytecode::ConstantPool& cp,
    const std::unordered_map<std::string, std::string>& elem_renames,
    const std::unordered_map<std::string, std::string>& reverse_class_renames,
    const std::unordered_map<std::string, std::string>& field_renames,
    std::string_view orig_cls,
    PassContext& ctx) {
    if (offset >= bytes.size()) return false;
    bytecode::u1 tag = bytes[offset++];

    switch (tag) {
        case 'B': case 'C': case 'D': case 'F':
        case 'I': case 'J': case 'S': case 'Z':
        case 'c': {
            if (offset + 2 > bytes.size()) return false;
            offset += 2;
            return true;
        }
        case 's': {
            if (offset + 2 > bytes.size()) return false;
            if (ctx.config.string_encryption.enabled && ctx.config.string_encryption.encrypt_annotation_strings && ctx.config.rename.enabled && !orig_cls.empty() && ctx.is_class_in_scope(orig_cls)) {
                bytecode::u2 str_idx = (static_cast<bytecode::u2>(bytes[offset]) << 8) | bytes[offset + 1];
                std::string_view str_val = cp.get_utf8(str_idx);
                if (!str_val.empty()) {
                    std::string enc = base64_encode(std::vector<uint8_t>(str_val.begin(), str_val.end()));
                    bytecode::u2 new_u = cp.find_or_add_utf8(enc);
                    bytes[offset] = static_cast<bytecode::u1>((new_u >> 8) & 0xFF);
                    bytes[offset + 1] = static_cast<bytecode::u1>(new_u & 0xFF);
                }
            }
            offset += 2;
            return true;
        }
        case 'e': {
            if (offset + 4 > bytes.size()) return false;
            bytecode::u2 enum_type_idx = (static_cast<bytecode::u2>(bytes[offset]) << 8) | bytes[offset + 1];
            bytecode::u2 enum_const_idx = (static_cast<bytecode::u2>(bytes[offset + 2]) << 8) | bytes[offset + 3];
            std::string_view enum_type_desc = cp.get_utf8(enum_type_idx);
            if (enum_type_desc.size() >= 3 && enum_type_desc.front() == 'L' && enum_type_desc.back() == ';') {
                std::string enum_cls(enum_type_desc.substr(1, enum_type_desc.size() - 2));
                if (auto rit = reverse_class_renames.find(enum_cls); rit != reverse_class_renames.end()) {
                    enum_cls = rit->second;
                }
                std::string_view const_name = cp.get_utf8(enum_const_idx);
                std::string fkey = enum_cls + "." + std::string(const_name);
                auto fit = field_renames.find(fkey);
                if (fit != field_renames.end()) {
                    bytecode::u2 new_idx = cp.find_or_add_utf8(fit->second);
                    bytes[offset + 2] = static_cast<bytecode::u1>((new_idx >> 8) & 0xFF);
                    bytes[offset + 3] = static_cast<bytecode::u1>(new_idx & 0xFF);
                }
            }
            offset += 4;
            return true;
        }
        case '@': {
            return rewrite_annotation(offset, bytes, cp, elem_renames, reverse_class_renames, field_renames, ctx);
        }
        case '[': {
            if (offset + 2 > bytes.size()) return false;
            bytecode::u2 num_vals = (static_cast<bytecode::u2>(bytes[offset]) << 8) | bytes[offset + 1];
            offset += 2;
            for (bytecode::u2 i = 0; i < num_vals; ++i) {
                if (!skip_element_value(offset, bytes, cp, elem_renames, reverse_class_renames, field_renames, orig_cls, ctx)) {
                    return false;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

static void remap_annotations_in_attributes(
    std::vector<bytecode::Attribute>& attrs,
    bytecode::ConstantPool& cp,
    const std::unordered_map<std::string, std::string>& elem_renames,
    const std::unordered_map<std::string, std::string>& reverse_class_renames,
    const std::unordered_map<std::string, std::string>& field_renames,
    PassContext& ctx) {
    if (elem_renames.empty() && !ctx.config.string_encryption.enabled) return;

    for (auto& attr : attrs) {
        auto* raw = attr.get_mut_if<bytecode::AttrRaw>();
        if (!raw || raw->data.empty()) continue;

        std::string_view aname = attr.name(cp);
        if (aname == "RuntimeVisibleAnnotations" || aname == "RuntimeInvisibleAnnotations") {
            if (raw->data.size() < 2) continue;
            size_t offset = 0;
            bytecode::u2 num_annotations = (static_cast<bytecode::u2>(raw->data[offset]) << 8) | raw->data[offset + 1];
            offset += 2;
            for (bytecode::u2 a = 0; a < num_annotations; ++a) {
                if (!rewrite_annotation(offset, raw->data, cp, elem_renames, reverse_class_renames, field_renames, ctx)) {
                    break;
                }
            }
        } else if (aname == "RuntimeVisibleParameterAnnotations" || aname == "RuntimeInvisibleParameterAnnotations") {
            if (raw->data.empty()) continue;
            size_t offset = 0;
            bytecode::u1 num_params = raw->data[offset++];
            for (bytecode::u1 p = 0; p < num_params; ++p) {
                if (offset + 2 > raw->data.size()) break;
                bytecode::u2 num_annotations = (static_cast<bytecode::u2>(raw->data[offset]) << 8) | raw->data[offset + 1];
                offset += 2;
                for (bytecode::u2 a = 0; a < num_annotations; ++a) {
                    if (!rewrite_annotation(offset, raw->data, cp, elem_renames, reverse_class_renames, field_renames, ctx)) {
                        break;
                    }
                }
            }
        }
    }
}

static void remap_class_annotations(
    bytecode::ClassFile& cls,
    bytecode::ConstantPool& cp,
    const std::unordered_map<std::string, std::string>& elem_renames,
    const std::unordered_map<std::string, std::string>& reverse_class_renames,
    const std::unordered_map<std::string, std::string>& field_renames,
    PassContext& ctx) {
    if (elem_renames.empty() && !ctx.config.string_encryption.enabled) return;

    remap_annotations_in_attributes(cls.attributes(), cp, elem_renames, reverse_class_renames, field_renames, ctx);
    for (auto& f : cls.fields()) {
        remap_annotations_in_attributes(f.attributes, cp, elem_renames, reverse_class_renames, field_renames, ctx);
    }
    for (auto& m : cls.methods()) {
        remap_annotations_in_attributes(m.attributes, cp, elem_renames, reverse_class_renames, field_renames, ctx);
        if (auto* code = m.code_mut()) {
            remap_annotations_in_attributes(code->attributes, cp, elem_renames, reverse_class_renames, field_renames, ctx);
        }
    }
}

std::string RenamePass::generate_obfuscated_name(size_t index, config::RenameMode mode, uint64_t salt) {
    if (mode == config::RenameMode::ConfusingChars || mode == config::RenameMode::ConfusingLong) {
        // Spamming I and l with high-entropy alphanumeric suffix, e.g. "IlllIIIlIIII_9n1M2903Lp15"
        static const char il_chars[] = "Il";
        static const char b62_chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

        uint64_t h = splitmix64((static_cast<uint64_t>(index) ^ salt) + 0x517cc1b727220a95ULL);
        std::string res;
        res.reserve(28);

        // First character must be valid Java letter ('I' or 'l')
        res += il_chars[h & 1];
        h >>= 1;

        // 11 more alternating/confusing 'I' and 'l' chars
        for (int i = 0; i < 11; ++i) {
            res += il_chars[(h >> i) & 1];
        }
        res += '_';

        // High-entropy alphanumeric suffix incorporating index to guarantee 100% uniqueness
        uint64_t val = (static_cast<uint64_t>(index) * 11400714819323198485ULL) ^ splitmix64(h ^ salt);
        for (int i = 0; i < 12; ++i) {
            res += b62_chars[val % 62];
            val /= 62;
        }
        return res;
    } else if (mode == config::RenameMode::RandomHex) {
        std::ostringstream oss;
        oss << "_c" << std::hex << std::setfill('0') << std::setw(8) << ((index * 2654435761ULL ^ salt) & 0xFFFFFFFF);
        return oss.str();
    } else {
        // ShortAlpha: a, b, c, ... z, aa, ab ...
        std::string res;
        size_t n = index;
        do {
            res += static_cast<char>('a' + (n % 26));
            n /= 26;
        } while (n > 0);
        std::reverse(res.begin(), res.end());
        return res;
    }
}

bool RenamePass::run_on_archive(PassContext& ctx) {
    m_annotation_element_renames.clear();
    generate_mappings(ctx);

    // 1. Apply mappings to all classes (methods, fields, descriptors, constant pool references)
    for (auto& [name, cf] : ctx.archive.classes()) {
        apply_mappings_to_class(cf, ctx);
    }

    // Remap reflection proxy targets
    for (const auto& pm : ctx.proxy_methods) {
        auto* cf = ctx.archive.find_class(pm.owner_class);
        if (!cf) continue;

        bytecode::MethodInfo* m = nullptr;
        for (auto& meth : cf->methods()) {
            if (meth.name(cf->constant_pool()) == pm.proxy_method_name) {
                m = &meth;
                break;
            }
        }
        if (!m || !m->code_mut()) continue;

        std::string new_dot_class;
        if (auto it = ctx.class_renames.find(pm.target_class); it != ctx.class_renames.end()) {
            new_dot_class = it->second;
            std::replace(new_dot_class.begin(), new_dot_class.end(), '/', '.');
        }
        std::string new_member_name;
        if (pm.is_field) {
            new_member_name = resolve_field_rename(pm.target_class, pm.target_method, ctx);
        } else if (!pm.is_constructor) {
            new_member_name = resolve_method_rename(pm.target_class, pm.target_method, pm.target_desc, ctx);
        }

        auto& cp = cf->constant_pool();
        auto instrs = bytecode::disassemble(m->code_mut()->code);
        size_t ldc_count = 0;
        for (auto& inst : instrs) {
            if (inst.opcode == bytecode::Opcode::ldc || inst.opcode == bytecode::Opcode::ldc_w) {
                if (ldc_count == 0 && !new_dot_class.empty()) {
                    bytecode::u2 idx = cp.find_or_add_string(new_dot_class);
                    inst.opcode = (idx <= 255) ? bytecode::Opcode::ldc : bytecode::Opcode::ldc_w;
                    inst.operand = bytecode::CpIndexOperand{idx};
                } else if (ldc_count == 1 && !new_member_name.empty()) {
                    bytecode::u2 idx = cp.find_or_add_string(new_member_name);
                    inst.opcode = (idx <= 255) ? bytecode::Opcode::ldc : bytecode::Opcode::ldc_w;
                    inst.operand = bytecode::CpIndexOperand{idx};
                } else {
                    if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&inst.operand)) {
                        if (const auto* str = cp.get_if<bytecode::CpString>(cp_op->index)) {
                            std::string val(cp.get_utf8(str->string_index));
                            std::string slash_val = val;
                            std::replace(slash_val.begin(), slash_val.end(), '.', '/');
                            if (auto it = ctx.class_renames.find(slash_val); it != ctx.class_renames.end()) {
                                std::string new_dot = it->second;
                                std::replace(new_dot.begin(), new_dot.end(), '/', '.');
                                bytecode::u2 idx = cp.find_or_add_string(new_dot);
                                inst.opcode = (idx <= 255) ? bytecode::Opcode::ldc : bytecode::Opcode::ldc_w;
                                inst.operand = bytecode::CpIndexOperand{idx};
                            }
                        }
                    }
                }
                ldc_count++;
            }
        }
        bytecode::update_code(*m->code_mut(), instrs, cp);
    }

    // 2. Update keys in archive map if classes were renamed
    if (!ctx.class_renames.empty()) {
        std::unordered_map<std::string, bytecode::ClassFile> updated_classes;
        for (auto& [old_name, cf] : ctx.archive.classes()) {
            auto it = ctx.class_renames.find(old_name);
            std::string target_name = (it != ctx.class_renames.end()) ? it->second : old_name;
            updated_classes[target_name] = std::move(cf);
        }
        ctx.archive.classes() = std::move(updated_classes);
        update_manifest(ctx);
    }

    return true;
}

void RenamePass::generate_mappings(PassContext& ctx) {
    size_t class_counter = 0;
    size_t field_counter = 0;
    size_t method_counter = 0;

    auto main_cls_opt = ctx.archive.get_main_class();
    std::string main_cls_internal;
    if (main_cls_opt) {
        main_cls_internal = *main_cls_opt;
        std::replace(main_cls_internal.begin(), main_cls_internal.end(), '.', '/');
    }

    uint64_t class_salt = ctx.rng();
    uint64_t pkg_salt = ctx.rng();
    uint64_t field_salt = ctx.rng();
    uint64_t method_salt = ctx.rng();

    std::unordered_map<std::string, std::string> pkg_renames;
    size_t pkg_counter = 0;

    // 1. Class Renaming & Package Obfuscation / Unnesting
    if (ctx.config.rename.rename_classes || ctx.config.rename.remove_packages || ctx.config.rename.rename_packages) {
        for (const auto& [class_name, cf] : ctx.archive.classes()) {
            if (!ctx.config.is_class_in_scope(class_name)) continue;
            if (ctx.config.rename.keep_main_class && class_name == main_cls_internal) continue;

            // Don't rename classes in exclusions
            bool excluded = false;
            for (const auto& ex : ctx.config.rename.exclusions) {
                if (class_name.starts_with(ex)) { excluded = true; break; }
            }
            if (excluded) continue;

            // Find matching pattern for this class (if any)
            std::string preserved_prefix = "";
            bool matched_specific_pattern = false;
            size_t best_match_len = 0;
            for (const auto& pkg : ctx.config.target_packages) {
                auto parsed = config::parse_package_pattern(pkg);
                if (parsed.match_prefix.empty()) {
                    if (!matched_specific_pattern) {
                        preserved_prefix = "";
                    }
                } else if (class_name.starts_with(parsed.match_prefix) || 
                           class_name == parsed.match_prefix.substr(0, parsed.match_prefix.size() - 1)) {
                    if (parsed.match_prefix.size() > best_match_len) {
                        best_match_len = parsed.match_prefix.size();
                        preserved_prefix = parsed.preserved_prefix;
                        matched_specific_pattern = true;
                    }
                }
            }

            std::string obf_simple_name = ctx.config.rename.rename_classes
                ? generate_obfuscated_name(class_counter++, ctx.config.rename.mode, class_salt)
                : std::string(class_name.substr(class_name.find_last_of('/') != std::string::npos ? class_name.find_last_of('/') + 1 : 0));

            size_t last_slash = class_name.find_last_of('/');
            std::string orig_pkg = (last_slash != std::string::npos) ? class_name.substr(0, last_slash) : "";

            std::string new_name;
            if (ctx.config.rename.remove_packages) {
                // Remove matched packages completely: all classes unnested into the default/root package (no package!)
                new_name = obf_simple_name;
            } else if (ctx.config.rename.rename_packages) {
                if (orig_pkg.empty()) {
                    new_name = obf_simple_name;
                } else {
                    auto pit = pkg_renames.find(orig_pkg);
                    if (pit == pkg_renames.end()) {
                        std::string obf_pkg = generate_obfuscated_name(pkg_counter++, ctx.config.rename.mode, pkg_salt);
                        pit = pkg_renames.emplace(orig_pkg, obf_pkg).first;
                    }
                    const std::string& obf_pkg = pit->second;
                    if (matched_specific_pattern && !preserved_prefix.empty()) {
                        // Only obfuscate the package after preserved_prefix (e.g. com/ preserved, testapp obfuscated)
                        new_name = preserved_prefix + obf_pkg + "/" + obf_simple_name;
                    } else {
                        // Whole package scrambled (e.g. com.* or empty target_packages)
                        new_name = obf_pkg + "/" + obf_simple_name;
                    }
                }
            } else {
                // Keep directory prefix, rename class base name
                std::string dir_prefix = (last_slash != std::string::npos) ? class_name.substr(0, last_slash + 1) : "";
                new_name = dir_prefix + obf_simple_name;
            }

            ctx.class_renames[class_name] = new_name;
            ctx.reverse_class_renames[new_name] = class_name;
            ctx.stats.classes_renamed++;
        }
    }

    // 2. Member Renaming (Fields & Methods)
    for (auto& [class_name, cf] : ctx.archive.classes()) {
        if (!ctx.config.is_class_in_scope(class_name)) continue;

        bool is_enum = (cf.access_flags() & bytecode::access_flags::ACC_ENUM) != 0;
        if (cf.super_class_index()) {
            if (cf.constant_pool().get_class_name(cf.super_class_index()) == "java/lang/Enum") {
                is_enum = true;
            }
        }

        // Fields
        if (ctx.config.rename.rename_fields) {
            for (auto& f : cf.fields()) {
                if (is_enum) {
                    if (f.name(cf.constant_pool()) == "$VALUES" || f.name(cf.constant_pool()) == "ENUM$VALUES") {
                        continue;
                    }
                    if ((f.access_flags & bytecode::access_flags::ACC_ENUM) != 0 && !ctx.config.rename.obfuscate_enums) {
                        continue;
                    }
                }
                std::string fname(f.name(cf.constant_pool()));
                std::string key = class_name + "." + fname;
                std::string obf_name = generate_obfuscated_name(field_counter++, ctx.config.rename.mode, field_salt);
                ctx.field_renames[key] = obf_name;
                ctx.stats.fields_renamed++;
            }
        }

        // Methods
        if (ctx.config.rename.rename_methods) {
            for (auto& m : cf.methods()) {
                std::string mname(m.name(cf.constant_pool()));
                std::string mdesc(m.descriptor(cf.constant_pool()));

                if (is_reserved_method_name(mname, mdesc) || m.is_native() || m.is_synthetic()) {
                    continue;
                }

                // Check if this method was already renamed in an interface, base class, or subclass
                std::string mapped_name = resolve_method_rename(class_name, mname, mdesc, ctx);

                if (mapped_name.empty()) {
                    mapped_name = generate_obfuscated_name(method_counter++, ctx.config.rename.mode, method_salt);
                }

                std::unordered_set<std::string> visited;
                assign_method_name_to_hierarchy(class_name, mname, mdesc, mapped_name, ctx, visited);
                ctx.stats.methods_renamed++;

                if (cf.is_annotation()) {
                    m_annotation_element_renames[class_name + "." + mname] = mapped_name;
                    m_annotation_element_renames["L" + class_name + ";." + mname] = mapped_name;
                    if (auto cit = ctx.class_renames.find(class_name); cit != ctx.class_renames.end()) {
                        m_annotation_element_renames[cit->second + "." + mname] = mapped_name;
                        m_annotation_element_renames["L" + cit->second + ";." + mname] = mapped_name;
                    }
                }
            }
        }
    }
}

void RenamePass::apply_mappings_to_class(bytecode::ClassFile& cls, PassContext& ctx) {
    auto& cp = cls.constant_pool();
    std::string current_class(cls.name());

    // Build reverse map for class names so we can always look up the original target class
    std::unordered_map<std::string, std::string> reverse_class_renames;
    for (const auto& [orig, renamed] : ctx.class_renames) {
        reverse_class_renames[renamed] = orig;
    }

    // 1. Update NameAndType references in ConstantPool across all call sites (Fieldref, Methodref, InterfaceMethodref)
    // Running this BEFORE remapping class names and descriptors ensures target_cls and descriptors match ctx.*_renames!
    size_t cp_size_refs = cp.size();
    for (size_t i = 1; i < cp_size_refs; ++i) {
        bytecode::u2 nat_idx = 0;
        bytecode::u2 class_idx = 0;
        int ref_type = 0; // 1 = methodref, 2 = imethodref, 3 = fieldref

        if (const auto* mr = cp.get_if<bytecode::CpMethodref>(static_cast<bytecode::u2>(i))) {
            nat_idx = mr->name_and_type_index;
            class_idx = mr->class_index;
            ref_type = 1;
        } else if (const auto* imr = cp.get_if<bytecode::CpInterfaceMethodref>(static_cast<bytecode::u2>(i))) {
            nat_idx = imr->name_and_type_index;
            class_idx = imr->class_index;
            ref_type = 2;
        } else if (const auto* fr = cp.get_if<bytecode::CpFieldref>(static_cast<bytecode::u2>(i))) {
            nat_idx = fr->name_and_type_index;
            class_idx = fr->class_index;
            ref_type = 3;
        }

        if (ref_type != 0 && nat_idx != 0 && class_idx != 0) {
            std::string target_cls(cp.get_class_name(class_idx));
            if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(nat_idx)) {
                std::string member_name(cp.get_utf8(nat->name_index));
                std::string member_desc(cp.get_utf8(nat->descriptor_index));

                std::string orig_cls = target_cls;
                if (auto rit = reverse_class_renames.find(target_cls); rit != reverse_class_renames.end()) {
                    orig_cls = rit->second;
                }

                std::optional<std::string> new_name;
                if (ref_type == 1 || ref_type == 2) {
                    std::string res = resolve_method_rename(orig_cls, member_name, member_desc, ctx);
                    if (!res.empty()) {
                        new_name = res;
                    }
                } else if (ref_type == 3) {
                    std::string res = resolve_field_rename(orig_cls, member_name, ctx);
                    if (!res.empty()) {
                        new_name = res;
                    }
                }

                if (new_name) {
                    bytecode::u2 new_nat = cp.find_or_add_name_and_type(*new_name, member_desc);
                    if (ref_type == 1) {
                        if (auto* mr_mut = cp.get_mut_if<bytecode::CpMethodref>(static_cast<bytecode::u2>(i))) {
                            mr_mut->name_and_type_index = new_nat;
                        }
                    } else if (ref_type == 2) {
                        if (auto* imr_mut = cp.get_mut_if<bytecode::CpInterfaceMethodref>(static_cast<bytecode::u2>(i))) {
                            imr_mut->name_and_type_index = new_nat;
                        }
                    } else if (ref_type == 3) {
                        if (auto* fr_mut = cp.get_mut_if<bytecode::CpFieldref>(static_cast<bytecode::u2>(i))) {
                            fr_mut->name_and_type_index = new_nat;
                        }
                    }
                }
            }
        }
    }

    // 2. Rename declared fields in MemberInfo and ConstantPool
    for (auto& f : cls.fields()) {
        std::string fname(f.name(cp));
        std::string key = current_class + "." + fname;
        if (auto it = ctx.field_renames.find(key); it != ctx.field_renames.end()) {
            f.name_index = cp.find_or_add_utf8(it->second);
        }
    }

    // 3. Rename declared methods in MemberInfo and ConstantPool
    for (auto& m : cls.methods()) {
        std::string mname(m.name(cp));
        std::string mdesc(m.descriptor(cp));
        std::string key = current_class + "." + mname + mdesc;
        if (auto it = ctx.method_renames.find(key); it != ctx.method_renames.end()) {
            m.name_index = cp.find_or_add_utf8(it->second);
        }
    }

    // 3b. If obfuscate_enums is enabled, rewrite enum constant names in enum <clinit>
    if (ctx.config.rename.obfuscate_enums) {
        bool is_enum = (cls.access_flags() & bytecode::access_flags::ACC_ENUM) != 0;
        if (cls.super_class_index() && cp.get_class_name(cls.super_class_index()) == "java/lang/Enum") {
            is_enum = true;
        }
        if (is_enum) {
            for (auto& m : cls.methods()) {
                if (m.name(cp) == "<clinit>" && m.code_mut()) {
                    auto instrs = bytecode::disassemble(m.code_mut()->code);
                    bool m_mod = false;
                    for (auto& inst : instrs) {
                        if (inst.opcode == bytecode::Opcode::ldc || inst.opcode == bytecode::Opcode::ldc_w) {
                            if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&inst.operand)) {
                                if (const auto* str = cp.get_if<bytecode::CpString>(cp_op->index)) {
                                    std::string val(cp.get_utf8(str->string_index));
                                    std::string fkey = current_class + "." + val;
                                    if (auto fit = ctx.field_renames.find(fkey); fit != ctx.field_renames.end()) {
                                        bytecode::u2 new_s_idx = cp.find_or_add_string(fit->second);
                                        inst.opcode = (new_s_idx <= 255) ? bytecode::Opcode::ldc : bytecode::Opcode::ldc_w;
                                        inst.operand = bytecode::CpIndexOperand{new_s_idx};
                                        m_mod = true;
                                    }
                                }
                            }
                        }
                    }
                    if (m_mod) {
                        bytecode::update_code(*m.code_mut(), instrs, cp);
                    }
                }
            }
        }
    }

    // 4. Remap this_class name if renamed
    if (auto it = ctx.class_renames.find(current_class); it != ctx.class_renames.end()) {
        bytecode::u2 new_u = cp.find_or_add_utf8(it->second);
        if (auto* c = cp.get_mut_if<bytecode::CpClass>(cls.this_class_index())) {
            c->name_index = new_u;
        }
    }

    // 5. Remap super_class name if renamed
    if (cls.super_class_index()) {
        std::string super_name(cp.get_class_name(cls.super_class_index()));
        if (auto it = ctx.class_renames.find(super_name); it != ctx.class_renames.end()) {
            bytecode::u2 new_u = cp.find_or_add_utf8(it->second);
            if (auto* c = cp.get_mut_if<bytecode::CpClass>(cls.super_class_index())) {
                c->name_index = new_u;
            }
        }
    }

    // 6. Remap interfaces if renamed
    for (auto iface_idx : cls.interfaces()) {
        std::string iface_name(cp.get_class_name(iface_idx));
        if (auto it = ctx.class_renames.find(iface_name); it != ctx.class_renames.end()) {
            bytecode::u2 new_u = cp.find_or_add_utf8(it->second);
            if (auto* c = cp.get_mut_if<bytecode::CpClass>(iface_idx)) {
                c->name_index = new_u;
            }
        }
    }

    // 7. Remap all CONSTANT_Class entries in constant pool
    size_t cp_classes_size = cp.size();
    for (size_t i = 1; i < cp_classes_size; ++i) {
        if (const auto* c = cp.get_if<bytecode::CpClass>(static_cast<bytecode::u2>(i))) {
            std::string cname(cp.get_utf8(c->name_index));
            if (auto it = ctx.class_renames.find(cname); it != ctx.class_renames.end()) {
                bytecode::u2 new_u = cp.find_or_add_utf8(it->second);
                if (auto* c_mut = cp.get_mut_if<bytecode::CpClass>(static_cast<bytecode::u2>(i))) {
                    c_mut->name_index = new_u;
                }
            }
        }
    }

    // 7b. Adapt string constants matching renamed classes (for dynamic reflection e.g. Class.forName)
    if (ctx.config.rename.rename_classes && !ctx.class_renames.empty()) {
        for (size_t i = 1; i < cp_classes_size; ++i) {
            if (auto* s = cp.get_mut_if<bytecode::CpString>(static_cast<bytecode::u2>(i))) {
                std::string val(cp.get_utf8(s->string_index));
                if (!val.empty() && (val.find('/') != std::string::npos || val.find('.') != std::string::npos)) {
                    std::string slash_val = val;
                    std::replace(slash_val.begin(), slash_val.end(), '.', '/');
                    if (auto it = ctx.class_renames.find(slash_val); it != ctx.class_renames.end()) {
                        std::string new_name = it->second;
                        if (val.find('.') != std::string::npos && val.find('/') == std::string::npos) {
                            std::replace(new_name.begin(), new_name.end(), '/', '.');
                        }
                        s->string_index = cp.find_or_add_utf8(new_name);
                    }
                }
            }
        }
    }

    // 8. Remap all descriptor strings in constant pool (Field, Method, NameAndType descriptors)
    if (!ctx.class_renames.empty()) {
        for (size_t i = 1; i < cp.size(); ++i) {
            if (auto* u = cp.get_mut_if<bytecode::CpUtf8>(static_cast<bytecode::u2>(i))) {
                if (u->value.find('L') != std::string::npos && u->value.find(';') != std::string::npos) {
                    u->value = remap_descriptor(u->value, ctx.class_renames);
                }
            }
        }
    }

    // 8b. Remap element names in all annotation attributes across class, fields, and methods
    remap_class_annotations(cls, cp, m_annotation_element_renames, ctx.reverse_class_renames, ctx.field_renames, ctx);

    // 8c. If reflection proxy pass was disabled, insert _dec_ann after invokeinterface calling annotation string getters
    if (ctx.config.string_encryption.enabled && ctx.config.string_encryption.encrypt_annotation_strings && ctx.config.rename.enabled && !ctx.config.reflection_proxy.enabled) {
        size_t num_original_methods = cls.methods().size();
        for (size_t m_idx = 0; m_idx < num_original_methods; ++m_idx) {
            if (cls.methods()[m_idx].name(cp) == "_dec_ann" || !cls.methods()[m_idx].code_mut()) continue;
            auto instrs = bytecode::disassemble(cls.methods()[m_idx].code_mut()->code);
            std::vector<bytecode::Instruction> new_instrs;
            bool modified = false;

            for (const auto& inst : instrs) {
                new_instrs.push_back(inst);
                if (inst.opcode == bytecode::Opcode::invokeinterface) {
                    if (const auto* iface_op = std::get_if<bytecode::InterfaceCallOperand>(&inst.operand)) {
                        if (const auto* imr = cp.get_if<bytecode::CpInterfaceMethodref>(iface_op->index)) {
                            std::string target_cls(cp.get_class_name(imr->class_index));
                            std::string orig_target_cls = target_cls;
                            if (auto rit = ctx.reverse_class_renames.find(target_cls); rit != ctx.reverse_class_renames.end()) {
                                orig_target_cls = rit->second;
                            }
                            if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(imr->name_and_type_index)) {
                                std::string_view m_desc = cp.get_utf8(nat->descriptor_index);
                                if (m_desc == "()Ljava/lang/String;") {
                                    if (auto* target_cf = ctx.archive.find_class(orig_target_cls); target_cf && target_cf->is_annotation()) {
                                        bytecode::u2 dec_mref = get_or_create_ann_decryptor(cls);
                                        bytecode::Instruction dec_inst{
                                            .opcode = bytecode::Opcode::invokestatic,
                                            .offset = static_cast<bytecode::u4>(-1),
                                            .operand = bytecode::CpIndexOperand{dec_mref}
                                        };
                                        new_instrs.push_back(dec_inst);
                                        modified = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (modified) {
                bytecode::update_code(*cls.methods()[m_idx].code_mut(), new_instrs, cp);
            }
        }
    }

    // 9. Rebuild UTF-8 lookup table to ensure any newly added or remapped strings are synchronized
    cp.rebuild_utf8_lookup();
}

void RenamePass::update_manifest(PassContext& ctx) {
    auto main_cls = ctx.archive.get_main_class();
    if (main_cls) {
        std::string internal_main = *main_cls;
        std::replace(internal_main.begin(), internal_main.end(), '.', '/');
        if (auto it = ctx.class_renames.find(internal_main); it != ctx.class_renames.end()) {
            std::string new_main = it->second;
            std::replace(new_main.begin(), new_main.end(), '/', '.');
            ctx.archive.set_main_class(new_main);
        }
    }
}

} // namespace obf::passes
