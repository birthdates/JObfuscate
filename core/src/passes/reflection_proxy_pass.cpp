#include "passes/reflection_proxy_pass.hpp"
#include "passes/rename_pass.hpp"
#include "bytecode/instructions.hpp"
#include "jar/jar_archive.hpp"
#include <algorithm>
#include <vector>
#include <iostream>

namespace obf::passes {

static bool should_proxy(std::string_view target_class, std::string_view current_class, const config::ReflectionProxyOptions& opts) {
    if (target_class.empty() || target_class == current_class) return false;
    if (target_class[0] == '[') return false; // array types
    if (target_class.starts_with(std::string(current_class) + "$")) return false; // internal nested/local/anonymous classes
    if (current_class.starts_with(std::string(target_class) + "$")) return false; // outer enclosing class of inner class
    if (target_class == "java/lang/Object" || target_class == "java/lang/String" || 
        target_class == "java/lang/StringBuilder" || target_class == "java/lang/System" ||
        target_class == "java/lang/Math" || target_class == "java/lang/Class" ||
        target_class == "java/lang/reflect/Method" || target_class == "java/lang/reflect/Constructor" ||
        target_class == "java/lang/reflect/Field") {
        return false; // keep basic primitives and reflection API direct
    }

    bool is_java = target_class.starts_with("java/") || target_class.starts_with("javax/") || target_class.starts_with("jdk/");
    if (is_java && !opts.proxy_java_imports) {
        return false;
    }

    // Classes in the same package never import each other in Java
    auto get_pkg = [](std::string_view c) {
        auto pos = c.rfind('/');
        return (pos == std::string_view::npos) ? std::string_view{} : c.substr(0, pos);
    };
    if (get_pkg(target_class) == get_pkg(current_class)) {
        return false;
    }

    if (!opts.import_packages.empty()) {
        for (const auto& pkg : opts.import_packages) {
            auto parsed = config::parse_package_pattern(pkg);
            if (parsed.match_prefix.empty()) return true;
            if (target_class.starts_with(parsed.match_prefix) || 
                target_class == parsed.match_prefix.substr(0, parsed.match_prefix.size() - 1)) {
                return true;
            }
        }
        return false;
    }

    return true;
}

struct ProxyParam {
    std::string desc;
    bytecode::u1 slot;
    bytecode::Opcode load_op;
    std::string box_class;
    std::string box_method;
    std::string box_desc;
};

static std::vector<ProxyParam> parse_proxy_params(std::string_view desc) {
    std::vector<ProxyParam> params;
    if (desc.empty() || desc[0] != '(') return params;
    size_t i = 1;
    bytecode::u1 current_slot = 0;

    while (i < desc.size() && desc[i] != ')') {
        char c = desc[i];
        ProxyParam p;
        p.slot = current_slot;

        if (c == 'Z') {
            p.desc = "Z";
            p.load_op = bytecode::Opcode::iload;
            p.box_class = "java/lang/Boolean";
            p.box_method = "valueOf";
            p.box_desc = "(Z)Ljava/lang/Boolean;";
            current_slot += 1;
            i += 1;
        } else if (c == 'B') {
            p.desc = "B";
            p.load_op = bytecode::Opcode::iload;
            p.box_class = "java/lang/Byte";
            p.box_method = "valueOf";
            p.box_desc = "(B)Ljava/lang/Byte;";
            current_slot += 1;
            i += 1;
        } else if (c == 'C') {
            p.desc = "C";
            p.load_op = bytecode::Opcode::iload;
            p.box_class = "java/lang/Character";
            p.box_method = "valueOf";
            p.box_desc = "(C)Ljava/lang/Character;";
            current_slot += 1;
            i += 1;
        } else if (c == 'S') {
            p.desc = "S";
            p.load_op = bytecode::Opcode::iload;
            p.box_class = "java/lang/Short";
            p.box_method = "valueOf";
            p.box_desc = "(S)Ljava/lang/Short;";
            current_slot += 1;
            i += 1;
        } else if (c == 'I') {
            p.desc = "I";
            p.load_op = bytecode::Opcode::iload;
            p.box_class = "java/lang/Integer";
            p.box_method = "valueOf";
            p.box_desc = "(I)Ljava/lang/Integer;";
            current_slot += 1;
            i += 1;
        } else if (c == 'J') {
            p.desc = "J";
            p.load_op = bytecode::Opcode::lload;
            p.box_class = "java/lang/Long";
            p.box_method = "valueOf";
            p.box_desc = "(J)Ljava/lang/Long;";
            current_slot += 2;
            i += 1;
        } else if (c == 'F') {
            p.desc = "F";
            p.load_op = bytecode::Opcode::fload;
            p.box_class = "java/lang/Float";
            p.box_method = "valueOf";
            p.box_desc = "(F)Ljava/lang/Float;";
            current_slot += 1;
            i += 1;
        } else if (c == 'D') {
            p.desc = "D";
            p.load_op = bytecode::Opcode::dload;
            p.box_class = "java/lang/Double";
            p.box_method = "valueOf";
            p.box_desc = "(D)Ljava/lang/Double;";
            current_slot += 2;
            i += 1;
        } else if (c == 'L') {
            size_t semi = desc.find(';', i);
            if (semi == std::string_view::npos) break;
            p.desc = std::string(desc.substr(i, semi - i + 1));
            p.load_op = bytecode::Opcode::aload;
            current_slot += 1;
            i = semi + 1;
        } else if (c == '[') {
            size_t start = i;
            while (i < desc.size() && desc[i] == '[') ++i;
            if (i < desc.size() && desc[i] == 'L') {
                size_t semi = desc.find(';', i);
                if (semi != std::string_view::npos) i = semi;
            }
            p.desc = std::string(desc.substr(start, i - start + 1));
            p.load_op = bytecode::Opcode::aload;
            current_slot += 1;
            i += 1;
        } else {
            break;
        }
        params.push_back(p);
    }
    return params;
}

static std::string get_return_desc(std::string_view desc) {
    size_t closing = desc.find(')');
    if (closing == std::string_view::npos) return "V";
    return std::string(desc.substr(closing + 1));
}

static std::string sanitize_type(std::string_view type_desc, std::string_view current_class, const config::ReflectionProxyOptions& opts) {
    if (type_desc.empty()) return std::string(type_desc);
    if (type_desc[0] == 'L') {
        std::string bare_class = std::string(type_desc.substr(1, type_desc.size() - 2));
        if (should_proxy(bare_class, current_class, opts)) {
            return "Ljava/lang/Object;";
        }
    } else if (type_desc[0] == '[') {
        size_t elem_start = type_desc.find_first_not_of('[');
        if (elem_start != std::string_view::npos && type_desc[elem_start] == 'L') {
            std::string bare_class = std::string(type_desc.substr(elem_start + 1, type_desc.size() - elem_start - 2));
            if (should_proxy(bare_class, current_class, opts)) {
                return std::string(elem_start, '[') + "Ljava/lang/Object;";
            }
        }
    }
    return std::string(type_desc);
}

static std::string sanitize_method_desc(std::string_view desc, std::string_view current_class, const config::ReflectionProxyOptions& opts) {
    auto params = parse_proxy_params(desc);
    std::string new_desc = "(";
    for (const auto& p : params) {
        new_desc += sanitize_type(p.desc, current_class, opts);
    }
    new_desc += ")";
    std::string ret = get_return_desc(desc);
    new_desc += sanitize_type(ret, current_class, opts);
    return new_desc;
}

template<typename F1, typename F2>
static void emit_build_param_types(std::vector<bytecode::Instruction>& p_instrs,
                                  bytecode::ConstantPool& cp,
                                  const std::vector<ProxyParam>& params,
                                  F1&& add_pi, F2&& add_pop) {
    bytecode::u2 cls_forName = cp.find_or_add_methodref("java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
    bytecode::u2 class_class = cp.find_or_add_class("java/lang/Class");

    add_pop(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(params.size())});
    add_pop(bytecode::Opcode::anewarray, bytecode::CpIndexOperand{class_class});

    for (size_t a = 0; a < params.size(); ++a) {
        const auto& param = params[a];
        add_pi(bytecode::Opcode::dup);
        add_pop(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(a)});
        if (param.desc == "Z") {
            bytecode::u2 fref = cp.find_or_add_fieldref("java/lang/Boolean", "TYPE", "Ljava/lang/Class;");
            add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{fref});
        } else if (param.desc == "B") {
            bytecode::u2 fref = cp.find_or_add_fieldref("java/lang/Byte", "TYPE", "Ljava/lang/Class;");
            add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{fref});
        } else if (param.desc == "C") {
            bytecode::u2 fref = cp.find_or_add_fieldref("java/lang/Character", "TYPE", "Ljava/lang/Class;");
            add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{fref});
        } else if (param.desc == "S") {
            bytecode::u2 fref = cp.find_or_add_fieldref("java/lang/Short", "TYPE", "Ljava/lang/Class;");
            add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{fref});
        } else if (param.desc == "I") {
            bytecode::u2 fref = cp.find_or_add_fieldref("java/lang/Integer", "TYPE", "Ljava/lang/Class;");
            add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{fref});
        } else if (param.desc == "J") {
            bytecode::u2 fref = cp.find_or_add_fieldref("java/lang/Long", "TYPE", "Ljava/lang/Class;");
            add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{fref});
        } else if (param.desc == "F") {
            bytecode::u2 fref = cp.find_or_add_fieldref("java/lang/Float", "TYPE", "Ljava/lang/Class;");
            add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{fref});
        } else if (param.desc == "D") {
            bytecode::u2 fref = cp.find_or_add_fieldref("java/lang/Double", "TYPE", "Ljava/lang/Class;");
            add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{fref});
        } else if (param.desc[0] == 'L') {
            std::string pcls = param.desc.substr(1, param.desc.size() - 2);
            std::replace(pcls.begin(), pcls.end(), '/', '.');
            bytecode::u2 sidx = cp.find_or_add_string(pcls);
            if (sidx <= 255) {
                add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{sidx});
            } else {
                add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{sidx});
            }
            add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{cls_forName});
        } else if (param.desc[0] == '[') {
            std::string pcls = param.desc;
            std::replace(pcls.begin(), pcls.end(), '/', '.');
            bytecode::u2 sidx = cp.find_or_add_string(pcls);
            if (sidx <= 255) {
                add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{sidx});
            } else {
                add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{sidx});
            }
            add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{cls_forName});
        }
        add_pi(bytecode::Opcode::aastore);
    }
}

