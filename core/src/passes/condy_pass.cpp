#include "passes/condy_pass.hpp"
#include "bytecode/instructions.hpp"
#include "reporter/reporter.hpp"
#include "jar/jar_archive.hpp"
#include <random>
#include <string>
#include <vector>
#include <unordered_map>

namespace obf::passes {

static void append_mutf8_char(std::string& out, uint16_t c) {
    if (c >= 0x0001 && c <= 0x007F) {
        out.push_back(static_cast<char>(c));
    } else if (c == 0 || (c >= 0x0080 && c <= 0x07FF)) {
        out.push_back(static_cast<char>(0xC0 | ((c >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | ((c >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

static std::string encrypt_for_condy(std::string_view str, int key) {
    std::string out;
    for (size_t i = 0; i < str.size(); ++i) {
        uint16_t c = static_cast<uint8_t>(str[i]);
        uint16_t enc = static_cast<uint16_t>(c ^ (key + static_cast<int>(i) * 17));
        append_mutf8_char(out, enc);
    }
    return out;
}

static bytecode::MethodInfo generate_condy_bsm(bytecode::ConstantPool& cp, std::string_view class_name, std::string_view bsm_name) {
    (void)class_name;
    bytecode::MethodInfo m;
    m.access_flags = bytecode::access_flags::ACC_PRIVATE |
                     bytecode::access_flags::ACC_STATIC |
                     bytecode::access_flags::ACC_SYNTHETIC;
    m.name_index = cp.find_or_add_utf8(bsm_name);
    m.descriptor_index = cp.find_or_add_utf8(
        "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/Class;ILjava/lang/String;)Ljava/lang/Object;"
    );

    bytecode::u2 toCharArray_ref = cp.find_or_add_methodref("java/lang/String", "toCharArray", "()[C");
    bytecode::u2 str_class = cp.find_or_add_class("java/lang/String");
    bytecode::u2 str_init_ref = cp.find_or_add_methodref("java/lang/String", "<init>", "([C)V");

    std::vector<bytecode::Instruction> instrs;
    bytecode::u4 cur_bci = 0;

    auto add_i = [&](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        instrs.push_back(inst);
    };

    auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        inst.operand = operand;
        instrs.push_back(inst);
    };

    // 0: aload 4 (enc)
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{4});
    // invokevirtual toCharArray
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{toCharArray_ref});
    // astore 5 (data)
    add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{5});

    // iconst_0, istore 6 (i = 0)
    add_i(bytecode::Opcode::iconst_0);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{6});

    // L_LOOP:
    bytecode::u4 bci_loop = cur_bci;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{6});
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{5});
    add_i(bytecode::Opcode::arraylength);

    size_t if_icmpge_idx = instrs.size();
    bytecode::Instruction if_end_inst;
    if_end_inst.offset = cur_bci++;
    if_end_inst.opcode = bytecode::Opcode::if_icmpge;
    if_end_inst.operand = bytecode::Branch16Operand{0, 0};
    instrs.push_back(if_end_inst);

    // data[i] = (char)(data[i] ^ (key + i * 17))
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{5});
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{6});
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{5});
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{6});
    add_i(bytecode::Opcode::caload);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3}); // key
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{6}); // i
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{17});
    add_i(bytecode::Opcode::imul);
    add_i(bytecode::Opcode::iadd);
    add_i(bytecode::Opcode::ixor);
    add_i(bytecode::Opcode::i2c);
    add_i(bytecode::Opcode::castore);

    // iinc 6, 1
    add_op(bytecode::Opcode::iinc, bytecode::IincOperand{6, 1});
    // goto L_LOOP
    add_op(bytecode::Opcode::goto_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_loop)});

    // L_END:
    bytecode::u4 bci_end = cur_bci;
    instrs[if_icmpge_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_end)};

    // return new String(data)
    add_op(bytecode::Opcode::new_op, bytecode::CpIndexOperand{str_class});
    add_i(bytecode::Opcode::dup);
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{5});
    add_op(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{str_init_ref});
    add_i(bytecode::Opcode::areturn);

    bytecode::AttrCode code;
    code.max_stack = 6;
    code.max_locals = 7;

    bytecode::u2 char_arr_cls = cp.find_or_add_class("[C");
    bytecode::BinaryWriter smt_writer;
    smt_writer.write_u2(2); // 2 entries

    smt_writer.write_u1(253); // append_frame (2 locals)
    smt_writer.write_u2(static_cast<bytecode::u2>(bci_loop));
    smt_writer.write_u1(7); smt_writer.write_u2(char_arr_cls);
    smt_writer.write_u1(1); // Integer

    bytecode::u4 delta_end = bci_end - bci_loop - 1;
    if (delta_end <= 63) {
        smt_writer.write_u1(static_cast<bytecode::u1>(delta_end));
    } else {
        smt_writer.write_u1(251); // same_frame_extended
        smt_writer.write_u2(static_cast<bytecode::u2>(delta_end));
    }

    bytecode::Attribute smt_attr;
    smt_attr.name_index = cp.find_or_add_utf8("StackMapTable");
    bytecode::AttrRaw raw;
    raw.data = std::move(smt_writer.data());
    smt_attr.data = std::move(raw);
    code.attributes.push_back(std::move(smt_attr));

    bytecode::update_code(code, instrs, cp);

    bytecode::Attribute code_attr;
    code_attr.name_index = cp.find_or_add_utf8("Code");
    code_attr.data = std::move(code);
    m.attributes.push_back(std::move(code_attr));

    return m;
}

