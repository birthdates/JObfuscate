#include "passes/pass.hpp"
#include "jar/jar_archive.hpp"
#include "bytecode/instructions.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <unordered_set>
#include <vector>

namespace obf::passes {

void ClassHierarchy::add_class(const std::string& name, const std::string& super_name, const std::vector<std::string>& ifaces) {
    auto& node = m_nodes[name];
    node.class_name = name;
    node.super_class = super_name;
    node.interfaces = ifaces;

    if (!super_name.empty()) {
        m_nodes[super_name].subclasses.insert(name);
    }
    for (const auto& iface : ifaces) {
        m_nodes[iface].subclasses.insert(name);
    }
}

bool ClassHierarchy::is_subclass_of(const std::string& sub, const std::string& base) const {
    if (sub == base) return true;
    auto it = m_nodes.find(sub);
    if (it == m_nodes.end()) return false;

    if (!it->second.super_class.empty() && is_subclass_of(it->second.super_class, base)) {
        return true;
    }
    for (const auto& iface : it->second.interfaces) {
        if (is_subclass_of(iface, base)) return true;
    }
    return false;
}

const ClassHierarchyNode* ClassHierarchy::get_node(const std::string& name) const {
    auto it = m_nodes.find(name);
    return it != m_nodes.end() ? &it->second : nullptr;
}

bool ClassHierarchy::is_method_overridden_in_hierarchy(const std::string& class_name, const std::string& /*method_name*/, const std::string& /*desc*/) const {
    // If method is in base class or interface, consider it an override
    const auto* node = get_node(class_name);
    if (!node) return false;

    // Check super class
    if (!node->super_class.empty() && node->super_class != "java/lang/Object") {
        return true;
    }
    if (!node->interfaces.empty()) {
        return true;
    }
    return !node->subclasses.empty();
}

bool IPass::run_on_archive(PassContext& ctx) {
    bool ok = true;
    size_t count = 0;
    size_t total = ctx.archive.classes().size();

    for (auto& [name, cf] : ctx.archive.classes()) {
        ctx.reporter.progress(this->name(), count++, total, name);
        if (!run_on_class(cf, ctx)) {
            ok = false;
        }
    }
    return ok;
}

bool IPass::run_on_class(bytecode::ClassFile& /*cls*/, PassContext& /*ctx*/) {
    return true;
}

std::string PassContext::get_global_cache_class_name() const {
    if (global_cache.class_name.empty()) return "";
    auto it = class_renames.find(global_cache.class_name);
    if (it != class_renames.end()) return it->second;
    return global_cache.class_name;
}

std::string PassContext::get_global_cache_field_name() const {
    if (global_cache.field_name.empty()) return "CACHE";
    std::string fkey = global_cache.class_name + "." + global_cache.field_name;
    auto it = field_renames.find(fkey);
    if (it != field_renames.end()) return it->second;
    return global_cache.field_name;
}

void PassContext::ensure_global_cache_name() {
    if (!global_cache.class_name.empty()) return;
    std::string pkg;
    for (const auto& [name, cf] : archive.classes()) {
        if (is_class_in_scope(name)) {
            auto pos = name.rfind('/');
            if (pos != std::string::npos) {
                pkg = name.substr(0, pos + 1);
                break;
            }
        }
    }
    global_cache.class_name = pkg + "lllIlIIllIll_Cache";

    bytecode::ClassFile cf;
    cf.set_version(static_cast<bytecode::u2>(bytecode::JavaMajorVersion::Java8), 0);
    cf.set_access_flags(bytecode::access_flags::ACC_PUBLIC | bytecode::access_flags::ACC_FINAL | bytecode::access_flags::ACC_SUPER | bytecode::access_flags::ACC_SYNTHETIC);
    auto& cp = cf.constant_pool();
    cf.set_this_class(cp.find_or_add_class(global_cache.class_name));
    cf.set_super_class(cp.find_or_add_class("java/lang/Object"));

    bytecode::FieldInfo fi;
    fi.access_flags = bytecode::access_flags::ACC_PUBLIC | bytecode::access_flags::ACC_STATIC | bytecode::access_flags::ACC_FINAL | bytecode::access_flags::ACC_SYNTHETIC;
    fi.name_index = cp.find_or_add_utf8(global_cache.field_name);
    fi.descriptor_index = cp.find_or_add_utf8("[Ljava/lang/Object;");
    cf.fields().push_back(std::move(fi));

    archive.add_or_update_class(global_cache.class_name, std::move(cf));
    hierarchy.add_class(global_cache.class_name, "java/lang/Object", {});
}

bool PassContext::is_keep_protected_class(std::string_view internal_name) const {
    for (const auto& rule : keep_rules) {
        if (rule.directive != config::KeepDirective::Keep) continue;
        if (config::keep_rule_matches_class(rule, internal_name)) return true;
    }
    return false;
}

bool PassContext::is_keep_protected_method(const std::string& internal_class,
                                           std::string_view name,
                                           std::string_view desc) const {
    if (keep_rules.empty()) return false;
    auto check = [&](const std::string& cls) {
        for (const auto& rule : keep_rules) {
            if (!config::keep_rule_matches_class(rule, cls)) continue;
            if (config::keep_rule_matches_method(rule, name, desc)) return true;
        }
        return false;
    };
    if (check(internal_class)) return true;
    if (name == "<init>" || name == "<clinit>") return false;
    // Overrides across the hierarchy must stay consistent: walk supers,
    // interfaces and subclasses for a matching keep.
    std::unordered_set<std::string> visited{internal_class};
    std::vector<std::string> stack{internal_class};
    while (!stack.empty()) {
        std::string cur = std::move(stack.back());
        stack.pop_back();
        const auto* node = hierarchy.get_node(cur);
        if (!node) continue;
        auto push = [&](const std::string& next) {
            if (!next.empty() && next != "java/lang/Object" && visited.insert(next).second) {
                stack.push_back(next);
            }
        };
        push(node->super_class);
        for (const auto& iface : node->interfaces) push(iface);
        for (const auto& sub : node->subclasses) push(sub);
        if (cur != internal_class && check(cur)) return true;
    }
    return false;
}

bool PassContext::is_keep_protected_field(const std::string& internal_class,
                                          std::string_view name,
                                          std::string_view desc) const {
    for (const auto& rule : keep_rules) {
        if (!config::keep_rule_matches_class(rule, internal_class)) continue;
        if (config::keep_rule_matches_field(rule, name, desc)) return true;
    }
    return false;
}

void synthesize_global_cache_class(PassContext& ctx) {    if (ctx.global_cache.synthesized) return;
    if (ctx.global_cache.total_slots == 0 &&
        ctx.global_cache.mh_lookup_virtual.empty() &&
        ctx.global_cache.mh_lookup_static.empty() &&
        ctx.global_cache.mh_lookup_getters.empty()) {
        if (!ctx.global_cache.class_name.empty()) {
            std::string final_name = ctx.get_global_cache_class_name();
            ctx.archive.classes().erase(final_name);
            ctx.archive.classes().erase(ctx.global_cache.class_name);
        }
        return;
    }
    ctx.global_cache.synthesized = true;

    std::string final_cls_name = ctx.get_global_cache_class_name();
    std::string final_fld_name = ctx.get_global_cache_field_name();
    size_t total_slots = ctx.global_cache.total_slots;

    bytecode::ClassFile cf;
    cf.set_version(static_cast<bytecode::u2>(bytecode::JavaMajorVersion::Java8), 0);
    cf.set_access_flags(bytecode::access_flags::ACC_PUBLIC | bytecode::access_flags::ACC_FINAL | bytecode::access_flags::ACC_SUPER | bytecode::access_flags::ACC_SYNTHETIC);
    auto& cp = cf.constant_pool();
    cf.set_this_class(cp.find_or_add_class(final_cls_name));
    cf.set_super_class(cp.find_or_add_class("java/lang/Object"));

    bytecode::FieldInfo fi;
    fi.access_flags = bytecode::access_flags::ACC_PUBLIC | bytecode::access_flags::ACC_STATIC | bytecode::access_flags::ACC_FINAL | bytecode::access_flags::ACC_SYNTHETIC;
    fi.name_index = cp.find_or_add_utf8(final_fld_name);
    fi.descriptor_index = cp.find_or_add_utf8("[Ljava/lang/Object;");
    cf.fields().push_back(std::move(fi));

    // Constructor: public <init>()V
    {
        bytecode::MethodInfo mi;
        mi.access_flags = bytecode::access_flags::ACC_PUBLIC;
        mi.name_index = cp.find_or_add_utf8("<init>");
        mi.descriptor_index = cp.find_or_add_utf8("()V");

        bytecode::u2 super_init = cp.find_or_add_methodref("java/lang/Object", "<init>", "()V");
        std::vector<bytecode::Instruction> instrs;
        instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::aload_0, .offset = 0, .operand = bytecode::NoOperand{}});
        instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::invokespecial, .offset = 1, .operand = bytecode::CpIndexOperand{super_init}});
        instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::return_op, .offset = 2, .operand = bytecode::NoOperand{}});

        bytecode::AttrCode code;
        code.max_stack = 1;
        code.max_locals = 1;
        bytecode::update_code(code, instrs, cp);

        bytecode::Attribute attr;
        attr.name_index = cp.find_or_add_utf8("Code");
        attr.data = std::move(code);
        mi.attributes.push_back(std::move(attr));
        cf.methods().push_back(std::move(mi));
    }

    // Static initializer: static {} (<clinit>()V)
    {
        bytecode::MethodInfo mi;
        mi.access_flags = bytecode::access_flags::ACC_STATIC;
        mi.name_index = cp.find_or_add_utf8("<clinit>");
        mi.descriptor_index = cp.find_or_add_utf8("()V");

        std::vector<bytecode::Instruction> instrs;
        uint32_t bci = 0;
        auto add_pop = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
            bytecode::Instruction inst;
            inst.offset = bci++;
            inst.opcode = op;
            inst.operand = operand;
            instrs.push_back(inst);
        };
        if (total_slots <= 5) {
            add_pop(static_cast<bytecode::Opcode>(static_cast<int>(bytecode::Opcode::iconst_0) + total_slots), bytecode::NoOperand{});
        } else if (total_slots <= 127) {
            add_pop(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(total_slots)});
        } else if (total_slots <= 32767) {
            add_pop(bytecode::Opcode::sipush, bytecode::ShortOperand{static_cast<bytecode::i2>(total_slots)});
        } else {
            bytecode::u2 count_idx = cp.find_or_add_integer(static_cast<int32_t>(total_slots));
            add_pop((count_idx <= 255) ? bytecode::Opcode::ldc : bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{count_idx});
        }

        bytecode::u2 obj_cls = cp.find_or_add_class("java/lang/Object");
        instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::anewarray, .offset = bci++, .operand = bytecode::CpIndexOperand{obj_cls}});

        bytecode::u2 fref = cp.find_or_add_fieldref(final_cls_name, final_fld_name, "[Ljava/lang/Object;");
        instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::putstatic, .offset = bci++, .operand = bytecode::CpIndexOperand{fref}});

        instrs.push_back(bytecode::Instruction{.opcode = bytecode::Opcode::return_op, .offset = bci++, .operand = bytecode::NoOperand{}});

        bytecode::AttrCode code;
        code.max_stack = 2;
        code.max_locals = 0;
        bytecode::update_code(code, instrs, cp);

        bytecode::Attribute attr;
        attr.name_index = cp.find_or_add_utf8("Code");
        attr.data = std::move(code);
        mi.attributes.push_back(std::move(attr));
        cf.methods().push_back(std::move(mi));
    }

    // Shared MethodHandle lookup helpers (names reserved by reflection proxies).
    // Cold miss-path-only code: runs once per proxied target, then the handle
    // is cached. Temp local assignment is shuffled per helper for variety.
    if (!ctx.global_cache.mh_lookup_virtual.empty() || !ctx.global_cache.mh_lookup_static.empty() ||
        !ctx.global_cache.mh_lookup_getters.empty()) {
        bytecode::u2 public_lookup_mref = cp.find_or_add_methodref(
            "java/lang/invoke/MethodHandles", "publicLookup", "()Ljava/lang/invoke/MethodHandles$Lookup;");
        bytecode::u2 forName_mref = cp.find_or_add_methodref(
            "java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
        bytecode::u2 method_type_mref = cp.find_or_add_methodref(
            "java/lang/invoke/MethodType", "methodType",
            "(Ljava/lang/Class;[Ljava/lang/Class;)Ljava/lang/invoke/MethodType;");
        bytecode::u2 find_virtual_mref = cp.find_or_add_methodref(
            "java/lang/invoke/MethodHandles$Lookup", "findVirtual",
            "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
        bytecode::u2 find_static_mref = cp.find_or_add_methodref(
            "java/lang/invoke/MethodHandles$Lookup", "findStatic",
            "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
        bytecode::u2 find_getter_mref = cp.find_or_add_methodref(
            "java/lang/invoke/MethodHandles$Lookup", "findStaticGetter",
            "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/Class;)Ljava/lang/invoke/MethodHandle;");

        // Method shape: (String className, String memberName, Class returnType,
        // Class[] paramTypes), bound per-helper to findVirtual or findStatic.
        // Straight-line on purpose: branch targets with grown locals would
        // need explicit frames, so virtual/static get separate helpers.
        struct MhMethodJob { const std::string* name; bytecode::u2 finder; };
        std::vector<MhMethodJob> jobs;
        for (const auto& hname : ctx.global_cache.mh_lookup_virtual) {
            jobs.push_back({&hname, find_virtual_mref});
        }
        for (const auto& hname : ctx.global_cache.mh_lookup_static) {
            jobs.push_back({&hname, find_static_mref});
        }
        for (const auto& job : jobs) {
            bytecode::MethodInfo mi;
            mi.access_flags = bytecode::access_flags::ACC_PUBLIC |
                              bytecode::access_flags::ACC_STATIC |
                              bytecode::access_flags::ACC_SYNTHETIC;
            mi.name_index = cp.find_or_add_utf8(*job.name);
            mi.descriptor_index = cp.find_or_add_utf8(kMhLookupMethodDesc);

            std::array<bytecode::u1, 2> tmps{4, 5};
            std::shuffle(tmps.begin(), tmps.end(), ctx.rng);
            bytecode::u1 t_lk = tmps[0];
            bytecode::u1 t_c = tmps[1];

            std::vector<bytecode::Instruction> instrs;
            uint32_t bci = 0;
            auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
                bytecode::Instruction inst;
                inst.offset = bci++;
                inst.opcode = op;
                inst.operand = operand;
                instrs.push_back(inst);
            };
            auto add_aload = [&](bytecode::u1 idx) {
                add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{idx});
            };
            auto add_astore = [&](bytecode::u1 idx) {
                add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{idx});
            };

            add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{public_lookup_mref});
            add_astore(t_lk);
            add_aload(0);
            add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{forName_mref});
            add_astore(t_c);
            add_aload(t_lk);
            add_aload(t_c);
            add_aload(1);
            add_aload(2);
            add_aload(3);
            add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{method_type_mref});
            add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{job.finder});
            add_op(bytecode::Opcode::areturn, bytecode::NoOperand{});

            bytecode::AttrCode code;
            code.max_stack = 5;
            code.max_locals = 6;
            bytecode::update_code(code, instrs, cp);

            bytecode::Attribute attr;
            attr.name_index = cp.find_or_add_utf8("Code");
            attr.data = std::move(code);
            mi.attributes.push_back(std::move(attr));
            cf.methods().push_back(std::move(mi));
        }

        // Getter shape: (String className, String fieldName, Class fieldType).
        for (const auto& hname : ctx.global_cache.mh_lookup_getters) {
            bytecode::MethodInfo mi;
            mi.access_flags = bytecode::access_flags::ACC_PUBLIC |
                              bytecode::access_flags::ACC_STATIC |
                              bytecode::access_flags::ACC_SYNTHETIC;
            mi.name_index = cp.find_or_add_utf8(hname);
            mi.descriptor_index = cp.find_or_add_utf8(kMhLookupGetterDesc);

            std::vector<bytecode::Instruction> instrs;
            uint32_t bci = 0;
            auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
                bytecode::Instruction inst;
                inst.offset = bci++;
                inst.opcode = op;
                inst.operand = operand;
                instrs.push_back(inst);
            };

            add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{public_lookup_mref});
            add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{3});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{0});
            add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{forName_mref});
            add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{4});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{3});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{4});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{1});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{2});
            add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{find_getter_mref});
            add_op(bytecode::Opcode::areturn, bytecode::NoOperand{});

            bytecode::AttrCode code;
            code.max_stack = 5;
            code.max_locals = 5;
            bytecode::update_code(code, instrs, cp);

            bytecode::Attribute attr;
            attr.name_index = cp.find_or_add_utf8("Code");
            attr.data = std::move(code);
            mi.attributes.push_back(std::move(attr));
            cf.methods().push_back(std::move(mi));
        }
    }

    if (final_cls_name != ctx.global_cache.class_name) {
        ctx.archive.classes().erase(ctx.global_cache.class_name);
    }
    ctx.archive.add_or_update_class(final_cls_name, std::move(cf));
    ctx.hierarchy.add_class(final_cls_name, "java/lang/Object", {});
}

} // namespace obf::passes
