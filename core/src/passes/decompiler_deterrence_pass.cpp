#include "passes/decompiler_deterrence_pass.hpp"
#include "bytecode/instructions.hpp"
#include "reporter/reporter.hpp"
#include "jar/jar_archive.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <random>

namespace obf::passes {

void DecompilerDeterrencePass::apply_return_type_overloads(bytecode::ClassFile& cf, PassContext& ctx) {
    auto& cp = cf.constant_pool();

    std::vector<bytecode::MethodInfo> new_methods;

    for (const auto& m : cf.methods()) {
        std::string_view m_name = m.name(cp);
        if (m_name == "<init>" || m_name == "<clinit>" || m_name == "main") continue;
        if (m.is_abstract() || m.is_native()) continue;

        std::string_view desc = m.descriptor(cp);
        size_t close_paren = desc.find(')');
        if (close_paren == std::string_view::npos) continue;

        std::string_view params = desc.substr(1, close_paren - 1);
        std::string_view ret = desc.substr(close_paren + 1);

        std::string new_ret;
        std::vector<bytecode::Instruction> instrs;

        if (ret == "V") {
            // Overload with String return
            new_ret = "Ljava/lang/String;";
            bytecode::Instruction aconst_null_inst;
            aconst_null_inst.offset = 0;
            aconst_null_inst.opcode = bytecode::Opcode::aconst_null;
            instrs.push_back(aconst_null_inst);

            bytecode::Instruction areturn_inst;
            areturn_inst.offset = 1;
            areturn_inst.opcode = bytecode::Opcode::areturn;
            instrs.push_back(areturn_inst);
        } else if (ret == "I" || ret == "Z" || ret == "B" || ret == "C" || ret == "S") {
            // Overload with void return
            new_ret = "V";
            bytecode::Instruction return_inst;
            return_inst.offset = 0;
            return_inst.opcode = bytecode::Opcode::return_op;
            instrs.push_back(return_inst);
        } else if (!ret.empty() && ret.front() == 'L') {
            // Overload with int return
            new_ret = "I";
            bytecode::Instruction iconst_0_inst;
            iconst_0_inst.offset = 0;
            iconst_0_inst.opcode = bytecode::Opcode::iconst_0;
            instrs.push_back(iconst_0_inst);

            bytecode::Instruction ireturn_inst;
            ireturn_inst.offset = 1;
            ireturn_inst.opcode = bytecode::Opcode::ireturn;
            instrs.push_back(ireturn_inst);
        } else {
            continue;
        }

        std::string new_desc = "(" + std::string(params) + ")" + new_ret;

        // Ensure method doesn't already exist
        if (cf.find_method(m_name, new_desc) != nullptr) {
            continue;
        }

        bytecode::MethodInfo decoy;
        decoy.access_flags = (m.access_flags & ~bytecode::access_flags::ACC_ABSTRACT & ~bytecode::access_flags::ACC_PUBLIC & ~bytecode::access_flags::ACC_PROTECTED) |
                             bytecode::access_flags::ACC_PRIVATE |
                             bytecode::access_flags::ACC_SYNTHETIC |
                             bytecode::access_flags::ACC_BRIDGE;
        decoy.name_index = m.name_index;
        decoy.descriptor_index = cp.find_or_add_utf8(new_desc);

        bytecode::AttrCode code;
        code.max_stack = 1;
        // Count args for max_locals
        bytecode::u2 max_l = m.is_static() ? 0 : 1;
        for (char c : params) {
            if (c == 'J' || c == 'D') max_l += 2;
            else max_l += 1;
        }
        code.max_locals = std::max<bytecode::u2>(max_l, 1);
        code.code = bytecode::assemble(instrs);

        bytecode::Attribute code_attr;
        code_attr.name_index = cp.find_or_add_utf8("Code");
        code_attr.data = std::move(code);
        decoy.attributes.push_back(std::move(code_attr));

        new_methods.push_back(std::move(decoy));
        ctx.stats.decompiler_deterrents_applied++;

        if (new_methods.size() >= 3) {
            break; // Limit to 3 overloads per class to avoid excessive bytecode bloat
        }
    }

    for (auto& nm : new_methods) {
        cf.methods().push_back(std::move(nm));
    }
}

void DecompilerDeterrencePass::apply_synthetic_bridge_tags(bytecode::ClassFile& cf, PassContext& ctx) {
    auto& cp = cf.constant_pool();

    for (auto& m : cf.methods()) {
        std::string_view m_name = m.name(cp);
        if (m_name == "<init>" || m_name == "<clinit>" || m_name == "main") continue;
        if (m.is_abstract() || m.is_native()) continue;

        // Tag internal non-public methods with synthetic & bridge flags
        if (!m.is_public()) {
            m.access_flags |= bytecode::access_flags::ACC_SYNTHETIC |
                              bytecode::access_flags::ACC_BRIDGE;
            ctx.stats.decompiler_deterrents_applied++;
        }
    }
}

void DecompilerDeterrencePass::apply_phantom_inner_classes(bytecode::ClassFile& cf, PassContext& ctx) {
    auto& cp = cf.constant_pool();

    // Injects valid JVMS §4.7.6 InnerClasses attribute table entry
    auto* inner_attr_raw = cf.find_attribute_mut("InnerClasses");
    if (!inner_attr_raw) {
        bytecode::Attribute new_attr;
        new_attr.name_index = cp.find_or_add_utf8("InnerClasses");
        new_attr.data = bytecode::AttrInnerClasses{};
        cf.attributes().push_back(std::move(new_attr));
        inner_attr_raw = cf.find_attribute_mut("InnerClasses");
    }

    auto* inner_attr = inner_attr_raw->get_mut_if<bytecode::AttrInnerClasses>();
    if (inner_attr) {
        std::string phantom_inner = std::string(cf.name()) + "$1";
        bytecode::u2 inner_cls_idx = cp.find_or_add_class(phantom_inner);
        bytecode::u2 outer_cls_idx = cf.this_class_index();

        bytecode::InnerClassEntry entry;
        entry.inner_class_info_index = inner_cls_idx;
        entry.outer_class_info_index = outer_cls_idx;
        entry.inner_name_index = 0; // Anonymous
        entry.inner_class_access_flags = bytecode::access_flags::ACC_PUBLIC |
                                         bytecode::access_flags::ACC_STATIC |
                                         bytecode::access_flags::ACC_SYNTHETIC;

        inner_attr->classes.push_back(entry);
        ctx.stats.decompiler_deterrents_applied++;
    }
}

void DecompilerDeterrencePass::apply_custom_attributes(bytecode::ClassFile& cf, PassContext& ctx) {
    auto& cp = cf.constant_pool();

    // JVMS §4.7.1: Standards-compliant custom attribute silently ignored by standard JVMs
    bytecode::Attribute custom_attr;
    custom_attr.name_index = cp.find_or_add_utf8("$JObfDeterrent");
    custom_attr.data = bytecode::AttrRaw{{0x01, 0x02, 0x03, 0x04, 0x50, 0x52, 0x4F, 0x54}};
    cf.attributes().push_back(std::move(custom_attr));

    ctx.stats.decompiler_deterrents_applied++;
}

void DecompilerDeterrencePass::scramble_methods(bytecode::ClassFile& cf, PassContext& ctx) {
    if (cf.methods().size() <= 1) return;

    // Deterministic per-class PRNG seed combining global seed and class name FNV-1a hash
    uint64_t class_hash = 14695981039346656037ULL;
    for (char c : cf.name()) {
        class_hash ^= static_cast<uint64_t>(c);
        class_hash *= 1099511628211ULL;
    }

    uint64_t s = ctx.config.seed.value_or(0x5C8A1BULL) ^ class_hash;
    std::mt19937 rng(static_cast<uint32_t>(s ^ (s >> 32)));

    std::shuffle(cf.methods().begin(), cf.methods().end(), rng);
    ctx.stats.decompiler_deterrents_applied++;
}

bool DecompilerDeterrencePass::run_on_archive(PassContext& ctx) {
    bool deterrence_enabled = ctx.config.decompiler_deterrence.enabled;
    bool scramble_enabled = ctx.config.rename.scramble_methods || ctx.config.decompiler_deterrence.scramble_method_order;

    if (!deterrence_enabled && !scramble_enabled) {
        return true;
    }

    size_t count = 0;
    for (auto& [name, cf] : ctx.archive.classes()) {
        if (!ctx.config.is_class_in_scope(name)) continue;

        if (deterrence_enabled) {
            if (ctx.config.decompiler_deterrence.overload_return_types && !cf.is_interface() && !ctx.config.reflection_proxy.enabled) {
                apply_return_type_overloads(cf, ctx);
            }

            if (ctx.config.decompiler_deterrence.synthetic_bridge_tags) {
                apply_synthetic_bridge_tags(cf, ctx);
            }

            if (ctx.config.decompiler_deterrence.phantom_inner_classes && !cf.is_interface()) {
                apply_phantom_inner_classes(cf, ctx);
            }

            if (ctx.config.decompiler_deterrence.custom_attributes) {
                apply_custom_attributes(cf, ctx);
            }
        }

        if (scramble_enabled) {
            scramble_methods(cf, ctx);
        }

        count++;
    }

    ctx.reporter.log(reporter::LogLevel::Info,
                     "Applied decompiler deterrence transformations across " +
                     std::to_string(count) + " classes (" +
                     std::to_string(ctx.stats.decompiler_deterrents_applied) +
                     " deterrents applied).");

    return true;
}

} // namespace obf::passes