template<typename F1, typename F2>
static void emit_build_args_array(std::vector<bytecode::Instruction>& p_instrs,
                                  bytecode::ConstantPool& cp,
                                  const std::vector<ProxyParam>& params,
                                  bytecode::u1 slot_offset,
                                  F1&& add_pi, F2&& add_pop) {
    bytecode::u2 obj_class = cp.find_or_add_class("java/lang/Object");

    add_pop(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(params.size())});
    add_pop(bytecode::Opcode::anewarray, bytecode::CpIndexOperand{obj_class});

    for (size_t a = 0; a < params.size(); ++a) {
        const auto& param = params[a];
        add_pi(bytecode::Opcode::dup);
        add_pop(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(a)});
        add_pop(param.load_op, bytecode::LocalVarOperand{static_cast<bytecode::u1>(param.slot + slot_offset)});
        if (!param.box_class.empty()) {
            bytecode::u2 box_mref = cp.find_or_add_methodref(param.box_class, param.box_method, param.box_desc);
            add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{box_mref});
        }
        add_pi(bytecode::Opcode::aastore);
    }
}

template<typename F1, typename F2>
static void emit_unbox_or_cast_return(std::vector<bytecode::Instruction>& p_instrs,
                                      bytecode::ConstantPool& cp,
                                      std::string_view ret_desc,
                                      std::string_view current_class,
                                      const config::ReflectionProxyOptions& opts,
                                      F1&& add_pi, F2&& add_pop) {
    if (ret_desc == "V") {
        add_pi(bytecode::Opcode::pop);
        add_pi(bytecode::Opcode::return_op);
    } else if (ret_desc == "I") {
        bytecode::u2 box_cls = cp.find_or_add_class("java/lang/Integer");
        bytecode::u2 unbox_m = cp.find_or_add_methodref("java/lang/Integer", "intValue", "()I");
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{box_cls});
        add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{unbox_m});
        add_pi(bytecode::Opcode::ireturn);
    } else if (ret_desc == "Z") {
        bytecode::u2 box_cls = cp.find_or_add_class("java/lang/Boolean");
        bytecode::u2 unbox_m = cp.find_or_add_methodref("java/lang/Boolean", "booleanValue", "()Z");
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{box_cls});
        add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{unbox_m});
        add_pi(bytecode::Opcode::ireturn);
    } else if (ret_desc == "B") {
        bytecode::u2 box_cls = cp.find_or_add_class("java/lang/Byte");
        bytecode::u2 unbox_m = cp.find_or_add_methodref("java/lang/Byte", "byteValue", "()B");
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{box_cls});
        add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{unbox_m});
        add_pi(bytecode::Opcode::ireturn);
    } else if (ret_desc == "C") {
        bytecode::u2 box_cls = cp.find_or_add_class("java/lang/Character");
        bytecode::u2 unbox_m = cp.find_or_add_methodref("java/lang/Character", "charValue", "()C");
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{box_cls});
        add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{unbox_m});
        add_pi(bytecode::Opcode::ireturn);
    } else if (ret_desc == "S") {
        bytecode::u2 box_cls = cp.find_or_add_class("java/lang/Short");
        bytecode::u2 unbox_m = cp.find_or_add_methodref("java/lang/Short", "shortValue", "()S");
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{box_cls});
        add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{unbox_m});
        add_pi(bytecode::Opcode::ireturn);
    } else if (ret_desc == "J") {
        bytecode::u2 box_cls = cp.find_or_add_class("java/lang/Long");
        bytecode::u2 unbox_m = cp.find_or_add_methodref("java/lang/Long", "longValue", "()J");
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{box_cls});
        add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{unbox_m});
        add_pi(bytecode::Opcode::lreturn);
    } else if (ret_desc == "F") {
        bytecode::u2 box_cls = cp.find_or_add_class("java/lang/Float");
        bytecode::u2 unbox_m = cp.find_or_add_methodref("java/lang/Float", "floatValue", "()F");
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{box_cls});
        add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{unbox_m});
        add_pi(bytecode::Opcode::freturn);
    } else if (ret_desc == "D") {
        bytecode::u2 box_cls = cp.find_or_add_class("java/lang/Double");
        bytecode::u2 unbox_m = cp.find_or_add_methodref("java/lang/Double", "doubleValue", "()D");
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{box_cls});
        add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{unbox_m});
        add_pi(bytecode::Opcode::dreturn);
    } else if (ret_desc[0] == 'L') {
        std::string bare_class = std::string(ret_desc.substr(1, ret_desc.size() - 2));
        if (should_proxy(bare_class, current_class, opts)) {
            add_pi(bytecode::Opcode::areturn);
        } else {
            bytecode::u2 ret_cls = cp.find_or_add_class(bare_class);
            add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{ret_cls});
            add_pi(bytecode::Opcode::areturn);
        }
    } else if (ret_desc[0] == '[') {
        size_t elem_start = ret_desc.find_first_not_of('[');
        if (elem_start != std::string_view::npos && ret_desc[elem_start] == 'L') {
            std::string bare_class = std::string(ret_desc.substr(elem_start + 1, ret_desc.size() - elem_start - 2));
            if (should_proxy(bare_class, current_class, opts)) {
                bytecode::u2 ret_cls = cp.find_or_add_class(std::string(elem_start, '[') + "Ljava/lang/Object;");
                add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{ret_cls});
                add_pi(bytecode::Opcode::areturn);
                return;
            }
        }
        bytecode::u2 ret_cls = cp.find_or_add_class(ret_desc);
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{ret_cls});
        add_pi(bytecode::Opcode::areturn);
    } else {
        add_pi(bytecode::Opcode::areturn);
    }
}

