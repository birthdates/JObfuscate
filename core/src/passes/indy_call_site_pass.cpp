#include "passes/indy_call_site_pass.hpp"
#include "passes/rename_pass.hpp"
#include "bytecode/instructions.hpp"
#include "bytecode/attributes.hpp"
#include <random>
#include <vector>
#include <string>
#include <iostream>

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

static std::string encrypt_for_bsm(std::string_view input, int key) {
    std::string out;
    for (size_t i = 0; i < input.size(); ++i) {
        uint16_t c = static_cast<uint8_t>(input[i]);
        uint16_t enc = static_cast<uint16_t>(c ^ (key + static_cast<int>(i) * 17));
        append_mutf8_char(out, enc);
    }
    return out;
}

// Injects the lightweight string decryptor helper: private static String dec(String enc, int k)
static std::string inject_bsm_decryptor(bytecode::ClassFile& cls, size_t id) {
    auto& cp = cls.constant_pool();
    std::string dec_name = RenamePass::generate_obfuscated_name(id, config::RenameMode::ConfusingLong, 0x1A2B3C4DULL);

    bytecode::MethodInfo m;
    m.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) |
                     bytecode::access_flags::ACC_STATIC |
                     bytecode::access_flags::ACC_SYNTHETIC;
    m.name_index = cp.find_or_add_utf8(dec_name);
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

    // data[i] = (char)(data[i] ^ (k + i * 17));
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3});
    add_i(bytecode::Opcode::caload);
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{1}); // k
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{3}); // i
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{17});
    add_i(bytecode::Opcode::imul);
    add_i(bytecode::Opcode::iadd);
    add_i(bytecode::Opcode::ixor);
    add_i(bytecode::Opcode::i2c);
    add_i(bytecode::Opcode::castore);

    // iinc 3, 1
    add_op(bytecode::Opcode::iinc, bytecode::IincOperand{3, 1});

    // goto L_LOOP
    add_op(bytecode::Opcode::goto_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_loop)});

    // L_END:
    bytecode::u4 bci_end = cur_bci;
    instrs[if_icmpge_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_end)};

    // return new String(data);
    add_op(bytecode::Opcode::new_op, bytecode::CpIndexOperand{str_class});
    add_i(bytecode::Opcode::dup);
    add_i(bytecode::Opcode::aload_2);
    add_op(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{str_init_ref});
    add_i(bytecode::Opcode::areturn);

    bytecode::AttrCode code;
    code.max_stack = 6;
    code.max_locals = 4;

    bytecode::u2 char_arr_cls = cp.find_or_add_class("[C");
    bytecode::BinaryWriter smt_writer;
    smt_writer.write_u2(2);

    smt_writer.write_u1(253); // append_frame (2 locals)
    smt_writer.write_u2(static_cast<bytecode::u2>(bci_loop));
    smt_writer.write_u1(7); // Object
    smt_writer.write_u2(char_arr_cls);
    smt_writer.write_u1(1); // Integer

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
    code.attributes.push_back(std::move(smt_attr));

    bytecode::update_code(code, instrs, cp);

    bytecode::Attribute attr;
    attr.name_index = cp.find_or_add_utf8("Code");
    attr.data = std::move(code);
    m.attributes.push_back(std::move(attr));

    cls.methods().push_back(std::move(m));
    return dec_name;
}

