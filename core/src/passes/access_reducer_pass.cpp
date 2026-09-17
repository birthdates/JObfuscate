#include "passes/access_reducer_pass.hpp"
#include "jar/jar_archive.hpp"
#include "bytecode/instructions.hpp"
#include <unordered_set>

namespace obf::passes {

bool AccessReducerPass::run_on_archive(PassContext& ctx) {
    if (!ctx.config.access_reducer.enabled) return true;

    // Phase 1: Cross-class reference gathering
    std::unordered_set<std::string> external_called_methods;  // "target_class.method_name(desc)"
    std::unordered_set<std::string> external_accessed_fields; // "target_class.field_name"

    for (const auto& [caller_class, cf] : ctx.archive.classes()) {
        const auto& cp = cf.constant_pool();
        for (const auto& m : cf.methods()) {
            if (const auto* code = m.code()) {
                auto instructions = bytecode::disassemble(code->code);
                for (const auto& inst : instructions) {
                    if (inst.opcode == bytecode::Opcode::invokevirtual ||
                        inst.opcode == bytecode::Opcode::invokestatic ||
                        inst.opcode == bytecode::Opcode::invokespecial ||
                        inst.opcode == bytecode::Opcode::invokeinterface) {
                        
                        bytecode::u2 cp_idx = 0;
                        if (const auto* op = std::get_if<bytecode::CpIndexOperand>(&inst.operand)) {
                            cp_idx = op->index;
                        } else if (const auto* if_op = std::get_if<bytecode::InterfaceCallOperand>(&inst.operand)) {
                            cp_idx = if_op->index;
                        }

                        if (cp_idx != 0) {
                            std::string target_cls;
                            bytecode::u2 nat_idx = 0;
                            if (const auto* mr = cp.get_if<bytecode::CpMethodref>(cp_idx)) {
                                target_cls = std::string(cp.get_class_name(mr->class_index));
                                nat_idx = mr->name_and_type_index;
                            } else if (const auto* imr = cp.get_if<bytecode::CpInterfaceMethodref>(cp_idx)) {
                                target_cls = std::string(cp.get_class_name(imr->class_index));
                                nat_idx = imr->name_and_type_index;
                            }

                            if (nat_idx != 0 && !target_cls.empty() && target_cls != caller_class) {
                                if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(nat_idx)) {
                                    std::string mname(cp.get_utf8(nat->name_index));
                                    std::string mdesc(cp.get_utf8(nat->descriptor_index));
                                    std::string curr = target_cls;
                                    while (!curr.empty()) {
                                        external_called_methods.insert(curr + "." + mname + mdesc);
                                        const auto* node = ctx.hierarchy.get_node(curr);
                                        if (!node) break;
                                        for (const auto& iface : node->interfaces) {
                                            external_called_methods.insert(iface + "." + mname + mdesc);
                                        }
                                        if (node->super_class.empty() || node->super_class == "java/lang/Object") {
                                            break;
                                        }
                                        curr = node->super_class;
                                    }
                                }
                            }
                        }
                    } else if (inst.opcode == bytecode::Opcode::getfield ||
                               inst.opcode == bytecode::Opcode::putfield ||
                               inst.opcode == bytecode::Opcode::getstatic ||
                               inst.opcode == bytecode::Opcode::putstatic) {
                        if (const auto* op = std::get_if<bytecode::CpIndexOperand>(&inst.operand)) {
                            if (const auto* fr = cp.get_if<bytecode::CpFieldref>(op->index)) {
                                std::string target_cls(cp.get_class_name(fr->class_index));
                                if (!target_cls.empty() && target_cls != caller_class) {
                                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(fr->name_and_type_index)) {
                                        std::string fname(cp.get_utf8(nat->name_index));
                                        std::string curr = target_cls;
                                        while (!curr.empty()) {
                                            external_accessed_fields.insert(curr + "." + fname);
                                            const auto* node = ctx.hierarchy.get_node(curr);
                                            if (!node) break;
                                            for (const auto& iface : node->interfaces) {
                                                external_accessed_fields.insert(iface + "." + fname);
                                            }
                                            if (node->super_class.empty() || node->super_class == "java/lang/Object") {
                                                break;
                                            }
                                            curr = node->super_class;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Phase 2: Reduction
    for (auto& [class_name, cf] : ctx.archive.classes()) {
        if (!ctx.is_class_in_scope(class_name)) continue;

        // Interfaces and annotations are public contracts; their members cannot be private
        if (cf.is_interface() || cf.is_annotation()) continue;

        auto& cp = cf.constant_pool();
        std::unordered_set<std::string> privatized_methods;

        // Reduce methods
        for (auto& m : cf.methods()) {
            std::string mname(m.name(cp));
            std::string mdesc(m.descriptor(cp));

            if (mname == "<init>" || mname == "<clinit>" || (mname == "main" && mdesc == "([Ljava/lang/String;)V") ||
                mname == "values" || mname == "valueOf") {
                continue;
            }
            if (m.is_native() || m.is_abstract()) continue;
            if (ctx.hierarchy.is_method_overridden_in_hierarchy(class_name, mname, mdesc)) {
                continue;
            }

            std::string key = class_name + "." + mname + mdesc;
            if (external_called_methods.find(key) == external_called_methods.end()) {
                if ((m.access_flags & bytecode::access_flags::ACC_PUBLIC) || 
                    (m.access_flags & bytecode::access_flags::ACC_PROTECTED)) {
                    m.access_flags &= ~bytecode::access_flags::ACC_PUBLIC;
                    m.access_flags &= ~bytecode::access_flags::ACC_PROTECTED;
                    m.access_flags |= bytecode::access_flags::ACC_PRIVATE;
                    privatized_methods.insert(mname + mdesc);
                    ctx.stats.members_privatized++;
                }
            }
        }

        // Fix internal invokevirtual calls to privatized instance methods -> change to invokespecial
        if (!privatized_methods.empty()) {
            for (auto& m : cf.methods()) {
                if (auto* code = m.code_mut()) {
                    auto instrs = bytecode::disassemble(code->code);
                    bool modified = false;
                    for (auto& inst : instrs) {
                        if (inst.opcode == bytecode::Opcode::invokevirtual) {
                            if (const auto* op = std::get_if<bytecode::CpIndexOperand>(&inst.operand)) {
                                if (const auto* mr = cp.get_if<bytecode::CpMethodref>(op->index)) {
                                    if (cp.get_class_name(mr->class_index) == class_name) {
                                        if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(mr->name_and_type_index)) {
                                            std::string mkey = std::string(cp.get_utf8(nat->name_index)) + std::string(cp.get_utf8(nat->descriptor_index));
                                            if (privatized_methods.contains(mkey)) {
                                                inst.opcode = bytecode::Opcode::invokespecial;
                                                modified = true;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (modified) {
                        bytecode::update_code(*code, instrs, cp);
                    }
                }
            }
        }

        // Reduce fields
        for (auto& f : cf.fields()) {
            if ((f.access_flags & bytecode::access_flags::ACC_ENUM) != 0) continue;
            std::string fname(f.name(cp));
            if (fname == "$VALUES" || fname == "ENUM$VALUES") continue;

            std::string key = class_name + "." + fname;
            if (external_accessed_fields.find(key) == external_accessed_fields.end()) {
                if ((f.access_flags & bytecode::access_flags::ACC_PUBLIC) || 
                    (f.access_flags & bytecode::access_flags::ACC_PROTECTED)) {
                    f.access_flags &= ~bytecode::access_flags::ACC_PUBLIC;
                    f.access_flags &= ~bytecode::access_flags::ACC_PROTECTED;
                    f.access_flags |= bytecode::access_flags::ACC_PRIVATE;
                    ctx.stats.members_privatized++;
                }
            }
        }
    }

    return true;
}

} // namespace obf::passes
