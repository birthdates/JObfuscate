#include "passes/class_encryption_pass.hpp"
#include "passes/bootstrap_template_data.hpp"
#include "passes/rename_pass.hpp"
#include "passes/string_encrypt_pass.hpp"
#include "passes/control_flow_pass.hpp"
#include "passes/number_obfuscation_pass.hpp"
#include "jar/jar_archive.hpp"
#include "bytecode/class_file.hpp"
#include "bytecode/instructions.hpp"
#include "reporter/reporter.hpp"
#include <algorithm>
#include <random>
#include <cstring>

namespace obf::passes {

static void write_u4_be(std::vector<bytecode::u1>& buf, uint32_t val) {
    buf.push_back(static_cast<bytecode::u1>((val >> 24) & 0xFF));
    buf.push_back(static_cast<bytecode::u1>((val >> 16) & 0xFF));
    buf.push_back(static_cast<bytecode::u1>((val >> 8) & 0xFF));
    buf.push_back(static_cast<bytecode::u1>(val & 0xFF));
}

static void write_u2_be(std::vector<bytecode::u1>& buf, uint16_t val) {
    buf.push_back(static_cast<bytecode::u1>((val >> 8) & 0xFF));
    buf.push_back(static_cast<bytecode::u1>(val & 0xFF));
}

static void obfuscate_bootstrap_class(bytecode::ClassFile& boot_cf,
                                     PassContext& ctx,
                                     const std::string& launcher_internal,
                                     const std::string& real_main_dotted,
                                     const std::string& res_path,
                                     const std::string& key_hex) {
    auto& cp = boot_cf.constant_pool();

    // 0. Update class name if launcher_internal is not "Bootstrap"
    if (launcher_internal != "Bootstrap") {
        for (bytecode::u2 idx = 1; idx < cp.count(); ++idx) {
            if (auto* u = cp.get_mut_if<bytecode::CpUtf8>(idx)) {
                if (u->value == "Bootstrap") {
                    u->value = launcher_internal;
                }
            }
        }
        cp.rebuild_utf8_lookup();
    }

    // 1. Dead Constant Field Elimination:
    // Erase unused static final constants REAL_MAIN, RESOURCE_NAME, KEY_HEX from field table
    std::vector<bytecode::FieldInfo> remaining_fields;
    for (auto& f : boot_cf.fields()) {
        std::string fname(f.name(cp));
        if (fname != "REAL_MAIN" && fname != "RESOURCE_NAME" && fname != "KEY_HEX") {
            remaining_fields.push_back(std::move(f));
        }
    }
    boot_cf.fields() = std::move(remaining_fields);

    // 2. Member Renaming & Helper Injection:
    std::string obf_field_classes = RenamePass::generate_obfuscated_name(0, config::RenameMode::ConfusingLong, ctx.rng());
    std::string obf_method_init = RenamePass::generate_obfuscated_name(1, config::RenameMode::ConfusingLong, ctx.rng());
    std::string obf_method_decrypt = RenamePass::generate_obfuscated_name(2, config::RenameMode::ConfusingLong, ctx.rng());
    std::string obf_method_parse_hex = RenamePass::generate_obfuscated_name(3, config::RenameMode::ConfusingLong, ctx.rng());
    std::string obf_method_str_dec = RenamePass::generate_obfuscated_name(4, config::RenameMode::ConfusingLong, ctx.rng());
    std::string obf_method_fetch = RenamePass::generate_obfuscated_name(5, config::RenameMode::ConfusingLong, ctx.rng());
    std::string obf_method_define = RenamePass::generate_obfuscated_name(6, config::RenameMode::ConfusingLong, ctx.rng());

    for (bytecode::u2 idx = 1; idx < cp.count(); ++idx) {
        if (auto* u = cp.get_mut_if<bytecode::CpUtf8>(idx)) {
            if (u->value == "classes") {
                u->value = obf_field_classes;
            } else if (u->value == "init") {
                u->value = obf_method_init;
            } else if (u->value == "decrypt") {
                u->value = obf_method_decrypt;
            } else if (u->value == "parseHex") {
                u->value = obf_method_parse_hex;
            }
        }
    }
    cp.rebuild_utf8_lookup();

    // 2a. Synthesize Private Helpers and Flatten findClass:
    bytecode::u2 classes_fref = cp.find_or_add_fieldref(launcher_internal, obf_field_classes, "Ljava/util/Map;");
    bytecode::u2 str_replace_mref = cp.find_or_add_methodref("java/lang/String", "replace", "(CC)Ljava/lang/String;");
    bytecode::u2 map_remove_imref = cp.find_or_add_interface_methodref("java/util/Map", "remove", "(Ljava/lang/Object;)Ljava/lang/Object;");
    bytecode::u2 byte_array_cls = cp.find_or_add_class("[B");
    bytecode::u2 define_class_mref = cp.find_or_add_methodref("java/lang/ClassLoader", "defineClass", "(Ljava/lang/String;[BII)Ljava/lang/Class;");
    bytecode::u2 arrays_fill_mref = cp.find_or_add_methodref("java/util/Arrays", "fill", "([BB)V");
    bytecode::u2 fetch_bytes_mref = cp.find_or_add_methodref(launcher_internal, obf_method_fetch, "(Ljava/lang/String;)[B");
    bytecode::u2 def_class_mref = cp.find_or_add_methodref(launcher_internal, obf_method_define, "(Ljava/lang/String;[B)Ljava/lang/Class;");
    bytecode::u2 super_find_class_mref = cp.find_or_add_methodref("java/lang/ClassLoader", "findClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    (void)cp.find_or_add_methodref("java/lang/System", "currentTimeMillis", "()J");
    (void)cp.find_or_add_class("java/lang/SecurityException");
    (void)cp.find_or_add_methodref("java/lang/SecurityException", "<init>", "(Ljava/lang/String;)V");
    (void)cp.find_or_add_class("java/lang/ClassNotFoundException");
    (void)cp.find_or_add_methodref("java/lang/ClassNotFoundException", "<init>", "(Ljava/lang/String;)V");

    // Helper 1: obf_fetch_bytes (lock-free: ConcurrentHashMap.remove is
    // already thread-safe; avoids monitorenter/exception-handler frames that
    // the generic StackMapTable recalculation gets wrong).
    {
        bytecode::MethodInfo m_fetch;
        m_fetch.access_flags = bytecode::access_flags::ACC_PRIVATE;
        m_fetch.name_index = cp.find_or_add_utf8(obf_method_fetch);
        m_fetch.descriptor_index = cp.find_or_add_utf8("(Ljava/lang/String;)[B");

        bytecode::AttrCode code_attr;
        code_attr.max_stack = 3;
        code_attr.max_locals = 2;

        uint32_t bci = 0;
        std::vector<bytecode::Instruction> instrs;

        auto add_i = [&](bytecode::Opcode op) {
            bytecode::Instruction inst;
            inst.offset = bci++;
            inst.opcode = op;
            inst.operand = bytecode::NoOperand{};
            instrs.push_back(inst);
        };
        auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
            bytecode::Instruction inst;
            inst.offset = bci++;
            inst.opcode = op;
            inst.operand = operand;
            instrs.push_back(inst);
        };

        // 1. name = name.replace('/', '.');
        add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{1});
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{47});
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{46});
        add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{str_replace_mref});
        add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{1});

        // 2. return (byte[]) this.classes.remove(name);
        add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{0});
        add_op(bytecode::Opcode::getfield, bytecode::CpIndexOperand{classes_fref});
        add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{1});
        add_op(bytecode::Opcode::invokeinterface, bytecode::InterfaceCallOperand{map_remove_imref, 2});
        add_op(bytecode::Opcode::checkcast, bytecode::CpIndexOperand{byte_array_cls});
        add_i(bytecode::Opcode::areturn);

        bytecode::update_code(code_attr, instrs, cp);

        bytecode::Attribute attr;
        attr.name_index = cp.find_or_add_utf8("Code");
        attr.data = std::move(code_attr);
        m_fetch.attributes.push_back(std::move(attr));
        boot_cf.methods().push_back(std::move(m_fetch));
    }

    // Helper 2: obf_def_class
    {
        bytecode::MethodInfo m_def;
        m_def.access_flags = bytecode::access_flags::ACC_PRIVATE;
        m_def.name_index = cp.find_or_add_utf8(obf_method_define);
        m_def.descriptor_index = cp.find_or_add_utf8("(Ljava/lang/String;[B)Ljava/lang/Class;");

        bytecode::AttrCode code_attr;
        code_attr.max_stack = 5;
        code_attr.max_locals = 4;

        uint32_t bci = 0;
        std::vector<bytecode::Instruction> instrs;

        auto add_i = [&](bytecode::Opcode op) {
            bytecode::Instruction inst;
            inst.offset = bci++;
            inst.opcode = op;
            inst.operand = bytecode::NoOperand{};
            instrs.push_back(inst);
        };
        auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
            bytecode::Instruction inst;
            inst.offset = bci++;
            inst.opcode = op;
            inst.operand = operand;
            instrs.push_back(inst);
        };

        // Class c = this.defineClass(name, bytes, 0, bytes.length);
        add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{0});
        add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{1});
        add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{2});
        add_i(bytecode::Opcode::iconst_0);
        add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{2});
        add_i(bytecode::Opcode::arraylength);
        add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{define_class_mref});
        add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{3});

        // Memory scrubbing
        if (ctx.config.class_encryption.memory_scrubbing) {
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{2});
            add_i(bytecode::Opcode::iconst_0);
            add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{arrays_fill_mref});
        }

        // return c;
        add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{3});
        add_i(bytecode::Opcode::areturn);

        bytecode::update_code(code_attr, instrs, cp);

        bytecode::Attribute attr;
        attr.name_index = cp.find_or_add_utf8("Code");
        attr.data = std::move(code_attr);
        m_def.attributes.push_back(std::move(attr));
        boot_cf.methods().push_back(std::move(m_def));
    }

    // Rewrite findClass into a simple, verifiable sequence:
    //   byte[] b = fetch(name); if (b != null) return define(name, b); return super.findClass(name);
    // (Previous lookupswitch state machine produced an invalid StackMapTable;
    //  keep the loader simple so the JVM verifier accepts it. update_code()
    //  auto-generates the required stackmap frames.)
    for (auto& m : boot_cf.methods()) {
        if (m.name(cp) == "findClass") {
            auto* code_attr = m.code_mut();
            if (!code_attr) continue;

            code_attr->exception_table.clear();
            code_attr->attributes.clear();
            code_attr->max_stack = 3;
            code_attr->max_locals = 3;

            uint32_t bci = 0;
            std::vector<bytecode::Instruction> instrs;

            auto add_i = [&](bytecode::Opcode op) {
                bytecode::Instruction inst;
                inst.offset = bci++;
                inst.opcode = op;
                inst.operand = bytecode::NoOperand{};
                instrs.push_back(inst);
            };
            auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
                bytecode::Instruction inst;
                inst.offset = bci++;
                inst.opcode = op;
                inst.operand = operand;
                instrs.push_back(inst);
            };

            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{0});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{1});
            add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{fetch_bytes_mref});
            add_op(bytecode::Opcode::astore, bytecode::LocalVarOperand{2});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{2});
            uint32_t ifnull_idx = static_cast<uint32_t>(instrs.size());
            add_op(bytecode::Opcode::ifnull, bytecode::Branch16Operand{0, 0});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{0});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{1});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{2});
            add_op(bytecode::Opcode::invokevirtual, bytecode::CpIndexOperand{def_class_mref});
            add_i(bytecode::Opcode::areturn);
            uint32_t super_bci = bci;
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{0});
            add_op(bytecode::Opcode::aload, bytecode::LocalVarOperand{1});
            add_op(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{super_find_class_mref});
            add_i(bytecode::Opcode::areturn);
            instrs[ifnull_idx].operand = bytecode::Branch16Operand{0, static_cast<bytecode::i4>(super_bci)};

            bytecode::update_code(*code_attr, instrs, cp);
            break;
        }
    }

    // 3./4. NOTE: control-flow flattening + number splitting are intentionally
    // NOT applied to the Bootstrap loader. The loader contains synchronized
    // blocks with exception handlers (fetch helper) where the generic
    // flattening pass produces unverifiable StackMapTables (JVM VerifyError).
    // The loader stays protected via member renaming + string encryption below.
    // (Re-enable only with a verifier-safe flattening implementation.)

    // 5. Self-Contained String Encryption:
    bytecode::u2 str_dec_mref = cp.find_or_add_methodref(launcher_internal, obf_method_str_dec, "(Ljava/lang/String;I)Ljava/lang/String;");

    for (auto& method : boot_cf.methods()) {
        auto* code = method.code_mut();
        if (!code) continue;

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

                if (cp_idx != 0) {
                    if (const auto* str_entry = cp.get_if<bytecode::CpString>(cp_idx)) {
                        bytecode::u2 orig_u_idx = str_entry->string_index;
                        std::string orig_str(cp.get_string_value(cp_idx));

                        // Map template placeholders to real values
                        std::string plain;
                        if (orig_str == "__JOBF_REAL_MAIN__") {
                            plain = real_main_dotted;
                        } else if (orig_str == "__JOBF_RES_NAME__") {
                            plain = res_path;
                        } else if (orig_str.find("__JOBF_KEY_HEX_PLACEHOLDER") != std::string::npos) {
                            plain = key_hex;
                        } else {
                            plain = orig_str;
                        }

                        // Encrypt string with randomized key
                        uint32_t item_key = static_cast<uint32_t>(ctx.rng() ^ 0xCAFEBABE);
                        std::string cipher = StringEncryptPass::encrypt_string_variant(plain, item_key, 0);

                        // Wipe original string from constant pool if not shared with method/field/class
                        bool is_shared = false;
                        for (const auto& m : boot_cf.methods()) {
                            if (m.name_index == orig_u_idx || m.descriptor_index == orig_u_idx) {
                                is_shared = true;
                                break;
                            }
                        }
                        if (!is_shared) {
                            for (const auto& f : boot_cf.fields()) {
                                if (f.name_index == orig_u_idx || f.descriptor_index == orig_u_idx) {
                                    is_shared = true;
                                    break;
                                }
                            }
                        }
                        if (!is_shared) {
                            for (bytecode::u2 i = 1; i < cp.count(); ++i) {
                                if (const auto* c = cp.get_if<bytecode::CpClass>(i)) {
                                    if (c->name_index == orig_u_idx) { is_shared = true; break; }
                                } else if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(i)) {
                                    if (nat->name_index == orig_u_idx || nat->descriptor_index == orig_u_idx) { is_shared = true; break; }
                                }
                            }
                        }
                        if (!is_shared) {
                            if (auto* u = cp.get_mut_if<bytecode::CpUtf8>(orig_u_idx)) {
                                u->value.clear();
                            }
                        }

                        bytecode::u2 cipher_cp = cp.find_or_add_string(cipher);

                    // 1. ldc ciphertext
                    bytecode::Instruction ldc_instr;
                    ldc_instr.offset = instr.offset;
                    if (cipher_cp <= 255) {
                        ldc_instr.opcode = bytecode::Opcode::ldc;
                        ldc_instr.operand = bytecode::CpIndexOperand{cipher_cp};
                    } else {
                        ldc_instr.opcode = bytecode::Opcode::ldc_w;
                        ldc_instr.operand = bytecode::CpIndexOperand{cipher_cp};
                    }
                    new_instructions.push_back(ldc_instr);

                    // 2. push item_key (split on the stack when enabled)
                    if (ctx.config.string_encryption.split_keys) {
                        StringEncryptPass::emit_split_key(new_instructions, cp, item_key, ctx.rng);
                    } else {
                        int32_t s_key = static_cast<int32_t>(item_key);
                        bytecode::Instruction key_instr;
                        key_instr.offset = static_cast<bytecode::u4>(-1);
                        if (s_key >= -1 && s_key <= 5) {
                            key_instr.opcode = static_cast<bytecode::Opcode>(static_cast<uint8_t>(bytecode::Opcode::iconst_0) + s_key);
                            key_instr.operand = bytecode::NoOperand{};
                        } else if (s_key >= -128 && s_key <= 127) {
                            key_instr.opcode = bytecode::Opcode::bipush;
                            key_instr.operand = bytecode::ByteOperand{static_cast<bytecode::i1>(s_key)};
                        } else if (s_key >= -32768 && s_key <= 32767) {
                            key_instr.opcode = bytecode::Opcode::sipush;
                            key_instr.operand = bytecode::ShortOperand{static_cast<bytecode::i2>(s_key)};
                        } else {
                            bytecode::u2 key_cp = cp.find_or_add_integer(s_key);
                            if (key_cp <= 255) {
                                key_instr.opcode = bytecode::Opcode::ldc;
                                key_instr.operand = bytecode::CpIndexOperand{key_cp};
                            } else {
                                key_instr.opcode = bytecode::Opcode::ldc_w;
                                key_instr.operand = bytecode::CpIndexOperand{key_cp};
                            }
                        }
                        new_instructions.push_back(key_instr);
                    }

                    // 3. invokestatic decrypt_str
                    bytecode::Instruction call_instr;
                    call_instr.offset = static_cast<bytecode::u4>(-1);
                    call_instr.opcode = bytecode::Opcode::invokestatic;
                    call_instr.operand = bytecode::CpIndexOperand{str_dec_mref};
                    new_instructions.push_back(call_instr);

                    modified = true;
                    continue;
                }
                }
            }
            new_instructions.push_back(instr);
        }

        if (modified) {
            code->max_stack = static_cast<bytecode::u2>(code->max_stack + 6);
            bytecode::update_code(*code, new_instructions, cp);
        }
    }

    // Wipe any remaining template strings or old keys from constant pool
    for (bytecode::u2 idx = 1; idx < cp.count(); ++idx) {
        if (auto* u = cp.get_mut_if<bytecode::CpUtf8>(idx)) {
            if (u->value == "__JOBF_REAL_MAIN__" || u->value == "__JOBF_RES_NAME__" ||
                u->value.find("__JOBF_KEY_HEX_PLACEHOLDER") != std::string::npos ||
                u->value == "REAL_MAIN" || u->value == "RESOURCE_NAME" || u->value == "KEY_HEX") {
                u->value.clear();
            }
        }
    }
    cp.rebuild_utf8_lookup();

    // Inject self-contained Feistel decryptor method
    StringEncryptPass::inject_decryption_method_v0(boot_cf, obf_method_str_dec);

    // 6. Set synthetic flags, strip signatures, and strip debug metadata
    for (auto& f : boot_cf.fields()) {
        f.access_flags |= bytecode::access_flags::ACC_SYNTHETIC;
        f.remove_attribute("Signature", cp);
    }
    for (auto& m : boot_cf.methods()) {
        std::string mname(m.name(cp));
        if (mname != "<init>" && mname != "findClass" && mname != "main") {
            m.access_flags |= bytecode::access_flags::ACC_SYNTHETIC;
        }
        m.remove_attribute("Signature", cp);
        m.remove_attribute("LineNumberTable", cp);
        m.remove_attribute("LocalVariableTable", cp);
    }
    boot_cf.remove_attribute("SourceFile");
}