// Injects the custom Bootstrap Method (BSM) for invokedynamic call resolution
static std::string inject_bsm_method(bytecode::ClassFile& cls, const std::string& dec_name, size_t id) {
    auto& cp = cls.constant_pool();
    std::string bsm_name = RenamePass::generate_obfuscated_name(id, config::RenameMode::ConfusingLong, 0x8E7D6C5BUL);
    std::string bsm_desc = "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/invoke/MethodType;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;II)Ljava/lang/invoke/CallSite;";

    bytecode::MethodInfo m;
    m.access_flags = (cls.is_interface() ? bytecode::access_flags::ACC_PUBLIC : bytecode::access_flags::ACC_PRIVATE) |
                     bytecode::access_flags::ACC_STATIC |
                     bytecode::access_flags::ACC_SYNTHETIC;
    m.name_index = cp.find_or_add_utf8(bsm_name);
    m.descriptor_index = cp.find_or_add_utf8(bsm_desc);

    std::string class_name(cls.name());
    bytecode::u2 dec_mref = cls.is_interface()
        ? cp.find_or_add_interface_methodref(class_name, dec_name, "(Ljava/lang/String;I)Ljava/lang/String;")
        : cp.find_or_add_methodref(class_name, dec_name, "(Ljava/lang/String;I)Ljava/lang/String;");

    bytecode::u2 str_replace_ref = cp.find_or_add_methodref("java/lang/String", "replace", "(CC)Ljava/lang/String;");
    bytecode::u2 class_forName_ref = cp.find_or_add_methodref("java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;");
    bytecode::u2 class_getClassLoader_ref = cp.find_or_add_methodref("java/lang/Class", "getClassLoader", "()Ljava/lang/ClassLoader;");
    bytecode::u2 mt_fromDesc_ref = cp.find_or_add_methodref("java/lang/invoke/MethodType", "fromMethodDescriptorString", "(Ljava/lang/String;Ljava/lang/ClassLoader;)Ljava/lang/invoke/MethodType;");
    bytecode::u2 lookup_findStatic_ref = cp.find_or_add_methodref("java/lang/invoke/MethodHandles$Lookup", "findStatic", "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
    bytecode::u2 lookup_findVirtual_ref = cp.find_or_add_methodref("java/lang/invoke/MethodHandles$Lookup", "findVirtual", "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
    bytecode::u2 mh_asType_ref = cp.find_or_add_methodref("java/lang/invoke/MethodHandle", "asType", "(Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
    bytecode::u2 ccs_class = cp.find_or_add_class("java/lang/invoke/ConstantCallSite");
    bytecode::u2 ccs_init_ref = cp.find_or_add_methodref("java/lang/invoke/ConstantCallSite", "<init>", "(Ljava/lang/invoke/MethodHandle;)V");

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

    // 1. targetClass = dec(encClass, key);
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{3}); // encClass
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{7}); // key
    add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{dec_mref});
    add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{8}); // targetClass

    // 2. targetMethod = dec(encMethod, key + 1);
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{4}); // encMethod
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{7}); // key
    add_i(bytecode::Opcode::iconst_1);
    add_i(bytecode::Opcode::iadd);
    add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{dec_mref});
    add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{9}); // targetMethod

    // 3. targetDesc = dec(encDesc, key + 2);
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{5}); // encDesc
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{7}); // key
    add_i(bytecode::Opcode::iconst_2);
    add_i(bytecode::Opcode::iadd);
    add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{dec_mref});
    add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{10}); // targetDesc

    // 4. Class targetCls = Class.forName(targetClass.replace('/', '.'));
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{8}); // targetClass
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{47}); // '/'
    add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{46}); // '.'
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{str_replace_ref});
    add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{class_forName_ref});
    add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{11}); // targetCls

    // 5. MethodType mt = MethodType.fromMethodDescriptorString(targetDesc, targetCls.getClassLoader());
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{10}); // targetDesc
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{11}); // targetCls
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{class_getClassLoader_ref});
    add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{mt_fromDesc_ref});
    add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{12}); // targetMt

    // 6. if (kind == 1) findStatic else findVirtual
    add_op(bytecode::Opcode::iload, bytecode::LocalVarOperand{6}); // kind
    add_i(bytecode::Opcode::iconst_1);

    size_t if_ne_idx = instrs.size();
    bytecode::Instruction if_ne_inst;
    if_ne_inst.offset = cur_bci++;
    if_ne_inst.opcode = bytecode::Opcode::if_icmpne;
    if_ne_inst.operand = bytecode::Branch16Operand{0, 0};
    instrs.push_back(if_ne_inst);

    // lookup.findStatic(targetCls, targetMethod, mt)
    add_i(bytecode::Opcode::aload_0); // lookup
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{11}); // targetCls
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{9});  // targetMethod
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{12}); // mt
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{lookup_findStatic_ref});
    add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{13}); // mh

    size_t goto_adapt_idx = instrs.size();
    bytecode::Instruction goto_adapt_inst;
    goto_adapt_inst.offset = cur_bci++;
    goto_adapt_inst.opcode = bytecode::Opcode::goto_op;
    goto_adapt_inst.operand = bytecode::Branch16Operand{0, 0};
    instrs.push_back(goto_adapt_inst);

    // L_VIRTUAL:
    bytecode::u4 bci_virtual = cur_bci;
    instrs[if_ne_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_virtual)};

    // lookup.findVirtual(targetCls, targetMethod, mt)
    add_i(bytecode::Opcode::aload_0); // lookup
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{11}); // targetCls
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{9});  // targetMethod
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{12}); // mt
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{lookup_findVirtual_ref});
    add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{13}); // mh

    // L_ADAPT:
    bytecode::u4 bci_adapt = cur_bci;
    instrs[goto_adapt_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_adapt)};

    // 7. MethodHandle adapted = mh.asType(type);
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{13}); // mh
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{2});  // type
    add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{mh_asType_ref});
    add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{14}); // adapted

    // 8. return new ConstantCallSite(adapted);
    add_op(bytecode::Opcode::new_op, bytecode::CpIndexOperand{ccs_class});
    add_i(bytecode::Opcode::dup);
    add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{14}); // adapted
    add_op(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{ccs_init_ref});
    add_i(bytecode::Opcode::areturn);

    bytecode::AttrCode code;
    code.max_stack = 6;
    code.max_locals = 15;

    bytecode::u2 lookup_cls = cp.find_or_add_class("java/lang/invoke/MethodHandles$Lookup");
    bytecode::u2 string_cls = cp.find_or_add_class("java/lang/String");
    bytecode::u2 mt_cls = cp.find_or_add_class("java/lang/invoke/MethodType");
    bytecode::u2 class_cls = cp.find_or_add_class("java/lang/Class");
    bytecode::u2 mh_cls = cp.find_or_add_class("java/lang/invoke/MethodHandle");

    bytecode::BinaryWriter smt_writer2;
    smt_writer2.write_u2(2); // 2 entries

    // Frame 1 at bci_virtual: full_frame (13 locals, 0 stack)
    smt_writer2.write_u1(255); // full_frame
    smt_writer2.write_u2(static_cast<bytecode::u2>(bci_virtual));
    smt_writer2.write_u2(13); // 13 locals
    smt_writer2.write_u1(7); smt_writer2.write_u2(lookup_cls);
    smt_writer2.write_u1(7); smt_writer2.write_u2(string_cls);
    smt_writer2.write_u1(7); smt_writer2.write_u2(mt_cls);
    smt_writer2.write_u1(7); smt_writer2.write_u2(string_cls);
    smt_writer2.write_u1(7); smt_writer2.write_u2(string_cls);
    smt_writer2.write_u1(7); smt_writer2.write_u2(string_cls);
    smt_writer2.write_u1(1); // integer
    smt_writer2.write_u1(1); // integer
    smt_writer2.write_u1(7); smt_writer2.write_u2(string_cls);
    smt_writer2.write_u1(7); smt_writer2.write_u2(string_cls);
    smt_writer2.write_u1(7); smt_writer2.write_u2(string_cls);
    smt_writer2.write_u1(7); smt_writer2.write_u2(class_cls);
    smt_writer2.write_u1(7); smt_writer2.write_u2(mt_cls);
    smt_writer2.write_u2(0); // 0 stack items

    // Frame 2 at bci_adapt: append_frame (1 local: mh)
    bytecode::u4 delta_adapt = bci_adapt - bci_virtual - 1;
    smt_writer2.write_u1(252); // append_frame (1 local)
    smt_writer2.write_u2(static_cast<bytecode::u2>(delta_adapt));
    smt_writer2.write_u1(7); smt_writer2.write_u2(mh_cls);

    bytecode::Attribute smt_attr2;
    smt_attr2.name_index = cp.find_or_add_utf8("StackMapTable");
    bytecode::AttrRaw raw2;
    raw2.data = std::move(smt_writer2.data());
    smt_attr2.data = std::move(raw2);
    code.attributes.push_back(std::move(smt_attr2));

    bytecode::update_code(code, instrs, cp);

    bytecode::Attribute attr;
    attr.name_index = cp.find_or_add_utf8("Code");
    attr.data = std::move(code);
    m.attributes.push_back(std::move(attr));

    cls.methods().push_back(std::move(m));
    return bsm_name;
}

