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
        target_class == "java/lang/StringBuilder" || target_class == "java/lang/StringBuffer" ||
        target_class == "java/lang/System" || target_class == "java/lang/Math" ||
        target_class == "java/lang/Class" ||
        target_class == "java/lang/Throwable" || target_class == "java/lang/Exception" ||
        target_class == "java/lang/RuntimeException" || target_class == "java/lang/Error" ||
        target_class.ends_with("Exception") || target_class.ends_with("Error") ||
        target_class == "java/lang/Boolean" || target_class == "java/lang/Byte" ||
        target_class == "java/lang/Character" || target_class == "java/lang/Short" ||
        target_class == "java/lang/Integer" || target_class == "java/lang/Long" ||
        target_class == "java/lang/Float" || target_class == "java/lang/Double" ||
        target_class == "java/lang/Number" ||
        target_class == "java/lang/reflect/Method" || target_class == "java/lang/reflect/Constructor" ||
        target_class == "java/lang/reflect/Field") {
        return false; // keep basic primitives, wrappers, exceptions, and reflection API direct
    }

    bool is_java = target_class.starts_with("java/") || target_class.starts_with("javax/") || target_class.starts_with("jdk/") ||
                  target_class.starts_with("kotlin/") || target_class.starts_with("kotlinx/");
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

template<typename F>
static void emit_push_int(F&& add_pop, uint32_t val, bytecode::ConstantPool& cp) {
    if (val <= 5) {
        add_pop(static_cast<bytecode::Opcode>(static_cast<int>(bytecode::Opcode::iconst_0) + val), bytecode::NoOperand{});
    } else if (val <= 127) {
        add_pop(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(val)});
    } else if (val <= 32767) {
        add_pop(bytecode::Opcode::sipush, bytecode::ShortOperand{static_cast<bytecode::i2>(val)});
    } else {
        bytecode::u2 cidx = cp.find_or_add_integer(static_cast<int32_t>(val));
        if (cidx <= 255) {
            add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{cidx});
        } else {
            add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{cidx});
        }
    }
}

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
        if (bare_class.starts_with("java/") || bare_class.starts_with("javax/") || bare_class.starts_with("jdk/") ||
            bare_class.starts_with("kotlin/") || bare_class.starts_with("kotlinx/")) {
            return std::string(type_desc);
        }
        if (should_proxy(bare_class, current_class, opts)) {
            return "Ljava/lang/Object;";
        }
    } else if (type_desc[0] == '[') {
        size_t elem_start = type_desc.find_first_not_of('[');
        if (elem_start != std::string_view::npos && type_desc[elem_start] == 'L') {
            std::string bare_class = std::string(type_desc.substr(elem_start + 1, type_desc.size() - elem_start - 2));
            if (bare_class.starts_with("java/") || bare_class.starts_with("javax/") || bare_class.starts_with("jdk/") ||
                bare_class.starts_with("kotlin/") || bare_class.starts_with("kotlinx/")) {
                return std::string(type_desc);
            }
            if (should_proxy(bare_class, current_class, opts)) {
                return std::string(elem_start, '[') + "Ljava/lang/Object;";
            }
        }
    }
    return std::string(type_desc);
}