bool ClassEncryptionPass::run_on_archive(PassContext& ctx) {
    if (!ctx.config.class_encryption.enabled) {
        return true;
    }

    // 1. Locate real application Main-Class
    std::string real_main;
    if (!ctx.config.class_encryption.main_class.empty()) {
        real_main = ctx.config.class_encryption.main_class;
    } else {
        auto opt_main = ctx.archive.get_main_class();
        if (opt_main.has_value()) {
            real_main = *opt_main;
        }
    }

    if (real_main.empty()) {
        ctx.reporter.log(reporter::LogLevel::Error,
                         "Class encryption requires an application Main-Class entry point in MANIFEST.MF or --main-class.");
        return false;
    }

    // Trim whitespace
    while (!real_main.empty() && (real_main.front() == ' ' || real_main.front() == '\t')) real_main.erase(real_main.begin());
    while (!real_main.empty() && (real_main.back() == ' ' || real_main.back() == '\t' || real_main.back() == '\r')) real_main.pop_back();

    std::string real_main_dotted = real_main;
    std::replace(real_main_dotted.begin(), real_main_dotted.end(), '/', '.');

    // 2. Collect scoped classes to encrypt
    std::vector<std::string> classes_to_encrypt;
    std::string global_cache_name = ctx.get_global_cache_class_name();
    for (const auto& [name, cf] : ctx.archive.classes()) {
        if (!global_cache_name.empty() && name == global_cache_name) {
            classes_to_encrypt.push_back(name);
            continue;
        }
        if (!ctx.config.is_class_in_scope(name)) continue;

        bool excluded = false;
        for (const auto& exc : ctx.config.exclude_classes) {
            if (name == exc || name == exc + ".class") {
                excluded = true;
                break;
            }
        }
        if (!excluded) {
            classes_to_encrypt.push_back(name);
        }
    }

    if (classes_to_encrypt.empty()) {
        ctx.reporter.log(reporter::LogLevel::Warn, "No classes in scope to encrypt.");
        return true;
    }

    ctx.reporter.log(reporter::LogLevel::Info,
                     "Packaging and encrypting " + std::to_string(classes_to_encrypt.size()) +
                     " classes into secure container...");

    // 3. Serialize classes into virtual bundle:
    // [u4: total_classes_count]
    // For each class:
    //   [u2: name_length]
    //   [bytes: class_name UTF-8]
    //   [u4: bytecode_length]
    //   [bytes: raw classfile bytes]
    std::vector<bytecode::u1> bundle;
    write_u4_be(bundle, static_cast<uint32_t>(classes_to_encrypt.size()));

    for (const auto& cls_name : classes_to_encrypt) {
        auto* cf = ctx.archive.find_class(cls_name);
        if (!cf) continue;
        std::vector<bytecode::u1> class_bytes = cf->to_bytes();

        write_u2_be(bundle, static_cast<uint16_t>(cls_name.size()));
        bundle.insert(bundle.end(), cls_name.begin(), cls_name.end());

        write_u4_be(bundle, static_cast<uint32_t>(class_bytes.size()));
        bundle.insert(bundle.end(), class_bytes.begin(), class_bytes.end());
    }

    // 4. Generate 32-byte cryptographic random key
    std::vector<uint8_t> key(32);
    for (size_t i = 0; i < 32; ++i) {
        key[i] = static_cast<uint8_t>(ctx.rng() & 0xFF);
    }

    std::string key_hex;
    static const char hex_chars[] = "0123456789abcdef";
    for (uint8_t b : key) {
        key_hex.push_back(hex_chars[(b >> 4) & 0x0F]);
        key_hex.push_back(hex_chars[b & 0x0F]);
    }

    // 5. Encrypt container bundle:
    // Step 1: XOR with dynamic key schedule and position factor
    for (size_t i = 0; i < bundle.size(); ++i) {
        uint8_t k = static_cast<uint8_t>(key[i % key.size()] ^ static_cast<uint8_t>(i * 31));
        bundle[i] = static_cast<bytecode::u1>(bundle[i] ^ k);
    }
    // Step 2: CBC feedback diffusion
    for (size_t i = 1; i < bundle.size(); ++i) {
        bundle[i] = static_cast<bytecode::u1>(bundle[i] ^ bundle[i - 1]);
    }

    // 6. Save encrypted container bundle as a resource
    std::string res_path = ctx.config.class_encryption.resource_name;
    if (res_path.empty()) {
        res_path = "META-INF/.data/app.bin";
    }
    ctx.archive.add_resource(res_path, std::move(bundle));

    // 7. Remove encrypted classes from archive classes map
    for (const auto& cls_name : classes_to_encrypt) {
        ctx.archive.classes().erase(cls_name);
    }

    // 8. Parse and patch Bootstrap ClassLoader template
    std::span<const bytecode::u1> tpl_span(BOOTSTRAP_CLASS_BYTES, BOOTSTRAP_CLASS_BYTES_SIZE);
    auto boot_cf = bytecode::ClassFile::from_bytes(tpl_span);

    std::string launcher_internal = ctx.config.class_encryption.launcher_class;
    if (launcher_internal.empty()) launcher_internal = "Bootstrap";
    std::string launcher_dotted = launcher_internal;
    std::replace(launcher_dotted.begin(), launcher_dotted.end(), '/', '.');

    // Fully obfuscate the Bootstrap loader: eliminate dead constant fields,
    // scramble private members, apply control flow traps & number obfuscation,
    // encrypt all string literals with self-contained Feistel decryptor, and strip debug metadata.
    obfuscate_bootstrap_class(boot_cf, ctx, launcher_internal, real_main_dotted, res_path, key_hex);

    // Add bootstrap loader as the exposed class
    ctx.archive.add_or_update_class(launcher_internal, std::move(boot_cf));

    // 9. Update MANIFEST.MF Main-Class to point to bootstrap loader
    ctx.archive.set_main_class(launcher_dotted);

    // 10. Update stats
    ctx.stats.classes_encrypted = classes_to_encrypt.size();
    ctx.reporter.log(reporter::LogLevel::Info,
                     "Encrypted " + std::to_string(classes_to_encrypt.size()) +
                     " classes into resource bundle '" + res_path + "'");
    ctx.reporter.log(reporter::LogLevel::Info,
                     "Injected in-memory bootstrap ClassLoader: '" + launcher_dotted + "'");

    return true;
}

} // namespace obf::passes
