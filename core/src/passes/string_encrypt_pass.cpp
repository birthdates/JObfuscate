#include "passes/string_encrypt_pass.hpp"
#include "passes/rename_pass.hpp"
#include "bytecode/instructions.hpp"
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <iostream>

namespace obf::passes {

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

static void append_mutf8(std::string& out, uint16_t c) {
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

static std::string encrypt_string_variant(std::string_view input, bytecode::u4 key, size_t variant_idx) {
    size_t v = variant_idx % 4;
    if (v == 0) {
        // Variant 0: 2-Round Feistel Cipher on byte nibbles (L: high 4 bits, R: low 4 bits)
        std::string out;
        out.reserve(input.size() * 3);
        for (size_t i = 0; i < input.size(); ++i) {
            uint8_t b = static_cast<uint8_t>(input[i]);
            uint8_t l0 = (b >> 4) & 0x0F;
            uint8_t r0 = b & 0x0F;

            // Round 1: f1(r0) = (r0 * 3 + (key >> 16) + i) & 0x0F
            uint8_t f1 = static_cast<uint8_t>((r0 * 3 + (key >> 16) + i) & 0x0F);
            uint8_t l1 = r0;
            uint8_t r1 = l0 ^ f1;

            // Round 2: f2(r1) = (r1 * 7 + (key & 0xFFFF) + i * 5) & 0x0F
            uint8_t f2 = static_cast<uint8_t>((r1 * 7 + (key & 0xFFFF) + i * 5) & 0x0F);
            uint8_t l2 = r1;
            uint8_t r2 = l1 ^ f2;

            uint8_t enc = static_cast<uint8_t>((l2 << 4) | r2);
            append_mutf8(out, enc);
        }
        return out;
    } else if (v == 1) {
        // Variant 1: Keyed dynamic byte rolling XOR + Base64 encoding
        std::vector<uint8_t> enc_bytes;
        enc_bytes.reserve(input.size());
        for (size_t i = 0; i < input.size(); ++i) {
            uint8_t b = static_cast<uint8_t>(input[i]);
            uint8_t k = static_cast<uint8_t>((key >> (i % 24)) + i * 13);
            enc_bytes.push_back(b ^ k);
        }
        return base64_encode(enc_bytes);
    } else if (v == 2) {
        // Variant 2: Rolling LCG Keystream Accumulator (CFB mode)
        std::string out;
        out.reserve(input.size() * 3);
        uint32_t state = static_cast<uint32_t>(key);
        for (size_t i = 0; i < input.size(); ++i) {
            uint8_t p = static_cast<uint8_t>(input[i]);
            uint32_t next_state = state * 1103515245U + 12345U;
            uint8_t ks = static_cast<uint8_t>((next_state >> 16) & 0xFF);
            uint8_t enc = p ^ ks;
            state = next_state ^ enc;
            append_mutf8(out, enc);
        }
        return out;
    } else {
        // Variant 3: Modular Congruence Multiplicative Cipher (157 * c, inverse 181)
        std::string out;
        out.reserve(input.size() * 3);
        for (size_t i = 0; i < input.size(); ++i) {
            uint8_t p = static_cast<uint8_t>(input[i]);
            uint8_t stream = static_cast<uint8_t>((key >> 7) + i * 19 + 73);
            uint8_t t = (p ^ stream) & 0xFF;
            uint8_t c = (t * 157) & 0xFF;
            append_mutf8(out, c);
        }
        return out;
    }
}

struct MethodParam {
    std::string desc;
    bytecode::u2 slot;
    bytecode::Opcode load_op;
    std::string append_method_desc;
};

static std::vector<MethodParam> parse_method_params(std::string_view desc) {
    std::vector<MethodParam> params;
    if (desc.empty() || desc[0] != '(') return params;
    size_t i = 1;
    bytecode::u2 current_slot = 0;

    while (i < desc.size() && desc[i] != ')') {
        char c = desc[i];
        MethodParam p;
        p.slot = current_slot;

        if (c == 'B' || c == 'C' || c == 'I' || c == 'S' || c == 'Z') {
            p.desc = std::string(1, c);
            p.load_op = bytecode::Opcode::iload;
            if (c == 'Z') p.append_method_desc = "(Z)Ljava/lang/StringBuilder;";
            else if (c == 'C') p.append_method_desc = "(C)Ljava/lang/StringBuilder;";
            else p.append_method_desc = "(I)Ljava/lang/StringBuilder;";
            current_slot += 1;
            i += 1;
        } else if (c == 'J') {
            p.desc = "J";
            p.load_op = bytecode::Opcode::lload;
            p.append_method_desc = "(J)Ljava/lang/StringBuilder;";
            current_slot += 2;
            i += 1;
        } else if (c == 'F') {
            p.desc = "F";
            p.load_op = bytecode::Opcode::fload;
            p.append_method_desc = "(F)Ljava/lang/StringBuilder;";
            current_slot += 1;
            i += 1;
        } else if (c == 'D') {
            p.desc = "D";
            p.load_op = bytecode::Opcode::dload;
            p.append_method_desc = "(D)Ljava/lang/StringBuilder;";
            current_slot += 2;
            i += 1;
        } else if (c == 'L') {
            size_t semi = desc.find(';', i);
            if (semi == std::string_view::npos) break;
            p.desc = std::string(desc.substr(i, semi - i + 1));
            p.load_op = bytecode::Opcode::aload;
            if (p.desc == "Ljava/lang/String;") {
                p.append_method_desc = "(Ljava/lang/String;)Ljava/lang/StringBuilder;";
            } else {
                p.append_method_desc = "(Ljava/lang/Object;)Ljava/lang/StringBuilder;";
            }
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
            p.append_method_desc = "(Ljava/lang/Object;)Ljava/lang/StringBuilder;";
            current_slot += 1;
            i += 1;
        } else {
            break;
        }
        params.push_back(p);
    }
    return params;
}

void StringEncryptPass::transform_indy_string_concat(bytecode::ClassFile& cls, PassContext& ctx) {
    if (!ctx.config.string_encryption.encrypt_indy_concat) return;

    auto* raw_attr = cls.find_attribute_mut("BootstrapMethods");
    if (!raw_attr) return;
    auto* bsm_attr = raw_attr->get_mut_if<bytecode::AttrBootstrapMethods>();
    if (!bsm_attr) return;

    auto& cp = cls.constant_pool();
    std::string class_name(cls.name());
    size_t helper_counter = 0;

    // Track recipe strings to wipe
    std::vector<bytecode::u2> recipes_to_wipe;
    std::vector<bytecode::MethodInfo> synthesized_methods;

    for (auto& method : cls.methods()) {
        auto* code = method.code_mut();
        if (!code) continue;

        auto instructions = bytecode::disassemble(code->code);
        std::vector<bytecode::Instruction> new_instructions;
        new_instructions.reserve(instructions.size());
        bool modified = false;

        for (auto& instr : instructions) {
            if (instr.opcode == bytecode::Opcode::invokedynamic) {
                bytecode::u2 indy_cp_idx = 0;
                if (const auto* indy_op = std::get_if<bytecode::InvokeDynamicOperand>(&instr.operand)) {
                    indy_cp_idx = indy_op->index;
                } else if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&instr.operand)) {
                    indy_cp_idx = cp_op->index;
                }

                if (indy_cp_idx != 0) {
                    if (const auto* indy = cp.get_if<bytecode::CpInvokeDynamic>(indy_cp_idx)) {
                        if (indy->bootstrap_method_attr_index < bsm_attr->bootstrap_methods.size()) {
                            const auto& bsm_entry = bsm_attr->bootstrap_methods[indy->bootstrap_method_attr_index];
                            // Check if bootstrap method is StringConcatFactory
                            if (const auto* mh = cp.get_if<bytecode::CpMethodHandle>(bsm_entry.bootstrap_method_ref)) {
                                if (const auto* mr = cp.get_if<bytecode::CpMethodref>(mh->reference_index)) {
                                    std::string_view bsm_cls = cp.get_class_name(mr->class_index);
                                    if (bsm_cls == "java/lang/invoke/StringConcatFactory" && !bsm_entry.bootstrap_arguments.empty()) {
                                        bytecode::u2 recipe_cp_idx = bsm_entry.bootstrap_arguments[0];
                                        std::string recipe_str;
                                        if (const auto* str_entry = cp.get_if<bytecode::CpString>(recipe_cp_idx)) {
                                            recipe_str = std::string(cp.get_utf8(str_entry->string_index));
                                            if (recipe_str.find('\1') != std::string::npos || recipe_str.find('\2') != std::string::npos) {
                                                recipes_to_wipe.push_back(str_entry->string_index);
                                            }
                                        }

                                        const auto* nat = cp.get_if<bytecode::CpNameAndType>(indy->name_and_type_index);
                                        if (!nat) continue;
                                        std::string indy_desc(cp.get_utf8(nat->descriptor_index));
                                        auto params = parse_method_params(indy_desc);

                                        // Synthesize helper method with confusing homoglyph name
                                        std::string helper_name = RenamePass::generate_obfuscated_name(helper_counter++, config::RenameMode::ConfusingLong, 0xCAFE1234ULL);
                                        bytecode::MethodInfo hm;
                                        hm.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                                                          bytecode::access_flags::ACC_STATIC | 
                                                          bytecode::access_flags::ACC_SYNTHETIC;
                                        hm.name_index = cp.find_or_add_utf8(helper_name);
                                        hm.descriptor_index = cp.find_or_add_utf8(indy_desc);

                                        bytecode::u2 sb_class = cp.find_or_add_class("java/lang/StringBuilder");
                                        bytecode::u2 sb_init = cp.find_or_add_methodref("java/lang/StringBuilder", "<init>", "()V");
                                        bytecode::u2 sb_tostring = cp.find_or_add_methodref("java/lang/StringBuilder", "toString", "()Ljava/lang/String;");

                                        std::vector<bytecode::Instruction> hm_instrs;

                                        // new StringBuilder
                                        bytecode::Instruction new_sb;
                                        new_sb.offset = 0;
                                        new_sb.opcode = bytecode::Opcode::new_op;
                                        new_sb.operand = bytecode::CpIndexOperand{sb_class};
                                        hm_instrs.push_back(new_sb);

                                        // dup
                                        bytecode::Instruction dup_sb;
                                        dup_sb.offset = static_cast<bytecode::u4>(-1);
                                        dup_sb.opcode = bytecode::Opcode::dup;
                                        hm_instrs.push_back(dup_sb);

                                        // invokespecial StringBuilder.<init>:()V
                                        bytecode::Instruction init_sb;
                                        init_sb.offset = static_cast<bytecode::u4>(-1);
                                        init_sb.opcode = bytecode::Opcode::invokespecial;
                                        init_sb.operand = bytecode::CpIndexOperand{sb_init};
                                        hm_instrs.push_back(init_sb);

                                        // Parse recipe pieces
                                        size_t arg_idx = 0;
                                        size_t const_arg_idx = 1;
                                        std::string cur_chunk;

                                        auto flush_chunk = [&]() {
                                            if (!cur_chunk.empty()) {
                                                bytecode::u2 str_idx = cp.find_or_add_string(cur_chunk);
                                                bytecode::Instruction ldc_i;
                                                ldc_i.offset = static_cast<bytecode::u4>(-1);
                                                if (str_idx <= 255) {
                                                    ldc_i.opcode = bytecode::Opcode::ldc;
                                                    ldc_i.operand = bytecode::CpIndexOperand{str_idx};
                                                } else {
                                                    ldc_i.opcode = bytecode::Opcode::ldc_w;
                                                    ldc_i.operand = bytecode::CpIndexOperand{str_idx};
                                                }
                                                hm_instrs.push_back(ldc_i);

                                                bytecode::u2 app_ref = cp.find_or_add_methodref("java/lang/StringBuilder", "append", "(Ljava/lang/String;)Ljava/lang/StringBuilder;");
                                                bytecode::Instruction app_i;
                                                app_i.offset = static_cast<bytecode::u4>(-1);
                                                app_i.opcode = bytecode::Opcode::invokevirtual;
                                                app_i.operand = bytecode::CpIndexOperand{app_ref};
                                                hm_instrs.push_back(app_i);

                                                cur_chunk.clear();
                                            }
                                        };

                                        for (size_t r = 0; r < recipe_str.size(); ++r) {
                                            char rc = recipe_str[r];
                                            if (rc == '\1') {
                                                flush_chunk();
                                                if (arg_idx < params.size()) {
                                                    const auto& param = params[arg_idx++];
                                                    bytecode::Instruction load_i;
                                                    load_i.offset = static_cast<bytecode::u4>(-1);
                                                    load_i.opcode = param.load_op;
                                                    load_i.operand = bytecode::LocalVarOperand{static_cast<bytecode::u1>(param.slot)};
                                                    hm_instrs.push_back(load_i);

                                                    bytecode::u2 app_ref = cp.find_or_add_methodref("java/lang/StringBuilder", "append", param.append_method_desc);
                                                    bytecode::Instruction app_i;
                                                    app_i.offset = static_cast<bytecode::u4>(-1);
                                                    app_i.opcode = bytecode::Opcode::invokevirtual;
                                                    app_i.operand = bytecode::CpIndexOperand{app_ref};
                                                    hm_instrs.push_back(app_i);
                                                }
                                            } else if (rc == '\2') {
                                                flush_chunk();
                                                if (const_arg_idx < bsm_entry.bootstrap_arguments.size()) {
                                                    bytecode::u2 c_idx = bsm_entry.bootstrap_arguments[const_arg_idx++];
                                                    if (const auto* str_entry = cp.get_if<bytecode::CpString>(c_idx)) {
                                                        cur_chunk = std::string(cp.get_utf8(str_entry->string_index));
                                                        flush_chunk();
                                                    }
                                                }
                                            } else {
                                                cur_chunk += rc;
                                            }
                                        }
                                        flush_chunk();

                                        // invokevirtual StringBuilder.toString:()Ljava/lang/String;
                                        bytecode::Instruction tostring_i;
                                        tostring_i.offset = static_cast<bytecode::u4>(-1);
                                        tostring_i.opcode = bytecode::Opcode::invokevirtual;
                                        tostring_i.operand = bytecode::CpIndexOperand{sb_tostring};
                                        hm_instrs.push_back(tostring_i);

                                        // areturn
                                        bytecode::Instruction ret_i;
                                        ret_i.offset = static_cast<bytecode::u4>(-1);
                                        ret_i.opcode = bytecode::Opcode::areturn;
                                        hm_instrs.push_back(ret_i);

                                        bytecode::AttrCode hm_code;
                                        hm_code.max_stack = 4;
                                        hm_code.max_locals = static_cast<bytecode::u2>(params.empty() ? 1 : (params.back().slot + 2));
                                        bytecode::update_code(hm_code, hm_instrs, cp);

                                        bytecode::Attribute hm_attr;
                                        hm_attr.name_index = cp.find_or_add_utf8("Code");
                                        hm_attr.data = std::move(hm_code);
                                        hm.attributes.push_back(std::move(hm_attr));
                                        synthesized_methods.push_back(std::move(hm));

                                        // Replace invokedynamic with invokestatic helper
                                        bytecode::u2 helper_mref = cls.is_interface()
                                            ? cp.find_or_add_interface_methodref(class_name, helper_name, indy_desc)
                                            : cp.find_or_add_methodref(class_name, helper_name, indy_desc);
                                        bytecode::Instruction call_i;
                                        call_i.offset = instr.offset;
                                        call_i.opcode = bytecode::Opcode::invokestatic;
                                        call_i.operand = bytecode::CpIndexOperand{helper_mref};
                                        new_instructions.push_back(call_i);

                                        modified = true;
                                        continue;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            new_instructions.push_back(instr);
        }

        if (modified) {
            bytecode::update_code(*code, new_instructions, cp);
        }
    }

    for (auto& hm : synthesized_methods) {
        cls.methods().push_back(std::move(hm));
    }

    // Wipe recipe strings from constant pool
    for (bytecode::u2 u_idx : recipes_to_wipe) {
        bool shared_with_non_string = false;
        for (size_t i = 1; i < cp.size(); ++i) {
            if (const auto* c = cp.get_if<bytecode::CpClass>(static_cast<bytecode::u2>(i))) {
                if (c->name_index == u_idx) { shared_with_non_string = true; break; }
            } else if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(static_cast<bytecode::u2>(i))) {
                if (nat->name_index == u_idx || nat->descriptor_index == u_idx) {
                    shared_with_non_string = true; break;
                }
            }
        }
        if (!shared_with_non_string) {
            if (auto* u = cp.get_mut_if<bytecode::CpUtf8>(u_idx)) {
                u->value.clear();
            }
        }
    }
    if (!recipes_to_wipe.empty()) {
        cp.rebuild_utf8_lookup();
    }
}

static void inject_decryption_method_v0(bytecode::ClassFile& cls, const std::string& decrypt_name) {
    auto& cp = cls.constant_pool();
    bytecode::MethodInfo m;
    m.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                     bytecode::access_flags::ACC_STATIC | 
                     bytecode::access_flags::ACC_SYNTHETIC;
    m.name_index = cp.find_or_add_utf8(decrypt_name);
    m.descriptor_index = cp.find_or_add_utf8("(Ljava/lang/String;I)Ljava/lang/String;");

    bytecode::u2 toCharArray_ref = cp.find_or_add_methodref("java/lang/String", "toCharArray", "()[C");
    bytecode::u2 str_class = cp.find_or_add_class("java/lang/String");
    bytecode::u2 str_init_ref = cp.find_or_add_methodref("java/lang/String", "<init>", "([C)V");
    bytecode::u2 c0xFFFF_idx = cp.find_or_add_integer(0xFFFF);

    std::vector<bytecode::Instruction> instrs;
    bytecode::u4 cur_bci = 0;
    auto add_i = [&](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        instrs.push_back(inst);
    };
    auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        inst.operand = operand;
        instrs.push_back(inst);
    };

    // char[] data = str.toCharArray();
    add_i(bytecode::Opcode::aload_0);
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{toCharArray_ref});
    add_i(bytecode::Opcode::astore_2);

    // int i = 0;
    add_i(bytecode::Opcode::iconst_0);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{3});

    // L_LOOP:
    bytecode::u4 bci_loop = cur_bci;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::aload_2);
    add_i(bytecode::Opcode::arraylength);

    // if_icmpge L_END
    size_t if_icmpge_idx = instrs.size();
    bytecode::Instruction if_end_inst;
    if_end_inst.offset = cur_bci++;
    if_end_inst.opcode = bytecode::Opcode::if_icmpge;
    if_end_inst.operand = bytecode::Branch16Operand{0, 0};
    instrs.push_back(if_end_inst);

    // int b = data[i] & 0xFF;
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::caload);
    add_op(bytecode::Opcode::sipush, bytecode::ShortOperand{255});
    add_i(bytecode::Opcode::iand);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{4});