template<typename F1, typename F2>
static void emit_build_param_types(std::vector<bytecode::Instruction>& /*p_instrs*/,
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
static void emit_build_args_array(std::vector<bytecode::Instruction>& /*p_instrs*/,
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
static void emit_unbox_or_cast_return(std::vector<bytecode::Instruction>& /*p_instrs*/,
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
        if (bare_class.starts_with("java/") || bare_class.starts_with("javax/") || bare_class.starts_with("jdk/")) {
            bytecode::u2 ret_cls = cp.find_or_add_class(bare_class);
            add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{ret_cls});
            add_pi(bytecode::Opcode::areturn);
        } else if (should_proxy(bare_class, current_class, opts)) {
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
            if (bare_class.starts_with("java/") || bare_class.starts_with("javax/") || bare_class.starts_with("jdk/")) {
                bytecode::u2 ret_cls = cp.find_or_add_class(ret_desc);
                add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{ret_cls});
                add_pi(bytecode::Opcode::areturn);
                return;
            }
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

// ---------------------------------------------------------------------------
// MethodHandle-based proxy support.
//
// Steady-state dispatch goes through MethodHandle.invokeExact (exact,
// signature-polymorphic INVOKEVIRTUAL on java/lang/invoke/MethodHandle)
// instead of Method.invoke: no Object[] allocation, no boxing/unboxing of
// primitives, and no InvocationTargetException wrapping. Handles are
// resolved once on cache miss by shared lookup helpers living in the global
// cache class (a random pool member per proxy) and cached in the global
// Object[] cache like before. Only straight-line code + one ifnonnull per
// proxy: verifier-safe on Java 8+.
// ---------------------------------------------------------------------------

template<typename F>
static void emit_ldc_string(F&& add_pop, bytecode::ConstantPool& cp, const std::string& s) {
    bytecode::u2 sidx = cp.find_or_add_string(s);
    if (sidx <= 255) {
        add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{sidx});
    } else {
        add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{sidx});
    }
}

// Push the Class object for a field/method type descriptor.
// Primitives (and V) map to Wrapper.TYPE; references/arrays via forName.
template<typename F1, typename F2>
static void emit_load_type_class(bytecode::ConstantPool& cp,
                                 std::string_view tdesc,
                                 F1&& add_pi, F2&& add_pop) {
    (void)add_pi;
    const char* box = nullptr;
    if (tdesc.size() == 1) {
        switch (tdesc[0]) {
            case 'Z': box = "java/lang/Boolean"; break;
            case 'B': box = "java/lang/Byte"; break;
            case 'C': box = "java/lang/Character"; break;
            case 'S': box = "java/lang/Short"; break;
            case 'I': box = "java/lang/Integer"; break;
            case 'J': box = "java/lang/Long"; break;
            case 'F': box = "java/lang/Float"; break;
            case 'D': box = "java/lang/Double"; break;
            case 'V': box = "java/lang/Void"; break;
            default: break;
        }
    }
    if (box) {
        bytecode::u2 fref = cp.find_or_add_fieldref(box, "TYPE", "Ljava/lang/Class;");
        add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{fref});
        return;
    }
    std::string dotted(tdesc);
    if (dotted.size() >= 2 && dotted.front() == 'L' && dotted.back() == ';') {
        // Class.forName needs the binary name (dots, no L...; wrapper).
        dotted = dotted.substr(1, dotted.size() - 2);
    }
    std::replace(dotted.begin(), dotted.end(), '/', '.');
    emit_ldc_string(add_pop, cp, dotted);
    bytecode::u2 forName = cp.find_or_add_methodref("java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
    add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{forName});
}

// Pool of shared MethodHandle lookup helpers emitted into the global cache
// class at synthesis time. The method-shape pool (virtual/static members,
// chosen per proxy) holds a randomized count in
// [mh_lookup_helpers_min, mh_lookup_helpers_max]; the getter pool holds one
// helper reserved on first static-field-proxy use. Each proxy calls a random
// pool member so call sites spread across several callees.
static size_t mh_lookup_name_counter = 0;

enum class MhLookupKind { Virtual, Static, Getter };