bool IndyCallSitePass::run_on_class(bytecode::ClassFile& cls, PassContext& ctx) {
    if (!ctx.config.indy_call_sites.enabled) return true;
    if (!ctx.is_class_in_scope(cls.name())) return true;
    if (cls.major_version() < static_cast<bytecode::u2>(bytecode::JavaMajorVersion::Java7)) return true;

    // Check target packages filter
    if (!ctx.config.indy_call_sites.target_packages.empty() &&
        ctx.config.indy_call_sites.target_packages[0] != "*") {
        bool match = false;
        std::string cname(cls.name());
        for (const auto& pat : ctx.config.indy_call_sites.target_packages) {
            std::string slash_pat = pat;
            std::replace(slash_pat.begin(), slash_pat.end(), '.', '/');
            if (slash_pat.ends_with('*')) {
                std::string prefix = slash_pat.substr(0, slash_pat.size() - 1);
                if (cname.starts_with(prefix)) { match = true; break; }
            } else if (cname == slash_pat) {
                match = true; break;
            }
        }
        if (!match) return true;
    }

    size_t call_counter = 0;
    std::string class_name(cls.name());

    struct CallReplacement {
        size_t method_idx;
        size_t instr_idx;
        std::string target_class;
        std::string target_name;
        std::string target_desc;
        std::string indy_desc;
        int kind; // 1=static, 2=virtual
    };
    std::vector<CallReplacement> replacements;

    auto& cp = cls.constant_pool();

    for (size_t m_idx = 0; m_idx < cls.methods().size(); ++m_idx) {
        auto& method = cls.methods()[m_idx];
        if (method.is_abstract() || method.is_native()) continue;
        auto* code = method.code_mut();
        if (!code) continue;

        auto instructions = bytecode::disassemble(code->code);
        for (size_t i_idx = 0; i_idx < instructions.size(); ++i_idx) {
            const auto& instr = instructions[i_idx];
            if (instr.opcode == bytecode::Opcode::invokestatic ||
                instr.opcode == bytecode::Opcode::invokevirtual ||
                instr.opcode == bytecode::Opcode::invokeinterface) {

                bytecode::u2 cp_idx = 0;
                if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&instr.operand)) {
                    cp_idx = cp_op->index;
                } else if (const auto* if_op = std::get_if<bytecode::InterfaceCallOperand>(&instr.operand)) {
                    cp_idx = if_op->index;
                }
                if (cp_idx == 0) continue;

                std::string target_class;
                std::string target_name;
                std::string target_desc;

                if (const auto* mr = cp.get_if<bytecode::CpMethodref>(cp_idx)) {
                    target_class = std::string(cp.get_class_name(mr->class_index));
                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(mr->name_and_type_index)) {
                        target_name = std::string(cp.get_utf8(nat->name_index));
                        target_desc = std::string(cp.get_utf8(nat->descriptor_index));
                    }
                } else if (const auto* imr = cp.get_if<bytecode::CpInterfaceMethodref>(cp_idx)) {
                    target_class = std::string(cp.get_class_name(imr->class_index));
                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(imr->name_and_type_index)) {
                        target_name = std::string(cp.get_utf8(nat->name_index));
                        target_desc = std::string(cp.get_utf8(nat->descriptor_index));
                    }
                }

                if (target_class.empty() || target_name.empty() || target_desc.empty()) continue;

                // Safety filters:
                if (target_name == "<init>" || target_name == "<clinit>") continue;
                if (target_class.starts_with("[")) continue; // skip array clones
                if (target_class.starts_with("java/lang/invoke/MethodHandle")) continue;

                int kind = (instr.opcode == bytecode::Opcode::invokestatic) ? 1 : 2;
                std::string indy_desc;
                if (kind == 1) {
                    indy_desc = target_desc;
                } else {
                    // Receiver prepended to descriptor: (Ltarget_class;Args...)Ret
                    indy_desc = "(L" + target_class + ";" + target_desc.substr(1);
                }

                replacements.push_back({m_idx, i_idx, target_class, target_name, target_desc, indy_desc, kind});
            }
        }
    }

    if (replacements.empty()) return true;

    // Inject BSM helper & decryptor
    std::string dec_name = inject_bsm_decryptor(cls, call_counter++);
    std::string bsm_name = inject_bsm_method(cls, dec_name, call_counter++);
    std::string bsm_desc = "(Ljava/lang/invoke/MethodHandles$Lookup;Ljava/lang/String;Ljava/lang/invoke/MethodType;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;II)Ljava/lang/invoke/CallSite;";

    // Find or create BootstrapMethods attribute
    auto* bsm_attr_raw = cls.find_attribute_mut("BootstrapMethods");
    if (!bsm_attr_raw) {
        bytecode::Attribute new_bsm_attr;
        new_bsm_attr.name_index = cp.find_or_add_utf8("BootstrapMethods");
        new_bsm_attr.data = bytecode::AttrBootstrapMethods{};
        cls.attributes().push_back(std::move(new_bsm_attr));
        bsm_attr_raw = cls.find_attribute_mut("BootstrapMethods");
    }
    auto* bsm_attr = bsm_attr_raw->get_mut_if<bytecode::AttrBootstrapMethods>();

    bytecode::u2 bsm_mref = cls.is_interface()
        ? cp.find_or_add_interface_methodref(class_name, bsm_name, bsm_desc)
        : cp.find_or_add_methodref(class_name, bsm_name, bsm_desc);
    bytecode::u2 bsm_handle = cp.find_or_add_method_handle(6 /* REF_invokeStatic */, bsm_mref);

    // Group replacements by method
    std::unordered_map<size_t, std::vector<CallReplacement>> by_method;
    for (const auto& r : replacements) {
        by_method[r.method_idx].push_back(r);
    }

    std::mt19937 rng(static_cast<uint32_t>(ctx.config.seed.value_or(1337) ^ 0xB5A41234ULL));

    for (auto& [m_idx, reps] : by_method) {
        auto& method = cls.methods()[m_idx];
        auto* code = method.code_mut();
        if (!code) continue;

        auto instructions = bytecode::disassemble(code->code);
        std::unordered_map<size_t, const CallReplacement*> rep_by_i;
        for (const auto& r : reps) {
            rep_by_i[r.instr_idx] = &r;
        }

        std::vector<bytecode::Instruction> new_instructions;
        new_instructions.reserve(instructions.size());

        for (size_t i = 0; i < instructions.size(); ++i) {
            auto it = rep_by_i.find(i);
            if (it != rep_by_i.end()) {
                const auto* r = it->second;

                int key = static_cast<int>(rng() & 0x7FFFFFFF);
                std::string enc_cls = encrypt_for_bsm(r->target_class, key);
                std::string enc_meth = encrypt_for_bsm(r->target_name, key + 1);
                std::string enc_desc = encrypt_for_bsm(r->target_desc, key + 2);

                bytecode::u2 cp_enc_cls = cp.find_or_add_string(enc_cls);
                bytecode::u2 cp_enc_meth = cp.find_or_add_string(enc_meth);
                bytecode::u2 cp_enc_desc = cp.find_or_add_string(enc_desc);
                bytecode::u2 cp_kind = cp.find_or_add_integer(r->kind);
                bytecode::u2 cp_key = cp.find_or_add_integer(key);

                bytecode::BootstrapMethodEntry bsm_entry;
                bsm_entry.bootstrap_method_ref = bsm_handle;
                bsm_entry.bootstrap_arguments = {cp_enc_cls, cp_enc_meth, cp_enc_desc, cp_kind, cp_key};

                bytecode::u2 bsm_attr_index = static_cast<bytecode::u2>(bsm_attr->bootstrap_methods.size());
                bsm_attr->bootstrap_methods.push_back(std::move(bsm_entry));

                std::string dyn_name = RenamePass::generate_obfuscated_name(call_counter++, config::RenameMode::ConfusingChars, rng());
                bytecode::u2 indy_index = cp.find_or_add_invoke_dynamic(bsm_attr_index, dyn_name, r->indy_desc);

                bytecode::Instruction indy_inst;
                indy_inst.offset = instructions[i].offset;
                indy_inst.opcode = bytecode::Opcode::invokedynamic;
                indy_inst.operand = bytecode::InvokeDynamicOperand{indy_index};
                new_instructions.push_back(indy_inst);

                ctx.stats.indy_call_sites_created++;
                continue;
            }
            new_instructions.push_back(instructions[i]);
        }

        bytecode::update_code(*code, new_instructions, cp);
    }

    return true;
}

} // namespace obf::passes