    // int l2 = (b >>> 4) & 0x0F;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{4});
    add_i(bytecode::Opcode::iconst_4);
    add_i(bytecode::Opcode::iushr);
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{0x0F});
    add_i(bytecode::Opcode::iand);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{5}); // l2 (r1)

    // int r2 = b & 0x0F;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{4});
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{0x0F});
    add_i(bytecode::Opcode::iand);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{6}); // r2

    // f2 = (r1 * 7 + (key & 0xFFFF) + i * 5) & 0x0F;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{5});
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{7});
    add_i(bytecode::Opcode::imul);
    add_i(bytecode::Opcode::iload_1);
    if (c0xFFFF_idx <= 255) {
        add_op(bytecode::Opcode::ldc, bytecode::CpIndexOperand{c0xFFFF_idx});
    } else {
        add_op(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{c0xFFFF_idx});
    }
    add_i(bytecode::Opcode::iand);
    add_i(bytecode::Opcode::iadd);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{5});
    add_i(bytecode::Opcode::imul);
    add_i(bytecode::Opcode::iadd);
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{0x0F});
    add_i(bytecode::Opcode::iand);

    // int l1 = r2 ^ f2;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{6});
    add_i(bytecode::Opcode::ixor);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{7}); // l1 (r0)

    // f1 = (r0 * 3 + (key >>> 16) + i) & 0x0F;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{7});
    add_i(bytecode::Opcode::iconst_3);
    add_i(bytecode::Opcode::imul);
    add_i(bytecode::Opcode::iload_1);
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{16});
    add_i(bytecode::Opcode::iushr);
    add_i(bytecode::Opcode::iadd);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::iadd);
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{0x0F});
    add_i(bytecode::Opcode::iand);

    // int l0 = r1 ^ f1;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{5});
    add_i(bytecode::Opcode::ixor);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{8}); // l0

    // data[i] = (char)((l0 << 4) | r0);
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{8});
    add_i(bytecode::Opcode::iconst_4);
    add_i(bytecode::Opcode::ishl);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{7});
    add_i(bytecode::Opcode::ior);
    add_i(bytecode::Opcode::i2c);
    add_i(bytecode::Opcode::castore);

    // i++
    add_op(bytecode::Opcode::iinc, bytecode::IincOperand{3, 1});

    // goto L_LOOP
    bytecode::Instruction goto_inst;
    goto_inst.offset = cur_bci++;
    goto_inst.opcode = bytecode::Opcode::goto_op;
    goto_inst.operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_loop)};
    instrs.push_back(goto_inst);

    // L_END:
    bytecode::u4 bci_end = cur_bci;
    instrs[if_icmpge_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_end)};

    // return new String(data);
    add_op(bytecode::Opcode::new_op, bytecode::CpIndexOperand{str_class});
    add_i(bytecode::Opcode::dup);
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{str_init_ref});
    add_i(bytecode::Opcode::areturn);

    bytecode::AttrCode code_attr;
    code_attr.max_stack = 6;
    code_attr.max_locals = 9;

    bytecode::u2 char_arr_cls = cp.find_or_add_class("[C");
    bytecode::BinaryWriter smt_writer;
    smt_writer.write_u2(2);

    smt_writer.write_u1(253);
    smt_writer.write_u2(static_cast<bytecode::u2>(bci_loop));
    smt_writer.write_u1(7);
    smt_writer.write_u2(char_arr_cls);
    smt_writer.write_u1(1);

    bytecode::u4 delta_end = bci_end - bci_loop - 1;
    if (delta_end <= 63) {
        smt_writer.write_u1(static_cast<bytecode::u1>(delta_end));
    } else {
        smt_writer.write_u1(251);
        smt_writer.write_u2(static_cast<bytecode::u2>(delta_end));
    }

    bytecode::Attribute smt_attr;
    smt_attr.name_index = cp.find_or_add_utf8("StackMapTable");
    bytecode::AttrRaw raw;
    raw.data = std::move(smt_writer.data());
    smt_attr.data = std::move(raw);
    code_attr.attributes.push_back(std::move(smt_attr));

    bytecode::update_code(code_attr, instrs, cp);

    bytecode::Attribute attr;
    attr.name_index = cp.find_or_add_utf8("Code");
    attr.data = std::move(code_attr);
    m.attributes.push_back(std::move(attr));
    cls.methods().push_back(std::move(m));
}