static const std::string& reserve_mh_lookup_name(PassContext& ctx, MhLookupKind kind) {
    auto& pool = (kind == MhLookupKind::Getter) ? ctx.global_cache.mh_lookup_getters
                 : (kind == MhLookupKind::Virtual) ? ctx.global_cache.mh_lookup_virtual
                                                   : ctx.global_cache.mh_lookup_static;
    if (!pool.empty()) {
        return pool[static_cast<size_t>(ctx.rng()) % pool.size()];
    }
    if (kind == MhLookupKind::Getter) {
        uint64_t salt = ctx.rng();
        pool.push_back(RenamePass::generate_obfuscated_name(
            mh_lookup_name_counter++, config::RenameMode::ConfusingLong, salt));
        return pool[0];
    }
    // First method-shape use: fill the shared budget across virtual/static.
    // At least one of each so later proxies of either kind are covered.
    size_t mn = ctx.config.reflection_proxy.mh_lookup_helpers_min;
    size_t mx = ctx.config.reflection_proxy.mh_lookup_helpers_max;
    if (mx < mn) std::swap(mn, mx);
    mn = std::max<size_t>(mn, 1);
    mx = std::max<size_t>(mx, mn);
    size_t total = mn + (mx > mn ? static_cast<size_t>(ctx.rng() % (mx - mn + 1)) : 0);
    total = std::max<size_t>(total, 2);
    uint64_t salt = ctx.rng();
    auto add_name = [&](std::vector<std::string>& p) {
        p.push_back(RenamePass::generate_obfuscated_name(
            mh_lookup_name_counter++, config::RenameMode::ConfusingLong,
            salt ^ (p.size() * 0x9E3779B9ULL)));
    };
    add_name(ctx.global_cache.mh_lookup_virtual);
    add_name(ctx.global_cache.mh_lookup_static);
    for (size_t i = 2; i < total; ++i) {
        if ((ctx.rng() & 1u) == 0) {
            add_name(ctx.global_cache.mh_lookup_virtual);
        } else {
            add_name(ctx.global_cache.mh_lookup_static);
        }
    }
    auto& target = (kind == MhLookupKind::Virtual) ? ctx.global_cache.mh_lookup_virtual
                                                   : ctx.global_cache.mh_lookup_static;
    return target[static_cast<size_t>(ctx.rng()) % target.size()];
}