static std::string get_proxy_method_desc(const std::string& target_class, const std::string& target_desc, bool is_static, std::string_view current_class, const config::ReflectionProxyOptions& opts) {
    auto params = parse_proxy_params(target_desc);
    std::string new_desc = "(";
    if (!is_static) {
        new_desc += "Ljava/lang/Object;";
    }
    for (const auto& p : params) {
        new_desc += sanitize_type(p.desc, current_class, opts);
    }
    new_desc += ")";
    std::string ret = get_return_desc(target_desc);
    new_desc += sanitize_type(ret, current_class, opts);
    return new_desc;
}

static void synthesize_proxy_constructor(bytecode::ClassFile& cls,
                                        const std::string& proxy_name,
                                        const std::string& target_class,
                                        const std::string& ctor_desc,
                                        PassContext& ctx) {
    auto& cp = cls.constant_pool();
    std::string current_class(cls.name());
    std::string dot_target_class = target_class;
    std::replace(dot_target_class.begin(), dot_target_class.end(), '/', '.');

    auto params = parse_proxy_params(ctor_desc);
    std::string helper_desc = "(";
    for (const auto& p : params) {
        helper_desc += sanitize_type(p.desc, current_class, ctx.config.reflection_proxy);
    }
    helper_desc += ")Ljava/lang/Object;";

    bytecode::MethodInfo pm;
    pm.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                      bytecode::access_flags::ACC_STATIC | 
                      bytecode::access_flags::ACC_SYNTHETIC;
    pm.name_index = cp.find_or_add_utf8(proxy_name);
    pm.descriptor_index = cp.find_or_add_utf8(helper_desc);

    bytecode::u2 cls_forName = cp.find_or_add_methodref("java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
    bytecode::u2 cls_getDeclaredConstructor = cp.find_or_add_methodref("java/lang/Class", "getDeclaredConstructor", "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;");
    bytecode::u2 ctor_setAccessible = cp.find_or_add_methodref("java/lang/reflect/Constructor", "setAccessible", "(Z)V");
    bytecode::u2 ctor_newInstance = cp.find_or_add_methodref("java/lang/reflect/Constructor", "newInstance", "([Ljava/lang/Object;)Ljava/lang/Object;");
    bytecode::u2 target_cls_str = cp.find_or_add_string(dot_target_class);

    std::vector<bytecode::Instruction> p_instrs;
    auto add_pi = [&](bytecode::Opcode op) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = op;
        i.operand = bytecode::NoOperand{};
        p_instrs.push_back(i);
    };
    auto add_pop = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = op;
        i.operand = operand;
        p_instrs.push_back(i);
    };

    // 1. Class<?> c = Class.forName("TargetClass");
    if (target_cls_str <= 255) {
        add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{target_cls_str});
    } else {
        add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{target_cls_str});
    }
    add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{cls_forName});

    // 2. Class<?>[] paramTypes
    emit_build_param_types(p_instrs, cp, params, add_pi, add_pop);

    // 3. Constructor<?> ctor = c.getDeclaredConstructor(paramTypes);
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{cls_getDeclaredConstructor});

    // ctor.setAccessible(true);
    add_pi(bytecode::Opcode::dup);
    add_pi(bytecode::Opcode::iconst_1);
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{ctor_setAccessible});

    // 4. Object[] args
    emit_build_args_array(p_instrs, cp, params, 0, add_pi, add_pop);

    // 5. Object res = ctor.newInstance(args);
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{ctor_newInstance});

    // 6. Return Object directly without checkcast
    add_pi(bytecode::Opcode::areturn);

    bytecode::AttrCode p_code;
    p_code.max_stack = 8;
    p_code.max_locals = std::max<bytecode::u2>(1, static_cast<bytecode::u2>(params.empty() ? 0 : (params.back().slot + (params.back().desc == "J" || params.back().desc == "D" ? 2 : 1))));
    bytecode::update_code(p_code, p_instrs, cp);

    bytecode::Attribute pm_attr;
    pm_attr.name_index = cp.find_or_add_utf8("Code");
    pm_attr.data = std::move(p_code);
    pm.attributes.push_back(std::move(pm_attr));
    cls.methods().push_back(std::move(pm));
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