bool CondyPass::run_on_archive(PassContext& ctx) {
    if (!ctx.config.condy.enabled) {
        return true;
    }

    size_t total_condy = 0;
    for (auto& [name, cf] : ctx.archive.classes()) {
        if (!ctx.config.is_class_in_scope(name)) continue;

        // Condy is strictly Java 11+ (major version 55+)
        if (cf.major_version() < 55) {
            continue;
        }

        if (cf.is_interface()) {
            continue;
        }

        if (transform_class(cf, ctx)) {
            total_condy++;
        }
    }

    ctx.reporter.log(reporter::LogLevel::Info,
                     "Transformed constants across " + std::to_string(total_condy) +
                     " Java 11+ classes with CONSTANT_Dynamic call sites (" +
                     std::to_string(ctx.stats.condy_call_sites_created) + " call sites created).");
    return true;
}

bool CondyPass::transform_class(bytecode::ClassFile& cf, PassContext& ctx) {
    auto& cp = cf.constant_pool();
    std::string class_name(cf.name());

    struct StringLdcTarget {
        size_t method_idx;
        size_t instr_idx;
        std::string text;
    };

    std::vector<StringLdcTarget> targets;

    for (size_t m_idx = 0; m_idx < cf.methods().size(); ++m_idx) {
        auto& method = cf.methods()[m_idx];
        auto* code = method.code();
        if (!code) continue;

        auto instrs = bytecode::disassemble(code->code);
        for (size_t i = 0; i < instrs.size(); ++i) {
            const auto& inst = instrs[i];
            if (inst.opcode == bytecode::Opcode::ldc || inst.opcode == bytecode::Opcode::ldc_w) {
                if (auto* op = std::get_if<bytecode::CpIndexOperand>(&inst.operand)) {
                    if (const auto* s = cp.get_if<bytecode::CpString>(op->index)) {
                        std::string_view text = cp.get_utf8(s->string_index);
                        if (!text.empty() && text.size() < 256) {
                            targets.push_back({m_idx, i, std::string(text)});
                        }
                    }
                }
            }
        }
    }

    if (targets.empty()) {
        return false;
    }

    // Generate Condy BSM
    std::string bsm_name = "_condy";
    auto bsm_method = generate_condy_bsm(cp, class_name, bsm_name);
    cf.methods().push_back(std::move(bsm_method));

    // Ensure BootstrapMethods attribute exists
    auto* bsm_attr_raw = cf.find_attribute_mut("BootstrapMethods");
    if (!bsm_attr_raw) {
        bytecode::Attribute new_attr;
        new_attr.name_index = cp.find_or_add_utf8("BootstrapMethods");
        new_attr.data = bytecode::AttrBootstrapMethods{};
        cf.attributes().push_back(std::move(new_attr));
        bsm_attr_raw = cf.find_attribute_mut("BootstrapMethods");
    }
    auto* bsm_attr = bsm_attr_raw->get_mut_if<bytecode::AttrBootstrapMethods>();

    bytecode::u2 bsm_mref = cp.find_or_add_methodref(
        class_name,
        bsm_name,
        "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/Class;ILjava/lang/String;)Ljava/lang/Object;"
    );
    bytecode::u2 bsm_handle = cp.find_or_add_method_handle(6 /* REF_invokeStatic */, bsm_mref);

    std::mt19937 rng(static_cast<uint32_t>(ctx.config.seed.value_or(9999) ^ 0xC011D7ULL));

    std::unordered_map<size_t, std::vector<StringLdcTarget>> by_method;
    for (const auto& t : targets) {
        by_method[t.method_idx].push_back(t);
    }

    for (auto& [m_idx, t_list] : by_method) {
        auto& method = cf.methods()[m_idx];
        auto* code = method.code_mut();
        if (!code) continue;

        auto instrs = bytecode::disassemble(code->code);
        std::unordered_map<size_t, StringLdcTarget> target_map;
        for (const auto& t : t_list) target_map[t.instr_idx] = t;

        for (size_t i = 0; i < instrs.size(); ++i) {
            auto it = target_map.find(i);
            if (it != target_map.end()) {
                int key = static_cast<int>(rng() & 0x7FFFFFFF);
                std::string enc = encrypt_for_condy(it->second.text, key);

                bytecode::u2 cp_key = cp.find_or_add_integer(key);
                bytecode::u2 cp_enc = cp.find_or_add_string(enc);

                bytecode::BootstrapMethodEntry bsm_entry;
                bsm_entry.bootstrap_method_ref = bsm_handle;
                bsm_entry.bootstrap_arguments = {cp_key, cp_enc};

                bytecode::u2 bsm_attr_index = static_cast<bytecode::u2>(bsm_attr->bootstrap_methods.size());
                bsm_attr->bootstrap_methods.push_back(bsm_entry);

                bytecode::u2 condy_idx = cp.find_or_add_dynamic(bsm_attr_index, "_", "Ljava/lang/String;");

                if (condy_idx > 255) {
                    instrs[i].opcode = bytecode::Opcode::ldc_w;
                } else {
                    instrs[i].opcode = bytecode::Opcode::ldc;
                }
                instrs[i].operand = bytecode::CpIndexOperand{condy_idx};
                ctx.stats.condy_call_sites_created++;
            }
        }

        bytecode::update_code(*code, instrs, cp);
    }

    return true;
}

} // namespace obf::passes