static void inject_decryption_method_v1(bytecode::ClassFile& cls, const std::string& decrypt_name) {
    auto& cp = cls.constant_pool();
    bytecode::MethodInfo m;
    m.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                     bytecode::access_flags::ACC_STATIC | 
                     bytecode::access_flags::ACC_SYNTHETIC;
    m.name_index = cp.find_or_add_utf8(decrypt_name);
    m.descriptor_index = cp.find_or_add_utf8("(Ljava/lang/String;I)Ljava/lang/String;");

    bytecode::u2 b64_getDec = cp.find_or_add_methodref("java/util/Base64", "getDecoder", "()Ljava/util/Base64$Decoder;");
    bytecode::u2 b64_decode = cp.find_or_add_methodref("java/util/Base64$Decoder", "decode", "(Ljava/lang/String;)[B");
    bytecode::u2 str_class = cp.find_or_add_class("java/lang/String");
    bytecode::u2 str_init_b = cp.find_or_add_methodref("java/lang/String", "<init>", "([BLjava/nio/charset/Charset;)V");
    bytecode::u2 utf8_fref = cp.find_or_add_fieldref("java/nio/charset/StandardCharsets", "UTF_8", "Ljava/nio/charset/Charset;");

    std::vector<bytecode::Instruction> instrs;
    bytecode::u4 cur_bci = 0;
    auto add_i = [&](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        instrs.push_back(inst);
    };
    auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        inst.operand = operand;
        instrs.push_back(inst);
    };

    // byte[] b = java.util.Base64.getDecoder().decode(str);
    add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{b64_getDec});
    add_i(bytecode::Opcode::aload_0);
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{b64_decode});
    add_i(bytecode::Opcode::astore_2);

    // int i = 0;
    add_i(bytecode::Opcode::iconst_0);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{3});

    // L_LOOP:
    bytecode::u4 bci_loop = cur_bci;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::aload_2);
    add_i(bytecode::Opcode::arraylength);

    // if_icmpge L_END
    size_t if_icmpge_idx = instrs.size();
    bytecode::Instruction if_end_inst;
    if_end_inst.offset = cur_bci++;
    if_end_inst.opcode = bytecode::Opcode::if_icmpge;
    if_end_inst.operand = bytecode::Branch16Operand{0, 0};
    instrs.push_back(if_end_inst);

    // b[i] = (byte)(b[i] ^ ((key >> (i % 24)) + i * 13));
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::baload);

    add_i(bytecode::Opcode::iload_1);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{24});
    add_i(bytecode::Opcode::irem);
    add_i(bytecode::Opcode::ishr);

    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{13});
    add_i(bytecode::Opcode::imul);
    add_i(bytecode::Opcode::iadd);

    add_i(bytecode::Opcode::ixor);
    add_i(bytecode::Opcode::i2b);
    add_i(bytecode::Opcode::bastore);

    // i++
    add_op(bytecode::Opcode::iinc, bytecode::IincOperand{3, 1});

    // goto L_LOOP
    bytecode::Instruction goto_inst;
    goto_inst.offset = cur_bci++;
    goto_inst.opcode = bytecode::Opcode::goto_op;
    goto_inst.operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_loop)};
    instrs.push_back(goto_inst);

    // L_END:
    bytecode::u4 bci_end = cur_bci;
    instrs[if_icmpge_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_end)};

    // return new String(b, StandardCharsets.UTF_8);
    add_op(bytecode::Opcode::new_op, bytecode::CpIndexOperand{str_class});
    add_i(bytecode::Opcode::dup);
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::getstatic, bytecode::CpIndexOperand{utf8_fref});
    add_op(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{str_init_b});
    add_i(bytecode::Opcode::areturn);

    bytecode::AttrCode code_attr;
    code_attr.max_stack = 6;
    code_attr.max_locals = 4;

    bytecode::u2 byte_arr_cls = cp.find_or_add_class("[B");
    bytecode::BinaryWriter smt_writer;
    smt_writer.write_u2(2);

    smt_writer.write_u1(253);
    smt_writer.write_u2(static_cast<bytecode::u2>(bci_loop));
    smt_writer.write_u1(7);
    smt_writer.write_u2(byte_arr_cls);
    smt_writer.write_u1(1);

    bytecode::u4 delta_end = bci_end - bci_loop - 1;
    if (delta_end <= 63) {
        smt_writer.write_u1(static_cast<bytecode::u1>(delta_end));
    } else {
        smt_writer.write_u1(251);
        smt_writer.write_u2(static_cast<bytecode::u2>(delta_end));
    }

    bytecode::Attribute smt_attr;
    smt_attr.name_index = cp.find_or_add_utf8("StackMapTable");
    bytecode::AttrRaw raw;
    raw.data = std::move(smt_writer.data());
    smt_attr.data = std::move(raw);
    code_attr.attributes.push_back(std::move(smt_attr));

    bytecode::update_code(code_attr, instrs, cp);

    bytecode::Attribute attr;
    attr.name_index = cp.find_or_add_utf8("Code");
    attr.data = std::move(code_attr);
    m.attributes.push_back(std::move(attr));
    cls.methods().push_back(std::move(m));
}