static void synthesize_proxy_class_literal(bytecode::ClassFile& cls,
                                          const std::string& proxy_name,
                                          const std::string& target_class,
                                          PassContext& ctx) {
    auto& cp = cls.constant_pool();
    std::string dot_target_class = target_class;
    std::replace(dot_target_class.begin(), dot_target_class.end(), '/', '.');

    bytecode::MethodInfo pm;
    pm.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                      bytecode::access_flags::ACC_STATIC | 
                      bytecode::access_flags::ACC_SYNTHETIC;
    pm.name_index = cp.find_or_add_utf8(proxy_name);
    pm.descriptor_index = cp.find_or_add_utf8("()Ljava/lang/Class;");

    bytecode::u2 cls_forName = cp.find_or_add_methodref("java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
    bytecode::u2 target_cls_str = cp.find_or_add_string(dot_target_class);

    std::vector<bytecode::Instruction> p_instrs;
    bytecode::Instruction ldc_i;
    ldc_i.offset = 0;
    if (target_cls_str <= 255) {
        ldc_i.opcode = bytecode::Opcode::ldc;
    } else {
        ldc_i.opcode = bytecode::Opcode::ldc_w;
    }
    ldc_i.operand = bytecode::CpIndexOperand{target_cls_str};
    p_instrs.push_back(ldc_i);

    bytecode::Instruction call_i;
    call_i.offset = 1;
    call_i.opcode = bytecode::Opcode::invokestatic;
    call_i.operand = bytecode::CpIndexOperand{cls_forName};
    p_instrs.push_back(call_i);

    bytecode::Instruction ret_i;
    ret_i.offset = 2;
    ret_i.opcode = bytecode::Opcode::areturn;
    ret_i.operand = bytecode::NoOperand{};
    p_instrs.push_back(ret_i);

    bytecode::AttrCode p_code;
    p_code.max_stack = 2;
    p_code.max_locals = 0;
    bytecode::update_code(p_code, p_instrs, cp);

    bytecode::Attribute pm_attr;
    pm_attr.name_index = cp.find_or_add_utf8("Code");
    pm_attr.data = std::move(p_code);
    pm.attributes.push_back(std::move(pm_attr));
    cls.methods().push_back(std::move(pm));
}

static void synthesize_proxy_method(bytecode::ClassFile& cls,
                                   const std::string& proxy_name,
                                   const std::string& target_class,
                                   const std::string& target_method,
                                   const std::string& target_desc,
                                   bool is_static,
                                   PassContext& ctx) {
    auto& cp = cls.constant_pool();
    std::string current_class(cls.name());
    std::string dot_target_class = target_class;
    std::replace(dot_target_class.begin(), dot_target_class.end(), '/', '.');

    auto params = parse_proxy_params(target_desc);
    std::string ret_desc = get_return_desc(target_desc);

    std::string helper_desc = get_proxy_method_desc(target_class, target_desc, is_static, current_class, ctx.config.reflection_proxy);

    bytecode::MethodInfo pm;
    pm.access_flags = bytecode::access_flags::ACC_PRIVATE | 
                      bytecode::access_flags::ACC_STATIC | 
                      bytecode::access_flags::ACC_SYNTHETIC;
    pm.name_index = cp.find_or_add_utf8(proxy_name);
    pm.descriptor_index = cp.find_or_add_utf8(helper_desc);

    bytecode::u2 cls_forName = cp.find_or_add_methodref("java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
    bytecode::u2 cls_getMethod = cp.find_or_add_methodref("java/lang/Class", "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    bytecode::u2 method_invoke = cp.find_or_add_methodref("java/lang/reflect/Method", "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");

    bytecode::u2 target_cls_str = cp.find_or_add_string(dot_target_class);
    bytecode::u2 target_m_str = cp.find_or_add_string(target_method);

    std::vector<bytecode::Instruction> p_instrs;
    auto add_pi = [&](bytecode::Opcode op) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = op;
        i.operand = bytecode::NoOperand{};
        p_instrs.push_back(i);
    };
    auto add_pop = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = op;
        i.operand = operand;
        p_instrs.push_back(i);
    };

    // 1. Class<?> c = Class.forName("TargetClass");
    if (target_cls_str <= 255) {
        add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{target_cls_str});
    } else {
        add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{target_cls_str});
    }
    add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{cls_forName});

    // 2. Method name string
    if (target_m_str <= 255) {
        add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{target_m_str});
    } else {
        add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{target_m_str});
    }

    // 3. Class<?>[] paramTypes
    emit_build_param_types(p_instrs, cp, params, add_pi, add_pop);

    // 4. Method m = c.getMethod(name, paramTypes);
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{cls_getMethod});

    // 5. Target instance: aconst_null for static, aload_0 for virtual
    if (is_static) {
        add_pi(bytecode::Opcode::aconst_null);
    } else {
        add_pi(bytecode::Opcode::aload_0);
    }

    // 6. Object[] args
    bytecode::u1 slot_offset = is_static ? 0 : 1;
    emit_build_args_array(p_instrs, cp, params, slot_offset, add_pi, add_pop);

    // 7. Method.invoke(target, args) -> Object
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{method_invoke});

    bool is_annotation_string = false;
    if (ctx.config.string_encryption.enabled && ctx.config.string_encryption.encrypt_annotation_strings && ctx.config.rename.enabled) {
        if (auto* target_cf = ctx.archive.find_class(target_class)) {
            if (target_cf->is_annotation() && ret_desc == "Ljava/lang/String;") {
                is_annotation_string = true;
            }
        }
    }

    if (is_annotation_string) {
        bytecode::u2 str_class = cp.find_or_add_class("java/lang/String");
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{str_class});
        bytecode::u2 dec_mref = get_or_create_ann_decryptor(cls);
        add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{dec_mref});
        add_pi(bytecode::Opcode::areturn);
    } else {
        // 8. Unbox / cast return
        emit_unbox_or_cast_return(p_instrs, cp, ret_desc, current_class, ctx.config.reflection_proxy, add_pi, add_pop);
    }

    bytecode::AttrCode p_code;
    p_code.max_stack = 8;
    bytecode::u2 max_loc = is_static ? 0 : 1;
    if (!params.empty()) {
        max_loc = static_cast<bytecode::u2>(params.back().slot + slot_offset + (params.back().desc == "J" || params.back().desc == "D" ? 2 : 1));
    }
    p_code.max_locals = std::max<bytecode::u2>(1, max_loc);
    bytecode::update_code(p_code, p_instrs, cp);

    bytecode::Attribute pm_attr;
    pm_attr.name_index = cp.find_or_add_utf8("Code");
    pm_attr.data = std::move(p_code);
    pm.attributes.push_back(std::move(pm_attr));
    cls.methods().push_back(std::move(pm));
}

