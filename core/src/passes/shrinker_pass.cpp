#include "passes/shrinker_pass.hpp"
#include "jar/jar_archive.hpp"
#include "bytecode/instructions.hpp"
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

namespace obf::passes {

namespace {

std::string to_internal_name(std::string_view name) {
    std::string s(name);
    // Strip leading 'L' and trailing ';' if present
    if (s.size() >= 2 && s.front() == 'L' && s.back() == ';') {
        s = s.substr(1, s.size() - 2);
    }
    // Strip leading array brackets '['
    while (!s.empty() && s.front() == '[') {
        s.erase(s.begin());
    }
    if (s.size() >= 2 && s.front() == 'L' && s.back() == ';') {
        s = s.substr(1, s.size() - 2);
    }
    std::replace(s.begin(), s.end(), '.', '/');
    return s;
}

std::vector<std::string> extract_classes_from_descriptor(std::string_view desc) {
    std::vector<std::string> result;
    size_t i = 0;
    while (i < desc.size()) {
        if (desc[i] == 'L') {
            size_t end = desc.find(';', i + 1);
            if (end != std::string_view::npos) {
                std::string cls_name = to_internal_name(desc.substr(i + 1, end - i - 1));
                if (!cls_name.empty()) {
                    result.push_back(std::move(cls_name));
                }
                i = end + 1;
                continue;
            }
        }
        ++i;
    }
    return result;
}

struct MethodRef {
    std::string class_name;
    std::string method_name;
    std::string desc;
};

} // anonymous namespace

bool ShrinkerPass::run_on_archive(PassContext& ctx) {
    if (!ctx.config.shrink.enabled) return true;

    std::unordered_set<std::string> reachable_classes;
    std::unordered_set<std::string> reachable_methods;
    std::unordered_set<std::string> reachable_fields;
    std::unordered_set<std::string> entry_point_classes;
    std::unordered_set<std::string> invoked_virtual_methods;

    std::queue<std::string> pending_classes;
    std::queue<MethodRef> pending_methods;

    auto mark_class_reachable = [&](std::string_view cls_name) {
        std::string internal = to_internal_name(cls_name);
        if (internal.empty()) return;
        if (reachable_classes.insert(internal).second) {
            pending_classes.push(internal);
        }
    };

    auto mark_method_reachable = [&](std::string_view cls_name, std::string_view mname, std::string_view mdesc) {
        std::string internal_cls = to_internal_name(cls_name);
        if (internal_cls.empty()) return;
        mark_class_reachable(internal_cls);
        std::string key = internal_cls + "." + std::string(mname) + std::string(mdesc);
        if (reachable_methods.insert(key).second) {
            pending_methods.push(MethodRef{internal_cls, std::string(mname), std::string(mdesc)});
        }
    };

    auto mark_field_reachable = [&](std::string_view cls_name, std::string_view fname) {
        std::string internal_cls = to_internal_name(cls_name);
        if (internal_cls.empty()) return;
        mark_class_reachable(internal_cls);
        reachable_fields.insert(internal_cls + "." + std::string(fname));

        // Resolve declaring class in hierarchy
        std::string cur = internal_cls;
        while (!cur.empty() && cur != "java/lang/Object") {
            reachable_fields.insert(cur + "." + std::string(fname));
            const auto* node = ctx.hierarchy.get_node(cur);
            if (!node) break;
            for (const auto& iface : node->interfaces) {
                reachable_fields.insert(iface + "." + std::string(fname));
            }
            cur = node->super_class;
        }
    };

    // =========================================================================
    // Phase 1: Entry Point Identification
    // =========================================================================

    // 0. Global string/reflection cache placeholder must survive shrinking.
    // It is created eagerly before passes run but only referenced by later
    // passes (reflection proxy / string encryption), so reachability analysis
    // would otherwise treat it as dead and prune it (or its CACHE field),
    // leaving the final synthesized cache with an unobfuscated package/name.
    if (!ctx.global_cache.class_name.empty()) {
        mark_class_reachable(ctx.global_cache.class_name);
        if (!ctx.global_cache.field_name.empty()) {
            mark_field_reachable(ctx.global_cache.class_name, ctx.global_cache.field_name);
        }
    }

    // 1. Main-Class from Manifest
    if (auto main_class = ctx.archive.get_main_class()) {
        std::string main_internal = to_internal_name(*main_class);
        mark_class_reachable(main_internal);
        entry_point_classes.insert(main_internal);
    }

    // 2. Scan Manifest for other execution entry points (Premain, Agent-Class, etc.)
    if (auto manifest_opt = ctx.archive.get_manifest()) {
        std::istringstream stream(*manifest_opt);
        std::string line;
        while (std::getline(stream, line)) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string header = line.substr(0, colon);
                if (header == "Main-Class" || header == "Premain-Class" ||
                    header == "Agent-Class" || header == "Launcher-Agent-Class") {
                    std::string val = line.substr(colon + 1);
                    while (!val.empty() && val.front() == ' ') val.erase(val.begin());
                    if (!val.empty()) {
                        std::string target = to_internal_name(val);
                        mark_class_reachable(target);
                        entry_point_classes.insert(target);
                    }
                }
            }
        }
    }

    // 3. SPI Service Providers in META-INF/services/
    for (const auto& [res_name, entry] : ctx.archive.resources()) {
        if (res_name.starts_with("META-INF/services/")) {
            std::string content(reinterpret_cast<const char*>(entry.data.data()), entry.data.size());
            std::istringstream stream(content);
            std::string line;
            while (std::getline(stream, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                auto hash_pos = line.find('#');
                if (hash_pos != std::string::npos) line = line.substr(0, hash_pos);
                while (!line.empty() && line.front() == ' ') line.erase(line.begin());
                while (!line.empty() && line.back() == ' ') line.pop_back();
                if (!line.empty()) {
                    std::string target = to_internal_name(line);
                    mark_class_reachable(target);
                    entry_point_classes.insert(target);
                }
            }
        }
    }

    // 4. Scan all classes for roots (main, native, annotations, out-of-scope)
    for (const auto& [class_name, cf] : ctx.archive.classes()) {
        const auto& cp = cf.constant_pool();

        // Classes outside of obfuscation scope must NEVER be pruned, and their members are roots
        if (!ctx.is_class_in_scope(class_name)) {
            mark_class_reachable(class_name);
            entry_point_classes.insert(class_name);
            for (const auto& m : cf.methods()) {
                mark_method_reachable(class_name, m.name(cp), m.descriptor(cp));
            }
            for (const auto& f : cf.fields()) {
                mark_field_reachable(class_name, f.name(cp));
            }
            continue;
        }

        // Annotation classes must be kept intact
        if (cf.is_annotation()) {
            mark_class_reachable(class_name);
            entry_point_classes.insert(class_name);
            for (const auto& m : cf.methods()) {
                mark_method_reachable(class_name, m.name(cp), m.descriptor(cp));
            }
            continue;
        }

        // -keep classes are entry points; their matching members are roots
        // (-keepclassmembers is applied after the first fixed point instead).
        if (ctx.is_keep_protected_class(class_name)) {
            mark_class_reachable(class_name);
            entry_point_classes.insert(class_name);
            for (const auto& m : cf.methods()) {
                if (ctx.is_keep_protected_method(class_name, m.name(cp), m.descriptor(cp))) {
                    mark_method_reachable(class_name, m.name(cp), m.descriptor(cp));
                }
            }
            for (const auto& f : cf.fields()) {
                if (ctx.is_keep_protected_field(class_name, f.name(cp), f.descriptor(cp))) {
                    mark_field_reachable(class_name, f.name(cp));
                }
            }
        }

        // Check methods for main or native entry points
        for (const auto& m : cf.methods()) {
            std::string_view mname = m.name(cp);
            std::string_view mdesc = m.descriptor(cp);

            if ((m.access_flags & bytecode::access_flags::ACC_PUBLIC) &&
                (m.access_flags & bytecode::access_flags::ACC_STATIC) &&
                mname == "main" && mdesc == "([Ljava/lang/String;)V") {
                mark_class_reachable(class_name);
                mark_method_reachable(class_name, mname, mdesc);
                entry_point_classes.insert(class_name);
            }

            if (m.is_native()) {
                mark_class_reachable(class_name);
                mark_method_reachable(class_name, mname, mdesc);
            }
        }

        // Serializable classes
        if (ctx.hierarchy.is_subclass_of(class_name, "java/io/Serializable")) {
            mark_field_reachable(class_name, "serialVersionUID");
            for (const auto& m : cf.methods()) {
                std::string_view mname = m.name(cp);
                if (mname == "writeObject" || mname == "readObject" ||
                    mname == "readResolve" || mname == "writeReplace") {
                    mark_method_reachable(class_name, mname, m.descriptor(cp));
                }
            }
        }

        // Enum definitions
        if ((cf.access_flags() & bytecode::access_flags::ACC_ENUM) != 0) {
            mark_class_reachable(class_name);
            mark_method_reachable(class_name, "<clinit>", "()V");
            mark_method_reachable(class_name, "values", "()[L" + class_name + ";");
            mark_method_reachable(class_name, "valueOf", "(Ljava/lang/String;)L" + class_name + ";");
            mark_field_reachable(class_name, "$VALUES");
            mark_field_reachable(class_name, "ENUM$VALUES");
            for (const auto& f : cf.fields()) {
                if ((f.access_flags & bytecode::access_flags::ACC_ENUM) != 0) {
                    mark_field_reachable(class_name, f.name(cp));
                }
            }
            for (const auto& m : cf.methods()) {
                if (m.name(cp) == "<init>") {
                    mark_method_reachable(class_name, m.name(cp), m.descriptor(cp));
                }
            }
        }
    }

    // =========================================================================
    // Phase 2: Reachability Fixed-Point Traversal
    // (+ keepclassmembers applied once the first fixed point is reached)
    // =========================================================================

    bool keep_members_applied = false;
    while (!pending_classes.empty() || !pending_methods.empty() || !keep_members_applied) {
        // Process pending classes
        while (!pending_classes.empty()) {
            std::string cls_name = std::move(pending_classes.front());
            pending_classes.pop();

            auto* cf = ctx.archive.find_class(cls_name);
            if (!cf) continue;
            const auto& cp = cf->constant_pool();

            // Super class & interfaces
            std::string super_name(cf->super_name());
            if (!super_name.empty() && super_name != "java/lang/Object") {
                mark_class_reachable(super_name);
            }
            for (bytecode::u2 iface_idx : cf->interfaces()) {
                mark_class_reachable(cp.get_class_name(iface_idx));
            }

            // Interfaces & annotations keep all declared methods
            if (cf->is_interface() || cf->is_annotation()) {
                for (const auto& m : cf->methods()) {
                    mark_method_reachable(cls_name, m.name(cp), m.descriptor(cp));
                }
            }

            // Static initializer
            if (cf->find_method("<clinit>", "()V")) {
                mark_method_reachable(cls_name, "<clinit>", "()V");
            }

            // Default constructor for entry points
            if (entry_point_classes.contains(cls_name)) {
                if (cf->find_method("<init>", "()V")) {
                    mark_method_reachable(cls_name, "<init>", "()V");
                }
            }

            // Scan constant pool string literals for Class.forName / dynamic reflection targets
            for (size_t i = 1; i < cp.size(); ++i) {
                if (const auto* s = cp.get_if<bytecode::CpString>(static_cast<bytecode::u2>(i))) {
                    std::string_view str = cp.get_utf8(s->string_index);
                    if (!str.empty()) {
                        std::string normalized = to_internal_name(str);
                        if (normalized != cls_name && ctx.archive.find_class(normalized)) {
                            mark_class_reachable(normalized);
                            if (auto* dyn_cf = ctx.archive.find_class(normalized)) {
                                for (const auto& dm : dyn_cf->methods()) {
                                    if (dm.is_public()) {
                                        mark_method_reachable(normalized, dm.name(dyn_cf->constant_pool()), dm.descriptor(dyn_cf->constant_pool()));
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // If reachable class has annotations, mark referenced annotation types reachable
            if (cf->find_attribute("RuntimeVisibleAnnotations")) {
                for (size_t i = 1; i < cp.size(); ++i) {
                    if (const auto* u = cp.get_if<bytecode::CpUtf8>(static_cast<bytecode::u2>(i))) {
                        if (u->value.starts_with('L') && u->value.ends_with(';')) {
                            for (const auto& cname : extract_classes_from_descriptor(u->value)) {
                                if (ctx.archive.find_class(cname)) {
                                    mark_class_reachable(cname);
                                }
                            }
                        }
                    }
                }
            }

            // Check if this newly reachable class overrides any method already invoked virtually
            for (const auto& m : cf->methods()) {
                std::string key = std::string(m.name(cp)) + std::string(m.descriptor(cp));
                if (invoked_virtual_methods.contains(key)) {
                    mark_method_reachable(cls_name, m.name(cp), m.descriptor(cp));
                }
            }
        }

        // Process pending methods
        while (!pending_methods.empty()) {
            auto mref = std::move(pending_methods.front());
            pending_methods.pop();

            auto* cf = ctx.archive.find_class(mref.class_name);
            if (!cf) continue;
            const auto& cp = cf->constant_pool();

            // Extract class types from method signature
            for (const auto& ctype : extract_classes_from_descriptor(mref.desc)) {
                mark_class_reachable(ctype);
            }

            auto* mi = cf->find_method(mref.method_name, mref.desc);
            if (!mi) continue;

            // Checked exceptions in attribute
            if (const auto* exc_attr = mi->find_attribute("Exceptions", cp)) {
                if (const auto* exc = exc_attr->get_if<bytecode::AttrExceptions>()) {
                    for (bytecode::u2 cidx : exc->exception_index_table) {
                        mark_class_reachable(cp.get_class_name(cidx));
                    }
                }
            }

            auto* code = mi->code_mut();
            if (!code) continue;

            // Exception handlers
            for (const auto& handler : code->exception_table) {
                if (handler.catch_type != 0) {
                    mark_class_reachable(cp.get_class_name(handler.catch_type));
                }
            }

            // Bytecode instruction scan
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

                        if (nat_idx != 0 && !target_cls.empty()) {
                            if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(nat_idx)) {
                                std::string tmname(cp.get_utf8(nat->name_index));
                                std::string tmdesc(cp.get_utf8(nat->descriptor_index));

                                mark_method_reachable(target_cls, tmname, tmdesc);

                                if (inst.opcode == bytecode::Opcode::invokevirtual ||
                                    inst.opcode == bytecode::Opcode::invokeinterface) {
                                    invoked_virtual_methods.insert(tmname + tmdesc);

                                    // Propagate to all currently reachable subclasses
                                    for (const auto& rcls : reachable_classes) {
                                        if (rcls != target_cls && ctx.hierarchy.is_subclass_of(rcls, target_cls)) {
                                            if (auto* sub_cf = ctx.archive.find_class(rcls)) {
                                                if (sub_cf->find_method(tmname, tmdesc)) {
                                                    mark_method_reachable(rcls, tmname, tmdesc);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (inst.opcode == bytecode::Opcode::invokedynamic) {
                    if (const auto* id_op = std::get_if<bytecode::InvokeDynamicOperand>(&inst.operand)) {
                        if (const auto* indy = cp.get_if<bytecode::CpInvokeDynamic>(id_op->index)) {
                            if (const auto* bsm_attr = cf->find_attribute("BootstrapMethods")) {
                                if (const auto* bsms = bsm_attr->get_if<bytecode::AttrBootstrapMethods>()) {
                                    if (indy->bootstrap_method_attr_index < bsms->bootstrap_methods.size()) {
                                        const auto& bsm_entry = bsms->bootstrap_methods[indy->bootstrap_method_attr_index];
                                        if (const auto* mh = cp.get_if<bytecode::CpMethodHandle>(bsm_entry.bootstrap_method_ref)) {
                                            if (const auto* mr = cp.get_if<bytecode::CpMethodref>(mh->reference_index)) {
                                                mark_class_reachable(cp.get_class_name(mr->class_index));
                                            }
                                        }
                                        for (bytecode::u2 arg_idx : bsm_entry.bootstrap_arguments) {
                                            if (const auto* mh = cp.get_if<bytecode::CpMethodHandle>(arg_idx)) {
                                                std::string target_cls;
                                                bytecode::u2 nat_idx = 0;
                                                if (const auto* mr = cp.get_if<bytecode::CpMethodref>(mh->reference_index)) {
                                                    target_cls = std::string(cp.get_class_name(mr->class_index));
                                                    nat_idx = mr->name_and_type_index;
                                                } else if (const auto* imr = cp.get_if<bytecode::CpInterfaceMethodref>(mh->reference_index)) {
                                                    target_cls = std::string(cp.get_class_name(imr->class_index));
                                                    nat_idx = imr->name_and_type_index;
                                                }
                                                if (!target_cls.empty() && nat_idx != 0) {
                                                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(nat_idx)) {
                                                        mark_method_reachable(target_cls, cp.get_utf8(nat->name_index), cp.get_utf8(nat->descriptor_index));
                                                    }
                                                }
                                            }
                                        }
                                    }
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
                            if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(fr->name_and_type_index)) {
                                std::string fname(cp.get_utf8(nat->name_index));
                                std::string fdesc(cp.get_utf8(nat->descriptor_index));
                                for (const auto& cdesc : extract_classes_from_descriptor(fdesc)) {
                                    mark_class_reachable(cdesc);
                                }
                                mark_field_reachable(target_cls, fname);
                            }
                        }
                    }
                } else if (inst.opcode == bytecode::Opcode::new_op ||
                           inst.opcode == bytecode::Opcode::checkcast ||
                           inst.opcode == bytecode::Opcode::instanceof_op ||
                           inst.opcode == bytecode::Opcode::anewarray ||
                           inst.opcode == bytecode::Opcode::multianewarray) {
                    bytecode::u2 cidx = 0;
                    if (const auto* op = std::get_if<bytecode::CpIndexOperand>(&inst.operand)) {
                        cidx = op->index;
                    } else if (const auto* mana = std::get_if<bytecode::MultiANewArrayOperand>(&inst.operand)) {
                        cidx = mana->index;
                    }
                    if (cidx != 0) {
                        std::string cname(cp.get_class_name(cidx));
                        for (const auto& cdesc : extract_classes_from_descriptor(cname)) {
                            mark_class_reachable(cdesc);
                        }
                        mark_class_reachable(cname);
                    }
                } else if (inst.opcode == bytecode::Opcode::ldc ||
                           inst.opcode == bytecode::Opcode::ldc_w) {
                    bytecode::u2 cidx = 0;
                    if (const auto* op = std::get_if<bytecode::CpIndexOperand>(&inst.operand)) {
                        cidx = op->index;
                    } else if (const auto* opw = std::get_if<bytecode::CpIndexWideOperand>(&inst.operand)) {
                        cidx = opw->index;
                    }
                    if (cidx != 0) {
                        if (const auto* c = cp.get_if<bytecode::CpClass>(cidx)) {
                            mark_class_reachable(cp.get_utf8(c->name_index));
                        } else if (const auto* s = cp.get_if<bytecode::CpString>(cidx)) {
                            std::string str(cp.get_utf8(s->string_index));
                            std::string norm = to_internal_name(str);
                            if (norm != mref.class_name && ctx.archive.find_class(norm)) {
                                mark_class_reachable(norm);
                                if (auto* dyn_cf = ctx.archive.find_class(norm)) {
                                    for (const auto& dm : dyn_cf->methods()) {
                                        if (dm.is_public()) {
                                            mark_method_reachable(norm, dm.name(dyn_cf->constant_pool()), dm.descriptor(dyn_cf->constant_pool()));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // -keepclassmembers: surviving classes keep their matching members.
        // Runs once after the first fixed point; newly marked methods drain
        // through the loop above for transitive tracing.
        if (pending_classes.empty() && pending_methods.empty() && !keep_members_applied) {
            keep_members_applied = true;
            for (const auto& [cname, cf] : ctx.archive.classes()) {
                if (!ctx.is_class_in_scope(cname)) continue;
                if (!reachable_classes.contains(cname)) continue;
                const auto& cp = cf.constant_pool();
                for (const auto& m : cf.methods()) {
                    if (ctx.is_keep_protected_method(cname, m.name(cp), m.descriptor(cp))) {
                        mark_method_reachable(cname, m.name(cp), m.descriptor(cp));
                    }
                }
                for (const auto& f : cf.fields()) {
                    if (ctx.is_keep_protected_field(cname, f.name(cp), f.descriptor(cp))) {
                        mark_field_reachable(cname, f.name(cp));
                    }
                }
            }
        }
    }

    // =========================================================================
    // Phase 3: Pruning (Classes, Methods, Fields, Attributes)
    // =========================================================================

    // 1. Prune Unreachable Classes
    if (ctx.config.shrink.shrink_classes) {
        std::vector<std::string> dead_classes;
        for (const auto& [cname, cf] : ctx.archive.classes()) {
            if (!ctx.is_class_in_scope(cname)) continue;
            // Never prune the global cache placeholder: later passes populate it.
            if (!ctx.global_cache.class_name.empty() && cname == ctx.global_cache.class_name) continue;
            if (!reachable_classes.contains(cname)) {
                dead_classes.push_back(cname);
            }
        }
        for (const auto& dead_c : dead_classes) {
            ctx.archive.classes().erase(dead_c);
            ctx.stats.classes_shrunk++;
        }
    }

    // 2. Prune Unreachable Methods
    if (ctx.config.shrink.shrink_methods) {
        for (auto& [cname, cf] : ctx.archive.classes()) {
            if (!ctx.is_class_in_scope(cname)) continue;
            if (!ctx.global_cache.class_name.empty() && cname == ctx.global_cache.class_name) continue;
            if (cf.is_interface() || cf.is_annotation()) continue;

            const auto& cp = cf.constant_pool();
            bool is_enum = (cf.access_flags() & bytecode::access_flags::ACC_ENUM) != 0;

            // Check if any constructor was explicitly invoked
            bool has_any_ctor_marked = false;
            for (const auto& m : cf.methods()) {
                if (m.name(cp) == "<init>" && reachable_methods.contains(cname + "." + std::string(m.name(cp)) + std::string(m.descriptor(cp)))) {
                    has_any_ctor_marked = true;
                    break;
                }
            }

            auto& methods = cf.methods();
            auto it = methods.begin();
            while (it != methods.end()) {
                std::string mname(it->name(cp));
                std::string mdesc(it->descriptor(cp));
                std::string key = cname + "." + mname + mdesc;

                bool keep = false;
                if (reachable_methods.contains(key)) {
                    keep = true;
                } else if (mname == "<clinit>") {
                    keep = true;
                } else if (mname == "<init>" && (!has_any_ctor_marked || entry_point_classes.contains(cname))) {
                    keep = true;
                } else if (it->is_native() || it->is_abstract()) {
                    keep = true;
                } else if (is_enum && (mname == "values" || mname == "valueOf" || mname == "<init>")) {
                    keep = true;
                } else if (ctx.hierarchy.is_method_overridden_in_hierarchy(cname, mname, mdesc)) {
                    keep = true;
                }

                if (keep) {
                    ++it;
                } else {
                    it = methods.erase(it);
                    ctx.stats.methods_shrunk++;
                }
            }
        }
    }

    // 3. Prune Unreachable Fields
    if (ctx.config.shrink.shrink_fields) {
        for (auto& [cname, cf] : ctx.archive.classes()) {
            if (!ctx.is_class_in_scope(cname)) continue;
            // Never prune the global cache slot array: later passes fill it.
            bool is_global_cache = !ctx.global_cache.class_name.empty() && cname == ctx.global_cache.class_name;
            const auto& cp = cf.constant_pool();

            auto& fields = cf.fields();
            auto it = fields.begin();
            while (it != fields.end()) {
                std::string fname(it->name(cp));
                std::string key = cname + "." + fname;

                bool keep = false;
                if (is_global_cache) {
                    keep = true;
                } else if (reachable_fields.contains(key)) {
                    keep = true;
                } else if ((it->access_flags & bytecode::access_flags::ACC_ENUM) != 0) {
                    keep = true;
                } else if (fname == "$VALUES" || fname == "ENUM$VALUES" || fname == "serialVersionUID") {
                    keep = true;
                }

                if (keep) {
                    ++it;
                } else {
                    it = fields.erase(it);
                    ctx.stats.fields_shrunk++;
                }
            }
        }
    }

    // 4. Remove Non-Essential Attributes & Sanitize Constant Pools
    if (ctx.config.shrink.remove_unused_attributes) {
        static constexpr const char* const STRIP_ATTR_NAMES[] = {
            "Deprecated",
            "SourceDebugExtension"
        };

        for (auto& [cname, cf] : ctx.archive.classes()) {
            if (!ctx.is_class_in_scope(cname)) continue;
            const auto& cp = cf.constant_pool();

            for (const auto* aname : STRIP_ATTR_NAMES) {
                size_t before = cf.attributes().size();
                cf.remove_attribute(aname);
                ctx.stats.attributes_shrunk += (before - cf.attributes().size());
            }

            for (auto& m : cf.methods()) {
                for (const auto* aname : STRIP_ATTR_NAMES) {
                    size_t before = m.attributes.size();
                    m.remove_attribute(aname, cp);
                    ctx.stats.attributes_shrunk += (before - m.attributes.size());
                }
                if (auto* code = m.code_mut()) {
                    for (const auto* aname : STRIP_ATTR_NAMES) {
                        size_t before = code->attributes.size();
                        std::erase_if(code->attributes, [&](const bytecode::Attribute& a) {
                            return a.name(cp) == aname;
                        });
                        ctx.stats.attributes_shrunk += (before - code->attributes.size());
                    }
                }
            }

            for (auto& f : cf.fields()) {
                for (const auto* aname : STRIP_ATTR_NAMES) {
                    size_t before = f.attributes.size();
                    f.remove_attribute(aname, cp);
                    ctx.stats.attributes_shrunk += (before - f.attributes.size());
                }
            }

            // Sanitize unused constant pool entries after member pruning
            cf.sanitize_unused_constants();
        }
    }

    return true;
}

} // namespace obf::passes