static void inject_decryption_method_v2(bytecode::ClassFile& cls, const std::string& decrypt_name) {
    auto& cp = cls.constant_pool();
    bytecode::MethodInfo m;
    m.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                     bytecode::access_flags::ACC_STATIC | 
                     bytecode::access_flags::ACC_SYNTHETIC;
    m.name_index = cp.find_or_add_utf8(decrypt_name);
    m.descriptor_index = cp.find_or_add_utf8("(Ljava/lang/String;I)Ljava/lang/String;");

    bytecode::u2 toCharArray_ref = cp.find_or_add_methodref("java/lang/String", "toCharArray", "()[C");
    bytecode::u2 str_class = cp.find_or_add_class("java/lang/String");
    bytecode::u2 str_init_ref = cp.find_or_add_methodref("java/lang/String", "<init>", "([C)V");
    bytecode::u2 mult_idx = cp.find_or_add_integer(1103515245);
    bytecode::u2 add_idx = cp.find_or_add_integer(12345);

    std::vector<bytecode::Instruction> instrs;
    bytecode::u4 cur_bci = 0;
    auto add_i = [&](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        instrs.push_back(inst);
    };
    auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        inst.operand = operand;
        instrs.push_back(inst);
    };

    // char[] data = str.toCharArray();
    add_i(bytecode::Opcode::aload_0);
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{toCharArray_ref});
    add_i(bytecode::Opcode::astore_2);

    // int state = key;
    add_i(bytecode::Opcode::iload_1);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{3});

    // int i = 0;
    add_i(bytecode::Opcode::iconst_0);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{4});

    // L_LOOP:
    bytecode::u4 bci_loop = cur_bci;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{4});
    add_i(bytecode::Opcode::aload_2);
    add_i(bytecode::Opcode::arraylength);

    // if_icmpge L_END
    size_t if_icmpge_idx = instrs.size();
    bytecode::Instruction if_end_inst;
    if_end_inst.offset = cur_bci++;
    if_end_inst.opcode = bytecode::Opcode::if_icmpge;
    if_end_inst.operand = bytecode::Branch16Operand{0, 0};
    instrs.push_back(if_end_inst);

    // int next_state = state * 1103515245 + 12345;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    if (mult_idx <= 255) {
        add_op(bytecode::Opcode::ldc, bytecode::CpIndexOperand{mult_idx});
    } else {
        add_op(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{mult_idx});
    }
    add_i(bytecode::Opcode::imul);
    if (add_idx <= 255) {
        add_op(bytecode::Opcode::ldc, bytecode::CpIndexOperand{add_idx});
    } else {
        add_op(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{add_idx});
    }
    add_i(bytecode::Opcode::iadd);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{5}); // next_state

    // int enc = data[i] & 0xFF;
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{4});
    add_i(bytecode::Opcode::caload);
    add_op(bytecode::Opcode::sipush, bytecode::ShortOperand{255});
    add_i(bytecode::Opcode::iand);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{6}); // enc

    // data[i] = (char)(enc ^ ((next_state >>> 16) & 0xFF));
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{4});
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{6});
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{5});
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{16});
    add_i(bytecode::Opcode::iushr);
    add_op(bytecode::Opcode::sipush, bytecode::ShortOperand{255});
    add_i(bytecode::Opcode::iand);
    add_i(bytecode::Opcode::ixor);
    add_i(bytecode::Opcode::i2c);
    add_i(bytecode::Opcode::castore);

    // state = next_state ^ enc;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{5});
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{6});
    add_i(bytecode::Opcode::ixor);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{3}); // state = ...

    // i++
    add_op(bytecode::Opcode::iinc, bytecode::IincOperand{4, 1});

    // goto L_LOOP
    bytecode::Instruction goto_inst;
    goto_inst.offset = cur_bci++;
    goto_inst.opcode = bytecode::Opcode::goto_op;
    goto_inst.operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_loop)};
    instrs.push_back(goto_inst);

    // L_END:
    bytecode::u4 bci_end = cur_bci;
    instrs[if_icmpge_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_end)};

    // return new String(data);
    add_op(bytecode::Opcode::new_op, bytecode::CpIndexOperand{str_class});
    add_i(bytecode::Opcode::dup);
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{str_init_ref});
    add_i(bytecode::Opcode::areturn);

    bytecode::AttrCode code_attr;
    code_attr.max_stack = 6;
    code_attr.max_locals = 7;

    bytecode::u2 char_arr_cls = cp.find_or_add_class("[C");
    bytecode::BinaryWriter smt_writer;
    smt_writer.write_u2(2);

    smt_writer.write_u1(254);
    smt_writer.write_u2(static_cast<bytecode::u2>(bci_loop));
    smt_writer.write_u1(7);
    smt_writer.write_u2(char_arr_cls);
    smt_writer.write_u1(1);
    smt_writer.write_u1(1);

    bytecode::u4 delta_end = bci_end - bci_loop - 1;
    if (delta_end <= 63) {
        smt_writer.write_u1(static_cast<bytecode::u1>(delta_end));
    } else {
        smt_writer.write_u1(251);
        smt_writer.write_u2(static_cast<bytecode::u2>(delta_end));
    }

    bytecode::Attribute smt_attr;
    smt_attr.name_index = cp.find_or_add_utf8("StackMapTable");
    bytecode::AttrRaw raw;
    raw.data = std::move(smt_writer.data());
    smt_attr.data = std::move(raw);
    code_attr.attributes.push_back(std::move(smt_attr));

    bytecode::update_code(code_attr, instrs, cp);

    bytecode::Attribute attr;
    attr.name_index = cp.find_or_add_utf8("Code");
    attr.data = std::move(code_attr);
    m.attributes.push_back(std::move(attr));
    cls.methods().push_back(std::move(m));
}