static void synthesize_proxy_field_getter(bytecode::ClassFile& cls,
                                         const std::string& proxy_name,
                                         const std::string& target_class,
                                         const std::string& target_field,
                                         const std::string& target_desc,
                                         PassContext& ctx) {
    auto& cp = cls.constant_pool();
    std::string current_class(cls.name());
    std::string dot_target_class = target_class;
    std::replace(dot_target_class.begin(), dot_target_class.end(), '/', '.');

    std::string ret_desc = sanitize_type(target_desc, current_class, ctx.config.reflection_proxy);
    std::string helper_desc = "()" + ret_desc;

    bytecode::MethodInfo pm;
    pm.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                      bytecode::access_flags::ACC_STATIC | 
                      bytecode::access_flags::ACC_SYNTHETIC;
    pm.name_index = cp.find_or_add_utf8(proxy_name);
    pm.descriptor_index = cp.find_or_add_utf8(helper_desc);

    bytecode::u2 cls_forName = cp.find_or_add_methodref("java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
    bytecode::u2 cls_getField = cp.find_or_add_methodref("java/lang/Class", "getField", "(Ljava/lang/String;)Ljava/lang/reflect/Field;");
    bytecode::u2 field_get = cp.find_or_add_methodref("java/lang/reflect/Field", "get", "(Ljava/lang/Object;)Ljava/lang/Object;");

    bytecode::u2 target_cls_str = cp.find_or_add_string(dot_target_class);
    bytecode::u2 target_f_str = cp.find_or_add_string(target_field);

    std::vector<bytecode::Instruction> p_instrs;
    auto add_pi = [&](bytecode::Opcode op) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = op;
        i.operand = bytecode::NoOperand{};
        p_instrs.push_back(i);
    };
    auto add_pop = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = op;
        i.operand = operand;
        p_instrs.push_back(i);
    };

    // 1. Class<?> c = Class.forName("TargetClass");
    if (target_cls_str <= 255) {
        add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{target_cls_str});
    } else {
        add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{target_cls_str});
    }
    add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{cls_forName});

    // 2. Field name string
    if (target_f_str <= 255) {
        add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{target_f_str});
    } else {
        add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{target_f_str});
    }

    // 3. Field f = c.getField("fieldName");
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{cls_getField});

    // 4. aconst_null
    add_pi(bytecode::Opcode::aconst_null);

    // 5. Object val = f.get(null);
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{field_get});

    // 6. Unbox / cast
    emit_unbox_or_cast_return(p_instrs, cp, target_desc, current_class, ctx.config.reflection_proxy, add_pi, add_pop);

    bytecode::AttrCode p_code;
    p_code.max_stack = 4;
    p_code.max_locals = 1;
    bytecode::update_code(p_code, p_instrs, cp);

    bytecode::Attribute pm_attr;
    pm_attr.name_index = cp.find_or_add_utf8("Code");
    pm_attr.data = std::move(p_code);
    pm.attributes.push_back(std::move(pm_attr));
    cls.methods().push_back(std::move(pm));
}