static std::string get_proxy_method_desc(const std::string& /*target_class*/, const std::string& target_desc, bool is_static, std::string_view current_class, const config::ReflectionProxyOptions& opts) {
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
    std::string ret_desc = "L" + target_class + ";";
    std::string helper_desc = "(";
    for (const auto& p : params) {
        helper_desc += sanitize_type(p.desc, current_class, ctx.config.reflection_proxy);
    }
    helper_desc += ")" + ret_desc;

    bytecode::MethodInfo pm;
    pm.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                      bytecode::access_flags::ACC_STATIC | 
                      bytecode::access_flags::ACC_SYNTHETIC;
    pm.name_index = cp.find_or_add_utf8(proxy_name);
    pm.descriptor_index = cp.find_or_add_utf8(helper_desc);

    ctx.ensure_global_cache_name();
    size_t slot = ctx.global_cache.get_or_create_slot("c#" + target_class + "#<init>" + ctor_desc);

    bytecode::u2 cls_forName = cp.find_or_add_methodref("java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
    bytecode::u2 cls_getDeclaredConstructor = cp.find_or_add_methodref("java/lang/Class", "getDeclaredConstructor", "([Ljava/lang/Class;)Ljava/lang/reflect/Constructor;");
    bytecode::u2 ctor_setAccessible = cp.find_or_add_methodref("java/lang/reflect/Constructor", "setAccessible", "(Z)V");
    bytecode::u2 ctor_newInstance = cp.find_or_add_methodref("java/lang/reflect/Constructor", "newInstance", "([Ljava/lang/Object;)Ljava/lang/Object;");
    bytecode::u2 target_cls_str = cp.find_or_add_string(dot_target_class);
    bytecode::u2 cache_fref = cp.find_or_add_fieldref(ctx.global_cache.class_name, ctx.global_cache.field_name, "[Ljava/lang/Object;");
    bytecode::u2 ctor_cls = cp.find_or_add_class("java/lang/reflect/Constructor");

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

    // 1. Check cache
    add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{cache_fref});
    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_pi(bytecode::Opcode::aaload);

    size_t if_cached_idx = p_instrs.size();
    bytecode::Instruction if_cached_inst;
    if_cached_inst.offset = static_cast<bytecode::u4>(-1);
    if_cached_inst.opcode = bytecode::Opcode::ifnonnull;
    if_cached_inst.operand = bytecode::Branch16Operand{0, 0};
    p_instrs.push_back(if_cached_inst);

    // 2. Cache Miss:
    add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{cache_fref});
    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);

    if (target_cls_str <= 255) {
        add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{target_cls_str});
    } else {
        add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{target_cls_str});
    }
    add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{cls_forName});

    emit_build_param_types(p_instrs, cp, params, add_pi, add_pop);
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{cls_getDeclaredConstructor});

    add_pi(bytecode::Opcode::dup);
    add_pi(bytecode::Opcode::iconst_1);
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{ctor_setAccessible});

    add_pi(bytecode::Opcode::aastore);

    // 3. L_HAVE_TARGET:
    uint32_t bci_have_target = static_cast<uint32_t>(p_instrs.size() + 0x60000000);
    p_instrs[if_cached_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_have_target)};

    bytecode::Instruction load_cache_inst;
    load_cache_inst.offset = bci_have_target;
    load_cache_inst.opcode = bytecode::Opcode::getstatic;
    load_cache_inst.operand = bytecode::CpIndexOperand{cache_fref};
    p_instrs.push_back(load_cache_inst);

    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_pi(bytecode::Opcode::aaload);
    add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{ctor_cls});

    // 4. Object[] args
    emit_build_args_array(p_instrs, cp, params, 0, add_pi, add_pop);

    // 5. Object res = ctor.newInstance(args);
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{ctor_newInstance});

    // 6. Cast to target class and return
    bytecode::u2 target_cls = cp.find_or_add_class(target_class);
    add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{target_cls});
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

    ctx.ensure_global_cache_name();
    size_t slot = ctx.global_cache.get_or_create_slot("cls#" + target_class);

    bytecode::u2 cls_forName = cp.find_or_add_methodref("java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
    bytecode::u2 target_cls_str = cp.find_or_add_string(dot_target_class);
    bytecode::u2 cache_fref = cp.find_or_add_fieldref(ctx.global_cache.class_name, ctx.global_cache.field_name, "[Ljava/lang/Object;");
    bytecode::u2 class_cls = cp.find_or_add_class("java/lang/Class");

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

    // 1. Check cache
    add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{cache_fref});
    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_pi(bytecode::Opcode::aaload);

    size_t if_cached_idx = p_instrs.size();
    bytecode::Instruction if_cached_inst;
    if_cached_inst.offset = static_cast<bytecode::u4>(-1);
    if_cached_inst.opcode = bytecode::Opcode::ifnonnull;
    if_cached_inst.operand = bytecode::Branch16Operand{0, 0};
    p_instrs.push_back(if_cached_inst);

    // 2. Cache Miss:
    add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{cache_fref});
    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);

    if (target_cls_str <= 255) {
        add_pop(bytecode::Opcode::ldc, bytecode::CpIndexOperand{target_cls_str});
    } else {
        add_pop(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{target_cls_str});
    }
    add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{cls_forName});
    add_pi(bytecode::Opcode::aastore);

    // 3. L_HAVE_TARGET:
    uint32_t bci_have_target = static_cast<uint32_t>(p_instrs.size() + 0x60000000);
    p_instrs[if_cached_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_have_target)};

    bytecode::Instruction load_cache_inst;
    load_cache_inst.offset = bci_have_target;
    load_cache_inst.opcode = bytecode::Opcode::getstatic;
    load_cache_inst.operand = bytecode::CpIndexOperand{cache_fref};
    p_instrs.push_back(load_cache_inst);

    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_pi(bytecode::Opcode::aaload);
    add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{class_cls});
    add_pi(bytecode::Opcode::areturn);

    bytecode::AttrCode p_code;
    p_code.max_stack = 4;
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
    pm.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                      bytecode::access_flags::ACC_STATIC | 
                      bytecode::access_flags::ACC_SYNTHETIC;
    pm.name_index = cp.find_or_add_utf8(proxy_name);
    pm.descriptor_index = cp.find_or_add_utf8(helper_desc);

    ctx.ensure_global_cache_name();
    size_t slot = ctx.global_cache.get_or_create_slot("m#" + target_class + "#" + target_method + target_desc);

    bytecode::u2 mh_cls = cp.find_or_add_class("java/lang/invoke/MethodHandle");

    bytecode::u2 cache_fref = cp.find_or_add_fieldref(ctx.global_cache.class_name, ctx.global_cache.field_name, "[Ljava/lang/Object;");

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

    // Exact invoke descriptor uses ORIGINAL (unsanitized) types.
    std::string exact_desc = "(";
    if (!is_static) exact_desc += "L" + target_class + ";";
    for (const auto& p : params) exact_desc += p.desc;
    exact_desc += ")" + ret_desc;
    bytecode::u2 invoke_exact_mref = cp.find_or_add_methodref(
        "java/lang/invoke/MethodHandle", "invokeExact", exact_desc);

    // Temp local after the declared params for the resolved handle.
    bytecode::u1 slot_offset = is_static ? 0 : 1;
    bytecode::u2 param_end = slot_offset;
    if (!params.empty()) {
        const auto& last = params.back();
        param_end = static_cast<bytecode::u2>(last.slot + slot_offset + ((last.desc == "J" || last.desc == "D") ? 2 : 1));
    }
    bytecode::u1 L_mh = static_cast<bytecode::u1>(param_end);

    // Shared cache-class lookup helper: random pool member already bound to
    // findVirtual (instance) or findStatic (static). The ldc name strings
    // stay here so rename remapping + string encryption still apply per proxy.
    const std::string& lookup_name = reserve_mh_lookup_name(
        ctx, is_static ? MhLookupKind::Static : MhLookupKind::Virtual);
    bytecode::u2 lookup_mref = cp.find_or_add_methodref(
        ctx.global_cache.class_name, lookup_name, kMhLookupMethodDesc);

    auto add_aload = [&](bytecode::u1 idx) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = bytecode::Opcode::aload;
        i.operand = bytecode::LocalVarOperand{idx};
        p_instrs.push_back(i);
    };
    auto add_astore = [&](bytecode::u1 idx) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = bytecode::Opcode::astore;
        i.operand = bytecode::LocalVarOperand{idx};
        p_instrs.push_back(i);
    };

    // 1. Check cache
    add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{cache_fref});
    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_pi(bytecode::Opcode::aaload);

    size_t if_cached_idx = p_instrs.size();
    bytecode::Instruction if_cached_inst;
    if_cached_inst.offset = static_cast<bytecode::u4>(-1);
    if_cached_inst.opcode = bytecode::Opcode::ifnonnull;
    if_cached_inst.operand = bytecode::Branch16Operand{0, 0};
    p_instrs.push_back(if_cached_inst);

    // 2. Cache Miss: mh = CacheHelper.lookup(className, memberName, rtype, ptypes)
    // The ldc name strings are remapped by RenamePass and encrypted by
    // StringEncryptPass downstream, exactly as before.
    emit_ldc_string(add_pop, cp, dot_target_class);
    emit_ldc_string(add_pop, cp, target_method);
    emit_load_type_class(cp, ret_desc, add_pi, add_pop);
    emit_build_param_types(p_instrs, cp, params, add_pi, add_pop);
    add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{lookup_mref});
    add_astore(L_mh);

    // Store into cache
    add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{cache_fref});
    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_aload(L_mh);
    add_pi(bytecode::Opcode::aastore);

    // 3. L_HAVE_TARGET:
    uint32_t bci_have_target = static_cast<uint32_t>(p_instrs.size() + 0x60000000);
    p_instrs[if_cached_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_have_target)};

    bytecode::Instruction load_cache_inst;
    load_cache_inst.offset = bci_have_target;
    load_cache_inst.opcode = bytecode::Opcode::getstatic;
    load_cache_inst.operand = bytecode::CpIndexOperand{cache_fref};
    p_instrs.push_back(load_cache_inst);

    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_pi(bytecode::Opcode::aaload);
    add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{mh_cls});

    // 4. Push exact-typed args: receiver (virtual) + params, casting
    // sanitized Object locals back to their original types for invokeExact.
    bytecode::u2 target_cls = cp.find_or_add_class(target_class);
    if (!is_static) {
        add_pi(bytecode::Opcode::aload_0);
        add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{target_cls});
    }
    for (const auto& param : params) {
        add_pop(param.load_op, bytecode::LocalVarOperand{static_cast<bytecode::u1>(param.slot + slot_offset)});
        if (sanitize_type(param.desc, current_class, ctx.config.reflection_proxy) != param.desc) {
            // CpClass entries need the bare internal name (arrays stay as descriptors).
            std::string check_name = param.desc;
            if (check_name.size() >= 2 && check_name.front() == 'L' && check_name.back() == ';') {
                check_name = check_name.substr(1, check_name.size() - 2);
            }
            bytecode::u2 exact_cls = cp.find_or_add_class(check_name);
            add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{exact_cls});
        }
    }

    // 5. Exact-typed mh.invokeExact(...) — no boxing, no unboxing, no
    // InvocationTargetException wrapping (original exceptions propagate).
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{invoke_exact_mref});

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
    } else if (ret_desc == "V") {
        add_pi(bytecode::Opcode::return_op);
    } else if (ret_desc.size() == 1) {
        // Primitive returns come back exact from invokeExact: no unboxing.
        if (ret_desc == "J") {
            add_pi(bytecode::Opcode::lreturn);
        } else if (ret_desc == "F") {
            add_pi(bytecode::Opcode::freturn);
        } else if (ret_desc == "D") {
            add_pi(bytecode::Opcode::dreturn);
        } else {
            add_pi(bytecode::Opcode::ireturn);
        }
    } else {
        // 7. Cast reference return (exact type already; keeps rename-driven checks)
        emit_unbox_or_cast_return(p_instrs, cp, ret_desc, current_class, ctx.config.reflection_proxy, add_pi, add_pop);
    }

    bytecode::AttrCode p_code;
    p_code.max_stack = static_cast<bytecode::u2>(8 + params.size());
    p_code.max_locals = static_cast<bytecode::u2>(param_end + 1);
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

    ctx.ensure_global_cache_name();
    size_t slot = ctx.global_cache.get_or_create_slot("f#" + target_class + "#" + target_field);

    bytecode::u2 mh_cls = cp.find_or_add_class("java/lang/invoke/MethodHandle");
    // Exact getter descriptor uses the ORIGINAL field type: no boxing.
    std::string exact_desc = "()" + target_desc;
    bytecode::u2 invoke_exact_mref = cp.find_or_add_methodref(
        "java/lang/invoke/MethodHandle", "invokeExact", exact_desc);

    bytecode::u2 cache_fref = cp.find_or_add_fieldref(ctx.global_cache.class_name, ctx.global_cache.field_name, "[Ljava/lang/Object;");

    // Single temp local for the resolved handle.
    bytecode::u1 L_mh = 0;

    // Shared cache-class getter helper (reserved pool name).
    const std::string& lookup_name = reserve_mh_lookup_name(ctx, MhLookupKind::Getter);
    bytecode::u2 lookup_mref = cp.find_or_add_methodref(
        ctx.global_cache.class_name, lookup_name, kMhLookupGetterDesc);

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
    auto add_aload = [&](bytecode::u1 idx) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = bytecode::Opcode::aload;
        i.operand = bytecode::LocalVarOperand{idx};
        p_instrs.push_back(i);
    };
    auto add_astore = [&](bytecode::u1 idx) {
        bytecode::Instruction i;
        i.offset = static_cast<bytecode::u4>(-1);
        i.opcode = bytecode::Opcode::astore;
        i.operand = bytecode::LocalVarOperand{idx};
        p_instrs.push_back(i);
    };

    // 1. Check cache
    add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{cache_fref});
    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_pi(bytecode::Opcode::aaload);

    size_t if_cached_idx = p_instrs.size();
    bytecode::Instruction if_cached_inst;
    if_cached_inst.offset = static_cast<bytecode::u4>(-1);
    if_cached_inst.opcode = bytecode::Opcode::ifnonnull;
    if_cached_inst.operand = bytecode::Branch16Operand{0, 0};
    p_instrs.push_back(if_cached_inst);

    // 2. Cache Miss: mh = CacheHelper.lookupGetter(className, fieldName, fieldType)
    emit_ldc_string(add_pop, cp, dot_target_class);
    emit_ldc_string(add_pop, cp, target_field);
    emit_load_type_class(cp, target_desc, add_pi, add_pop);
    add_pop(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{lookup_mref});
    add_astore(L_mh);

    // Store into cache
    add_pop(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{cache_fref});
    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_aload(L_mh);
    add_pi(bytecode::Opcode::aastore);

    // 3. L_HAVE_TARGET:
    uint32_t bci_have_target = static_cast<uint32_t>(p_instrs.size() + 0x60000000);
    p_instrs[if_cached_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_have_target)};

    bytecode::Instruction load_cache_inst;
    load_cache_inst.offset = bci_have_target;
    load_cache_inst.opcode = bytecode::Opcode::getstatic;
    load_cache_inst.operand = bytecode::CpIndexOperand{cache_fref};
    p_instrs.push_back(load_cache_inst);

    emit_push_int(add_pop, static_cast<uint32_t>(slot), cp);
    add_pi(bytecode::Opcode::aaload);
    add_pop(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{mh_cls});

    // Exact-typed val = mh.invokeExact() — no boxing.
    add_pop(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{invoke_exact_mref});

    // 6. Return (primitives direct; references keep rename-driven checks)
    if (target_desc.size() == 1) {
        if (target_desc == "J") {
            add_pi(bytecode::Opcode::lreturn);
        } else if (target_desc == "F") {
            add_pi(bytecode::Opcode::freturn);
        } else if (target_desc == "D") {
            add_pi(bytecode::Opcode::dreturn);
        } else {
            add_pi(bytecode::Opcode::ireturn);
        }
    } else {
        emit_unbox_or_cast_return(p_instrs, cp, target_desc, current_class, ctx.config.reflection_proxy, add_pi, add_pop);
    }

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
                std::string ret_desc = "L" + target_class + ";";
                std::string helper_desc = "(";
                for (const auto& p : params) {
                    helper_desc += sanitize_type(p.desc, current_class, ctx.config.reflection_proxy);
                }
                helper_desc += ")" + ret_desc;

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
                    if (target_class.starts_with("java/") || target_class.starts_with("javax/") || target_class.starts_with("jdk/")) {
                        continue;
                    }
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

    std::unordered_set<std::string> protected_classes;
    protected_classes.insert("java/lang/Throwable");
    protected_classes.insert("java/lang/Exception");
    protected_classes.insert("java/lang/RuntimeException");
    protected_classes.insert("java/lang/Error");
    for (const auto& entry : code.exception_table) {
        if (entry.catch_type != 0) {
            protected_classes.insert(std::string(cp.get_class_name(entry.catch_type)));
        }
    }

    auto remap_proxied_classes = [&](bytecode::u2 class_idx) -> bytecode::u2 {
        if (const auto* ce = cp.get_if<bytecode::CpClass>(class_idx)) {
            std::string cls_name(cp.get_utf8(ce->name_index));
            if (protected_classes.count(cls_name) ||
                cls_name == "java/lang/Throwable" ||
                cls_name == "java/lang/Exception" ||
                cls_name == "java/lang/RuntimeException" ||
                cls_name == "java/lang/Error" ||
                cls_name.ends_with("Exception") ||
                cls_name.ends_with("Error")) {
                return class_idx;
            }
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

static void sanitize_unreferenced_classes(bytecode::ClassFile& cls, PassContext& /*ctx*/) {
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