static void inject_decryption_method_v3(bytecode::ClassFile& cls, const std::string& decrypt_name) {
    auto& cp = cls.constant_pool();
    bytecode::MethodInfo m;
    m.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) | 
                     bytecode::access_flags::ACC_STATIC | 
                     bytecode::access_flags::ACC_SYNTHETIC;
    m.name_index = cp.find_or_add_utf8(decrypt_name);
    m.descriptor_index = cp.find_or_add_utf8("(Ljava/lang/String;I)Ljava/lang/String;");

    bytecode::u2 toCharArray_ref = cp.find_or_add_methodref("java/lang/String", "toCharArray", "()[C");
    bytecode::u2 str_class = cp.find_or_add_class("java/lang/String");
    bytecode::u2 str_init_ref = cp.find_or_add_methodref("java/lang/String", "<init>", "([C)V");

    std::vector<bytecode::Instruction> instrs;
    bytecode::u4 cur_bci = 0;
    auto add_i = [&](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        instrs.push_back(inst);
    };
    auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = cur_bci++;
        inst.opcode = op;
        inst.operand = operand;
        instrs.push_back(inst);
    };

    // char[] data = str.toCharArray();
    add_i(bytecode::Opcode::aload_0);
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{toCharArray_ref});
    add_i(bytecode::Opcode::astore_2);

    // int i = 0;
    add_i(bytecode::Opcode::iconst_0);
    add_op(bytecode::Opcode::istore, bytecode::LocalVarOperand{3});

    // L_LOOP:
    bytecode::u4 bci_loop = cur_bci;
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::aload_2);
    add_i(bytecode::Opcode::arraylength);

    // if_icmpge L_END
    size_t if_icmpge_idx = instrs.size();
    bytecode::Instruction if_end_inst;
    if_end_inst.offset = cur_bci++;
    if_end_inst.opcode = bytecode::Opcode::if_icmpge;
    if_end_inst.operand = bytecode::Branch16Operand{0, 0};
    instrs.push_back(if_end_inst);

    // data[i] = (char)(((data[i] & 0xFF) * 181 & 0xFF) ^ ((key >> 7) + i * 19 + 73));
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});

    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::caload);
    add_op(bytecode::Opcode::sipush, bytecode::ShortOperand{255});
    add_i(bytecode::Opcode::iand);
    add_op(bytecode::Opcode::sipush, bytecode::ShortOperand{181});
    add_i(bytecode::Opcode::imul);
    add_op(bytecode::Opcode::sipush, bytecode::ShortOperand{255});
    add_i(bytecode::Opcode::iand);

    add_i(bytecode::Opcode::iload_1);
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{7});
    add_i(bytecode::Opcode::ishr);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{19});
    add_i(bytecode::Opcode::imul);
    add_i(bytecode::Opcode::iadd);
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{73});
    add_i(bytecode::Opcode::iadd);
    add_op(bytecode::Opcode::sipush, bytecode::ShortOperand{255});
    add_i(bytecode::Opcode::iand);

    add_i(bytecode::Opcode::ixor);
    add_i(bytecode::Opcode::i2c);
    add_i(bytecode::Opcode::castore);

    // i++
    add_op(bytecode::Opcode::iinc, bytecode::IincOperand{3, 1});

    // goto L_LOOP
    bytecode::Instruction goto_inst;
    goto_inst.offset = cur_bci++;
    goto_inst.opcode = bytecode::Opcode::goto_op;
    goto_inst.operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_loop)};
    instrs.push_back(goto_inst);

    // L_END:
    bytecode::u4 bci_end = cur_bci;
    instrs[if_icmpge_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_end)};

    // return new String(data);
    add_op(bytecode::Opcode::new_op, bytecode::CpIndexOperand{str_class});
    add_i(bytecode::Opcode::dup);
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{str_init_ref});
    add_i(bytecode::Opcode::areturn);

    bytecode::AttrCode code_attr;
    code_attr.max_stack = 6;
    code_attr.max_locals = 4;

    bytecode::u2 char_arr_cls = cp.find_or_add_class("[C");
    bytecode::BinaryWriter smt_writer;
    smt_writer.write_u2(2);

    smt_writer.write_u1(253);
    smt_writer.write_u2(static_cast<bytecode::u2>(bci_loop));
    smt_writer.write_u1(7);
    smt_writer.write_u2(char_arr_cls);
    smt_writer.write_u1(1);

    bytecode::u4 delta_end = bci_end - bci_loop - 1;
    if (delta_end <= 63) {
        smt_writer.write_u1(static_cast<bytecode::u1>(delta_end));
    } else {
        smt_writer.write_u1(251);
        smt_writer.write_u2(static_cast<bytecode::u2>(delta_end));
    }

    bytecode::Attribute smt_attr;
    smt_attr.name_index = cp.find_or_add_utf8("StackMapTable");
    bytecode::AttrRaw raw;
    raw.data = std::move(smt_writer.data());
    smt_attr.data = std::move(raw);
    code_attr.attributes.push_back(std::move(smt_attr));

    bytecode::update_code(code_attr, instrs, cp);

    bytecode::Attribute attr;
    attr.name_index = cp.find_or_add_utf8("Code");
    attr.data = std::move(code_attr);
    m.attributes.push_back(std::move(attr));
    cls.methods().push_back(std::move(m));
}