void ReflectionProxyPass::transform_invocations(bytecode::AttrCode& code, bytecode::ClassFile& cls, PassContext& ctx) {
    auto& cp = cls.constant_pool();
    std::string current_class(cls.name());
    auto instructions = bytecode::disassemble(code.code);
    bool modified = false;

    static size_t proxy_counter = 0;
    uint64_t salt = ctx.rng();

    // 1. Pass: Detect and proxy constructor invocations (new TargetClass + dup ... invokespecial TargetClass.<init>)
    for (size_t i = 0; i < instructions.size(); ++i) {
        if (instructions[i].opcode == bytecode::Opcode::new_op) {
            const auto* cp_idx = std::get_if<bytecode::CpIndexOperand>(&instructions[i].operand);
            if (!cp_idx) continue;
            std::string target_class(cp.get_class_name(cp_idx->index));
            if (!should_proxy(target_class, current_class, ctx.config.reflection_proxy)) continue;

            if (i + 1 >= instructions.size() || instructions[i + 1].opcode != bytecode::Opcode::dup) continue;

            int new_depth = 0;
            size_t init_idx = static_cast<size_t>(-1);
            for (size_t j = i + 2; j < instructions.size(); ++j) {
                if (instructions[j].opcode == bytecode::Opcode::new_op) {
                    new_depth++;
                } else if (instructions[j].opcode == bytecode::Opcode::invokespecial) {
                    const auto* m_op = std::get_if<bytecode::CpIndexOperand>(&instructions[j].operand);
                    if (m_op) {
                        if (const auto* mr = cp.get_if<bytecode::CpMethodref>(m_op->index)) {
                            const auto* nat = cp.get_if<bytecode::CpNameAndType>(mr->name_and_type_index);
                            if (nat && cp.get_utf8(nat->name_index) == "<init>") {
                                if (new_depth > 0) {
                                    new_depth--;
                                } else {
                                    if (cp.get_class_name(mr->class_index) == target_class) {
                                        init_idx = j;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            if (init_idx != static_cast<size_t>(-1)) {
                instructions[i].opcode = bytecode::Opcode::nop;
                instructions[i].operand = bytecode::NoOperand{};
                instructions[i + 1].opcode = bytecode::Opcode::nop;
                instructions[i + 1].operand = bytecode::NoOperand{};

                const auto* m_op = std::get_if<bytecode::CpIndexOperand>(&instructions[init_idx].operand);
                const auto* mr = cp.get_if<bytecode::CpMethodref>(m_op->index);
                const auto* nat = cp.get_if<bytecode::CpNameAndType>(mr->name_and_type_index);
                std::string ctor_desc(cp.get_utf8(nat->descriptor_index));

                std::string proxy_name = RenamePass::generate_obfuscated_name(proxy_counter++, config::RenameMode::ConfusingLong, salt ^ 0xC001CAFEULL);
                synthesize_proxy_constructor(cls, proxy_name, target_class, ctor_desc, ctx);

                auto params = parse_proxy_params(ctor_desc);
                std::string helper_desc = "(";
                for (const auto& p : params) {
                    helper_desc += sanitize_type(p.desc, current_class, ctx.config.reflection_proxy);
                }
                helper_desc += ")Ljava/lang/Object;";

                bytecode::u2 proxy_mref = cp.find_or_add_methodref(current_class, proxy_name, helper_desc);
                instructions[init_idx].opcode = bytecode::Opcode::invokestatic;
                instructions[init_idx].operand = bytecode::CpIndexOperand{proxy_mref};

                ctx.proxy_methods.push_back(ProxyMethodInfo{
                    .owner_class = current_class,
                    .proxy_method_name = proxy_name,
                    .target_class = target_class,
                    .target_method = "<init>",
                    .target_desc = ctor_desc,
                    .is_field = false,
                    .is_constructor = true
                });
                ctx.stats.reflection_proxies_created++;
                modified = true;
            }
        }
    }

    // 2. Pass: Proxy static fields (getstatic) and method calls
    for (size_t idx = 0; idx < instructions.size(); ++idx) {
        auto& instr = instructions[idx];
        // Static field access
        if (instr.opcode == bytecode::Opcode::getstatic) {
            if (const auto* cp_idx = std::get_if<bytecode::CpIndexOperand>(&instr.operand)) {
                if (const auto* fr = cp.get_if<bytecode::CpFieldref>(cp_idx->index)) {
                    std::string target_class(cp.get_class_name(fr->class_index));
                    const auto* nat = cp.get_if<bytecode::CpNameAndType>(fr->name_and_type_index);
                    if (nat && should_proxy(target_class, current_class, ctx.config.reflection_proxy)) {
                        std::string target_field(cp.get_utf8(nat->name_index));
                        std::string target_desc(cp.get_utf8(nat->descriptor_index));

                        std::string proxy_name = RenamePass::generate_obfuscated_name(proxy_counter++, config::RenameMode::ConfusingLong, salt ^ 0xF1E1D001ULL);
                        synthesize_proxy_field_getter(cls, proxy_name, target_class, target_field, target_desc, ctx);

                        std::string ret_desc = sanitize_type(target_desc, current_class, ctx.config.reflection_proxy);
                        bytecode::u2 proxy_mref = cp.find_or_add_methodref(current_class, proxy_name, "()" + ret_desc);

                        instr.opcode = bytecode::Opcode::invokestatic;
                        instr.operand = bytecode::CpIndexOperand{proxy_mref};

                        ctx.proxy_methods.push_back(ProxyMethodInfo{
                            .owner_class = current_class,
                            .proxy_method_name = proxy_name,
                            .target_class = target_class,
                            .target_method = target_field,
                            .target_desc = target_desc,
                            .is_field = true,
                            .is_constructor = false
                        });
                        ctx.stats.reflection_proxies_created++;
                        modified = true;
                        continue;
                    }
                }
            }
        }

        // Invocation (invokevirtual, invokestatic, invokespecial, invokeinterface)
        if (instr.opcode == bytecode::Opcode::invokevirtual ||
            instr.opcode == bytecode::Opcode::invokestatic ||
            instr.opcode == bytecode::Opcode::invokeinterface) {

            bytecode::u2 mref_idx = 0;
            if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&instr.operand)) {
                mref_idx = cp_op->index;
            } else if (const auto* iface_op = std::get_if<bytecode::InterfaceCallOperand>(&instr.operand)) {
                mref_idx = iface_op->index;
            }

            if (mref_idx != 0) {
                std::string target_class;
                const bytecode::CpNameAndType* nat = nullptr;

                if (const auto* mr = cp.get_if<bytecode::CpMethodref>(mref_idx)) {
                    target_class = cp.get_class_name(mr->class_index);
                    nat = cp.get_if<bytecode::CpNameAndType>(mr->name_and_type_index);
                } else if (const auto* imr = cp.get_if<bytecode::CpInterfaceMethodref>(mref_idx)) {
                    target_class = cp.get_class_name(imr->class_index);
                    nat = cp.get_if<bytecode::CpNameAndType>(imr->name_and_type_index);
                }

                if (nat && should_proxy(target_class, current_class, ctx.config.reflection_proxy)) {
                    std::string target_method(cp.get_utf8(nat->name_index));
                    std::string target_desc(cp.get_utf8(nat->descriptor_index));
                    bool is_static = (instr.opcode == bytecode::Opcode::invokestatic);

                    if (target_method != "<init>" && target_method != "<clinit>" && !target_method.starts_with("access$")) {
                        std::string proxy_name = RenamePass::generate_obfuscated_name(proxy_counter++, config::RenameMode::ConfusingLong, salt ^ 0xDEADBEEFULL);
                        synthesize_proxy_method(cls, proxy_name, target_class, target_method, target_desc, is_static, ctx);

                        std::string caller_desc = get_proxy_method_desc(target_class, target_desc, is_static, current_class, ctx.config.reflection_proxy);

                        bytecode::u2 proxy_mref = cp.find_or_add_methodref(current_class, proxy_name, caller_desc);
                        instr.opcode = bytecode::Opcode::invokestatic;
                        instr.operand = bytecode::CpIndexOperand{proxy_mref};

                        ctx.proxy_methods.push_back(ProxyMethodInfo{
                            .owner_class = current_class,
                            .proxy_method_name = proxy_name,
                            .target_class = target_class,
                            .target_method = target_method,
                            .target_desc = target_desc,
                            .is_field = false,
                            .is_constructor = false
                        });
                        ctx.stats.reflection_proxies_created++;
                        modified = true;
                        continue;
                    }
                }
            }
        }
        // Class literals (ldc Class)
        if (instr.opcode == bytecode::Opcode::ldc || instr.opcode == bytecode::Opcode::ldc_w) {
            if (const auto* cp_idx = std::get_if<bytecode::CpIndexOperand>(&instr.operand)) {
                if (const auto* ce = cp.get_if<bytecode::CpClass>(cp_idx->index)) {
                    std::string target_class(cp.get_utf8(ce->name_index));
                    if (should_proxy(target_class, current_class, ctx.config.reflection_proxy)) {
                        std::string proxy_name = RenamePass::generate_obfuscated_name(proxy_counter++, config::RenameMode::ConfusingLong, salt ^ 0xC1A55001ULL);
                        synthesize_proxy_class_literal(cls, proxy_name, target_class, ctx);

                        bytecode::u2 proxy_mref = cp.find_or_add_methodref(current_class, proxy_name, "()Ljava/lang/Class;");
                        instr.opcode = bytecode::Opcode::invokestatic;
                        instr.operand = bytecode::CpIndexOperand{proxy_mref};

                        ctx.proxy_methods.push_back(ProxyMethodInfo{
                            .owner_class = current_class,
                            .proxy_method_name = proxy_name,
                            .target_class = target_class,
                            .target_method = "",
                            .target_desc = "",
                            .is_field = false,
                            .is_constructor = false,
                            .is_class_literal = true
                        });
                        ctx.stats.reflection_proxies_created++;
                        modified = true;
                        continue;
                    }
                }
            }
        }

        // Application checkcast elimination (only if safe to eliminate without breaking downstream consumers)
        if (instr.opcode == bytecode::Opcode::checkcast) {
            if (const auto* cp_idx = std::get_if<bytecode::CpIndexOperand>(&instr.operand)) {
                if (const auto* ce = cp.get_if<bytecode::CpClass>(cp_idx->index)) {
                    std::string target_class(cp.get_utf8(ce->name_index));
                    if (should_proxy(target_class, current_class, ctx.config.reflection_proxy)) {
                        bool safe_to_eliminate = false;
                        for (size_t next = idx + 1; next < instructions.size(); ++next) {
                            auto next_op = instructions[next].opcode;
                            if (next_op == bytecode::Opcode::nop) continue;
                            if (next_op == bytecode::Opcode::astore ||
                                next_op == bytecode::Opcode::astore_0 ||
                                next_op == bytecode::Opcode::astore_1 ||
                                next_op == bytecode::Opcode::astore_2 ||
                                next_op == bytecode::Opcode::astore_3 ||
                                next_op == bytecode::Opcode::pop) {
                                safe_to_eliminate = true;
                            }
                            break;
                        }
                        if (safe_to_eliminate) {
                            instr.opcode = bytecode::Opcode::nop;
                            instr.operand = bytecode::NoOperand{};
                            modified = true;
                            continue;
                        }
                    }
                }
            }
        }
    }

    auto remap_proxied_classes = [&](bytecode::u2 class_idx) -> bytecode::u2 {
        if (const auto* ce = cp.get_if<bytecode::CpClass>(class_idx)) {
            std::string cls_name(cp.get_utf8(ce->name_index));
            if (should_proxy(cls_name, current_class, ctx.config.reflection_proxy)) {
                return cp.find_or_add_class("java/lang/Object");
            }
        }
        return class_idx;
    };

    bool has_smt = false;
    for (const auto& attr : code.attributes) {
        if (attr.name(cp) == "StackMapTable") {
            has_smt = true;
            break;
        }
    }

    if (modified || has_smt) {
        bytecode::update_code(code, instructions, cp, remap_proxied_classes);
    }
}

static void sanitize_unreferenced_classes(bytecode::ClassFile& cls, PassContext& ctx) {
    cls.sanitize_unused_constants();
}

bool ReflectionProxyPass::run_on_class(bytecode::ClassFile& cls, PassContext& ctx) {
    if (!ctx.config.reflection_proxy.enabled) return true;
    if (!ctx.is_class_in_scope(cls.name())) return true;
    if (cls.is_interface() || cls.is_annotation()) return true; // Interfaces/annotations cannot have private static helper methods

    size_t num_original_methods = cls.methods().size();
    for (size_t m = 0; m < num_original_methods; ++m) {
        if (auto* code = cls.methods()[m].code_mut()) {
            transform_invocations(*code, cls, ctx);
        }
    }

    // Filter InnerClasses attribute to remove application classes that are proxied
    for (auto it = cls.attributes().begin(); it != cls.attributes().end(); ) {
        if (it->name(cls.constant_pool()) == "InnerClasses") {
            if (auto* ic = it->get_mut_if<bytecode::AttrInnerClasses>()) {
                ic->classes.erase(std::remove_if(ic->classes.begin(), ic->classes.end(), [&](const bytecode::InnerClassEntry& e) {
                    if (e.inner_class_info_index != 0) {
                        std::string ic_name(cls.constant_pool().get_class_name(e.inner_class_info_index));
                        if (should_proxy(ic_name, cls.name(), ctx.config.reflection_proxy)) {
                            return true;
                        }
                    }
                    if (e.outer_class_info_index != 0) {
                        std::string oc_name(cls.constant_pool().get_class_name(e.outer_class_info_index));
                        if (should_proxy(oc_name, cls.name(), ctx.config.reflection_proxy)) {
                            return true;
                        }
                    }
                    return false;
                }), ic->classes.end());

                if (ic->classes.empty()) {
                    it = cls.attributes().erase(it);
                    continue;
                }
            }
        }
        ++it;
    }

    // Sanitize unreferenced application classes in constant pool
    sanitize_unreferenced_classes(cls, ctx);

    return true;
}

} // namespace obf::passes