void StringEncryptPass::inject_decryption_method(bytecode::ClassFile& cls, const std::string& decrypt_name, size_t variant_idx) {
    if (cls.find_method(decrypt_name, "(Ljava/lang/String;I)Ljava/lang/String;")) {
        return;
    }
    size_t v = variant_idx % 4;
    if (v == 0) {
        inject_decryption_method_v0(cls, decrypt_name);
    } else if (v == 1) {
        inject_decryption_method_v1(cls, decrypt_name);
    } else if (v == 2) {
        inject_decryption_method_v2(cls, decrypt_name);
    } else {
        inject_decryption_method_v3(cls, decrypt_name);
    }
}

bool StringEncryptPass::run_on_class(bytecode::ClassFile& cls, PassContext& ctx) {
    if (!ctx.config.string_encryption.enabled) return true;
    if (!ctx.is_class_in_scope(cls.name())) return true;

    // 1. Transform indy concats into helper methods with encrypted chunks
    transform_indy_string_concat(cls, ctx);

    auto& cp = cls.constant_pool();
    std::string class_name(cls.name());
    uint64_t salt = ctx.rng();
    uint32_t base_key = (ctx.config.string_encryption.key_seed != 0) 
        ? ctx.config.string_encryption.key_seed 
        : static_cast<uint32_t>(ctx.rng() ^ 0xCAFEBABE);
    static size_t decryptor_counter = 0;

    struct DecryptorEntry {
        std::string name;
        size_t variant_idx;
        bytecode::u2 mref;
    };
    std::vector<DecryptorEntry> decryptors;

    auto get_or_create_decryptor = [&](size_t idx) -> const DecryptorEntry& {
        while (decryptors.size() <= idx) {
            size_t v_idx = decryptors.size() % 4;
            std::string dname = RenamePass::generate_obfuscated_name(decryptor_counter++, config::RenameMode::ConfusingLong, salt ^ (decryptors.size() * 0x61C88647ULL));
            bytecode::u2 mref = cls.is_interface()
                ? cp.find_or_add_interface_methodref(class_name, dname, "(Ljava/lang/String;I)Ljava/lang/String;")
                : cp.find_or_add_methodref(class_name, dname, "(Ljava/lang/String;I)Ljava/lang/String;");
            decryptors.push_back({dname, v_idx, mref});
        }
        return decryptors[idx];
    };

    size_t string_index = 0;
    std::unordered_map<bytecode::u2, std::pair<bytecode::u2, bytecode::u4>> encrypted_cp_strings;
    std::unordered_map<bytecode::u2, bytecode::u2> string_to_decryptor_mref;

    size_t min_chunk = std::max<size_t>(1, ctx.config.string_encryption.strings_per_decryptor_min);
    size_t max_chunk = std::max<size_t>(min_chunk, ctx.config.string_encryption.strings_per_decryptor_max);
    std::uniform_int_distribution<size_t> chunk_dist(min_chunk, max_chunk);

    size_t current_chunk_size = chunk_dist(ctx.rng);
    size_t strings_in_current_chunk = 0;
    size_t current_dec_idx = 0;

    for (auto& method : cls.methods()) {
        if (auto* code = method.code_mut()) {
            auto instructions = bytecode::disassemble(code->code);
            std::vector<bytecode::Instruction> new_instructions;
            new_instructions.reserve(instructions.size() * 3);
            bool modified = false;

            for (const auto& instr : instructions) {
                if (instr.opcode == bytecode::Opcode::ldc || instr.opcode == bytecode::Opcode::ldc_w) {
                    bytecode::u2 cp_idx = 0;
                    if (const auto* op = std::get_if<bytecode::CpIndexOperand>(&instr.operand)) {
                        cp_idx = op->index;
                    }

                    if (cp_idx != 0 && cp.get_if<bytecode::CpString>(cp_idx)) {
                        bytecode::u2 enc_str_idx = 0;
                        bytecode::u4 item_key = 0;
                        bytecode::u2 call_mref = 0;

                        auto it = encrypted_cp_strings.find(cp_idx);
                        if (it != encrypted_cp_strings.end()) {
                            enc_str_idx = it->second.first;
                            item_key = it->second.second;
                            call_mref = string_to_decryptor_mref[cp_idx];
                        } else {
                            std::string original_str(cp.get_string_value(cp_idx));
                            if (!original_str.empty()) {
                                if (strings_in_current_chunk >= current_chunk_size) {
                                    current_dec_idx++;
                                    current_chunk_size = chunk_dist(ctx.rng);
                                    strings_in_current_chunk = 0;
                                }
                                const auto& dec = get_or_create_decryptor(current_dec_idx);
                                call_mref = dec.mref;
                                strings_in_current_chunk++;
                                string_index++;
                                item_key = static_cast<bytecode::u4>(base_key ^ (string_index * 0x5bd1e995U + 0x1337U));

                                std::string enc = encrypt_string_variant(original_str, item_key, dec.variant_idx);
                                int retries = 0;
                                while ((enc == original_str || cp.has_utf8(enc)) && retries++ < 50) {
                                    item_key = static_cast<bytecode::u4>(item_key ^ (0x9E3779B9U + retries * 0x1337U));
                                    enc = encrypt_string_variant(original_str, item_key, dec.variant_idx);
                                }

                                enc_str_idx = cp.find_or_add_string(enc);
                                if (enc_str_idx == cp_idx) {
                                    bytecode::u2 new_utf = cp.add_entry(bytecode::CpUtf8{enc});
                                    enc_str_idx = cp.add_entry(bytecode::CpString{new_utf});
                                }
                                encrypted_cp_strings[cp_idx] = {enc_str_idx, item_key};
                                string_to_decryptor_mref[cp_idx] = call_mref;
                                ctx.stats.strings_encrypted++;
                            }
                        }

                        if (enc_str_idx != 0) {
                            // 1. ldc enc_str_idx
                            bytecode::Instruction ldc_instr;
                            ldc_instr.offset = instr.offset;
                            if (enc_str_idx <= 255) {
                                ldc_instr.opcode = bytecode::Opcode::ldc;
                                ldc_instr.operand = bytecode::CpIndexOperand{enc_str_idx};
                            } else {
                                ldc_instr.opcode = bytecode::Opcode::ldc_w;
                                ldc_instr.operand = bytecode::CpIndexOperand{enc_str_idx};
                            }
                            new_instructions.push_back(ldc_instr);

                            // 2. ldc/sipush key
                            bytecode::Instruction key_instr;
                            key_instr.offset = static_cast<bytecode::u4>(-1);
                            int32_t signed_key = static_cast<int32_t>(item_key);
                            if (signed_key >= -32768 && signed_key <= 32767) {
                                key_instr.opcode = bytecode::Opcode::sipush;
                                key_instr.operand = bytecode::ShortOperand{static_cast<bytecode::i2>(signed_key)};
                            } else {
                                bytecode::u2 int_cp = cp.find_or_add_integer(signed_key);
                                if (int_cp <= 255) {
                                    key_instr.opcode = bytecode::Opcode::ldc;
                                    key_instr.operand = bytecode::CpIndexOperand{int_cp};
                                } else {
                                    key_instr.opcode = bytecode::Opcode::ldc_w;
                                    key_instr.operand = bytecode::CpIndexOperand{int_cp};
                                }
                            }
                            new_instructions.push_back(key_instr);

                            // 3. invokestatic decrypt_method
                            bytecode::Instruction call_instr;
                            call_instr.offset = static_cast<bytecode::u4>(-1);
                            call_instr.opcode = bytecode::Opcode::invokestatic;
                            call_instr.operand = bytecode::CpIndexOperand{call_mref};
                            new_instructions.push_back(call_instr);

                            modified = true;
                            continue;
                        }
                    }
                }
                new_instructions.push_back(instr);
            }

            if (modified) {
                code->max_stack = std::max<bytecode::u2>(code->max_stack, code->max_stack + 3);
                bytecode::update_code(*code, new_instructions, cp);
            }
        }
    }

    // Collect all Utf8 indices that must NEVER be wiped (e.g. ciphertexts, method descriptors, etc.)
    std::unordered_set<bytecode::u2> protected_utf8_indices;
    for (const auto& [orig_idx, enc_pair] : encrypted_cp_strings) {
        if (const auto* enc_str = cp.get_if<bytecode::CpString>(enc_pair.first)) {
            protected_utf8_indices.insert(enc_str->string_index);
        }
    }
    for (const auto& dec : decryptors) {
        if (const auto* mr = cp.get_if<bytecode::CpMethodref>(dec.mref)) {
            if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(mr->name_and_type_index)) {
                protected_utf8_indices.insert(nat->name_index);
                protected_utf8_indices.insert(nat->descriptor_index);
            }
        }
    }

    // Erase original plaintext strings from the constant pool
    for (const auto& [orig_idx, enc_pair] : encrypted_cp_strings) {
        if (orig_idx == enc_pair.first) continue;
        if (auto* original_cp_str = cp.get_if<bytecode::CpString>(orig_idx)) {
            bytecode::u2 orig_utf8_idx = original_cp_str->string_index;
            if (protected_utf8_indices.count(orig_utf8_idx)) continue;

            bool shared_with_non_string = false;
            for (size_t i = 1; i < cp.size(); ++i) {
                if (const auto* c = cp.get_if<bytecode::CpClass>(static_cast<bytecode::u2>(i))) {
                    if (c->name_index == orig_utf8_idx) { shared_with_non_string = true; break; }
                } else if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(static_cast<bytecode::u2>(i))) {
                    if (nat->name_index == orig_utf8_idx || nat->descriptor_index == orig_utf8_idx) {
                        shared_with_non_string = true; break;
                    }
                }
            }
            if (!shared_with_non_string) {
                if (auto* utf8_entry = cp.get_mut_if<bytecode::CpUtf8>(orig_utf8_idx)) {
                    utf8_entry->value.clear(); // WIPE COMPLETELY!
                }
            }
        }
    }
    cp.rebuild_utf8_lookup();

    // Inject all instantiated decryptor methods
    for (const auto& dec : decryptors) {
        inject_decryption_method(cls, dec.name, dec.variant_idx);
    }

    return true;
}

} // namespace obf::passes
