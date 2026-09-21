#include "passes/control_flow_pass.hpp"
#include "bytecode/instructions.hpp"
#include "bytecode/attributes.hpp"
#include <random>
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <map>
#include <sstream>
#include <iomanip>

namespace obf::passes {

static int get_stack_delta(bytecode::Opcode op, const bytecode::InstructionOperand& operand, const bytecode::ConstantPool& cp) {
    switch (op) {
        case bytecode::Opcode::nop:
            return 0;
        case bytecode::Opcode::aconst_null:
        case bytecode::Opcode::iconst_m1:
        case bytecode::Opcode::iconst_0:
        case bytecode::Opcode::iconst_1:
        case bytecode::Opcode::iconst_2:
        case bytecode::Opcode::iconst_3:
        case bytecode::Opcode::iconst_4:
        case bytecode::Opcode::iconst_5:
        case bytecode::Opcode::fconst_0:
        case bytecode::Opcode::fconst_1:
        case bytecode::Opcode::fconst_2:
        case bytecode::Opcode::bipush:
        case bytecode::Opcode::sipush:
        case bytecode::Opcode::iload:
        case bytecode::Opcode::fload:
        case bytecode::Opcode::aload:
        case bytecode::Opcode::iload_0:
        case bytecode::Opcode::iload_1:
        case bytecode::Opcode::iload_2:
        case bytecode::Opcode::iload_3:
        case bytecode::Opcode::fload_0:
        case bytecode::Opcode::fload_1:
        case bytecode::Opcode::fload_2:
        case bytecode::Opcode::fload_3:
        case bytecode::Opcode::aload_0:
        case bytecode::Opcode::aload_1:
        case bytecode::Opcode::aload_2:
        case bytecode::Opcode::aload_3:
            return 1;
        case bytecode::Opcode::lconst_0:
        case bytecode::Opcode::lconst_1:
        case bytecode::Opcode::dconst_0:
        case bytecode::Opcode::dconst_1:
        case bytecode::Opcode::lload:
        case bytecode::Opcode::dload:
        case bytecode::Opcode::lload_0:
        case bytecode::Opcode::lload_1:
        case bytecode::Opcode::lload_2:
        case bytecode::Opcode::lload_3:
        case bytecode::Opcode::dload_0:
        case bytecode::Opcode::dload_1:
        case bytecode::Opcode::dload_2:
        case bytecode::Opcode::dload_3:
            return 2;
        case bytecode::Opcode::ldc:
        case bytecode::Opcode::ldc_w:
            return 1;
        case bytecode::Opcode::ldc2_w:
            return 2;
        case bytecode::Opcode::istore:
        case bytecode::Opcode::fstore:
        case bytecode::Opcode::astore:
        case bytecode::Opcode::istore_0:
        case bytecode::Opcode::istore_1:
        case bytecode::Opcode::istore_2:
        case bytecode::Opcode::istore_3:
        case bytecode::Opcode::fstore_0:
        case bytecode::Opcode::fstore_1:
        case bytecode::Opcode::fstore_2:
        case bytecode::Opcode::fstore_3:
        case bytecode::Opcode::astore_0:
        case bytecode::Opcode::astore_1:
        case bytecode::Opcode::astore_2:
        case bytecode::Opcode::astore_3:
        case bytecode::Opcode::pop:
            return -1;
        case bytecode::Opcode::lstore:
        case bytecode::Opcode::dstore:
        case bytecode::Opcode::lstore_0:
        case bytecode::Opcode::lstore_1:
        case bytecode::Opcode::lstore_2:
        case bytecode::Opcode::lstore_3:
        case bytecode::Opcode::dstore_0:
        case bytecode::Opcode::dstore_1:
        case bytecode::Opcode::dstore_2:
        case bytecode::Opcode::dstore_3:
        case bytecode::Opcode::pop2:
            return -2;
        case bytecode::Opcode::dup:
        case bytecode::Opcode::dup_x1:
        case bytecode::Opcode::dup_x2:
            return 1;
        case bytecode::Opcode::dup2:
        case bytecode::Opcode::dup2_x1:
        case bytecode::Opcode::dup2_x2:
            return 2;
        case bytecode::Opcode::swap:
        case bytecode::Opcode::iinc:
        case bytecode::Opcode::ineg:
        case bytecode::Opcode::lneg:
        case bytecode::Opcode::fneg:
        case bytecode::Opcode::dneg:
        case bytecode::Opcode::i2b:
        case bytecode::Opcode::i2c:
        case bytecode::Opcode::i2s:
            return 0;
        case bytecode::Opcode::iadd:
        case bytecode::Opcode::isub:
        case bytecode::Opcode::imul:
        case bytecode::Opcode::idiv:
        case bytecode::Opcode::irem:
        case bytecode::Opcode::iand:
        case bytecode::Opcode::ior:
        case bytecode::Opcode::ixor:
        case bytecode::Opcode::ishl:
        case bytecode::Opcode::ishr:
        case bytecode::Opcode::iushr:
        case bytecode::Opcode::fadd:
        case bytecode::Opcode::fsub:
        case bytecode::Opcode::fmul:
        case bytecode::Opcode::fdiv:
        case bytecode::Opcode::frem:
        case bytecode::Opcode::fcmpl:
        case bytecode::Opcode::fcmpg:
            return -1;
        case bytecode::Opcode::ladd:
        case bytecode::Opcode::lsub:
        case bytecode::Opcode::lmul:
        case bytecode::Opcode::ldiv:
        case bytecode::Opcode::lrem:
        case bytecode::Opcode::land:
        case bytecode::Opcode::lor:
        case bytecode::Opcode::lxor:
        case bytecode::Opcode::dadd:
        case bytecode::Opcode::dsub:
        case bytecode::Opcode::dmul:
        case bytecode::Opcode::ddiv:
        case bytecode::Opcode::drem:
            return -2;
        case bytecode::Opcode::lcmp:
        case bytecode::Opcode::dcmpl:
        case bytecode::Opcode::dcmpg:
            return -3;
        case bytecode::Opcode::i2l:
        case bytecode::Opcode::i2d:
        case bytecode::Opcode::f2l:
        case bytecode::Opcode::f2d:
            return 1;
        case bytecode::Opcode::l2i:
        case bytecode::Opcode::l2f:
        case bytecode::Opcode::d2i:
        case bytecode::Opcode::d2f:
            return -1;
        case bytecode::Opcode::new_op:
            return 1;
        case bytecode::Opcode::newarray:
        case bytecode::Opcode::anewarray:
        case bytecode::Opcode::arraylength:
        case bytecode::Opcode::checkcast:
        case bytecode::Opcode::instanceof_op:
            return 0;
        case bytecode::Opcode::iaload:
        case bytecode::Opcode::faload:
        case bytecode::Opcode::baload:
        case bytecode::Opcode::caload:
        case bytecode::Opcode::saload:
        case bytecode::Opcode::aaload:
            return -1;
        case bytecode::Opcode::laload:
        case bytecode::Opcode::daload:
            return 0;
        case bytecode::Opcode::iastore:
        case bytecode::Opcode::fastore:
        case bytecode::Opcode::bastore:
        case bytecode::Opcode::castore:
        case bytecode::Opcode::sastore:
        case bytecode::Opcode::aastore:
            return -3;
        case bytecode::Opcode::lastore:
        case bytecode::Opcode::dastore:
            return -4;
        case bytecode::Opcode::monitorenter:
        case bytecode::Opcode::monitorexit:
            return -1;
        case bytecode::Opcode::ireturn:
        case bytecode::Opcode::freturn:
        case bytecode::Opcode::areturn:
        case bytecode::Opcode::athrow:
            return -1;
        case bytecode::Opcode::lreturn:
        case bytecode::Opcode::dreturn:
            return -2;
        case bytecode::Opcode::return_op:
            return 0;
        case bytecode::Opcode::getstatic: {
            if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&operand)) {
                if (const auto* fr = cp.get_if<bytecode::CpFieldref>(cp_op->index)) {
                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(fr->name_and_type_index)) {
                        std::string_view desc = cp.get_utf8(nat->descriptor_index);
                        return (desc == "J" || desc == "D") ? 2 : 1;
                    }
                }
            }
            return 1;
        }
        case bytecode::Opcode::putstatic: {
            if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&operand)) {
                if (const auto* fr = cp.get_if<bytecode::CpFieldref>(cp_op->index)) {
                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(fr->name_and_type_index)) {
                        std::string_view desc = cp.get_utf8(nat->descriptor_index);
                        return (desc == "J" || desc == "D") ? -2 : -1;
                    }
                }
            }
            return -1;
        }
        case bytecode::Opcode::getfield: {
            if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&operand)) {
                if (const auto* fr = cp.get_if<bytecode::CpFieldref>(cp_op->index)) {
                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(fr->name_and_type_index)) {
                        std::string_view desc = cp.get_utf8(nat->descriptor_index);
                        return (desc == "J" || desc == "D") ? 1 : 0;
                    }
                }
            }
            return 0;
        }
        case bytecode::Opcode::putfield: {
            if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&operand)) {
                if (const auto* fr = cp.get_if<bytecode::CpFieldref>(cp_op->index)) {
                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(fr->name_and_type_index)) {
                        std::string_view desc = cp.get_utf8(nat->descriptor_index);
                        return (desc == "J" || desc == "D") ? -3 : -2;
                    }
                }
            }
            return -2;
        }
        case bytecode::Opcode::invokevirtual:
        case bytecode::Opcode::invokespecial:
        case bytecode::Opcode::invokestatic:
        case bytecode::Opcode::invokeinterface: {
            bytecode::u2 cp_idx = 0;
            if (const auto* cp_op = std::get_if<bytecode::CpIndexOperand>(&operand)) {
                cp_idx = cp_op->index;
            } else if (const auto* if_op = std::get_if<bytecode::InterfaceCallOperand>(&operand)) {
                cp_idx = if_op->index;
            }
            if (cp_idx != 0) {
                std::string_view desc;
                if (const auto* mr = cp.get_if<bytecode::CpMethodref>(cp_idx)) {
                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(mr->name_and_type_index)) {
                        desc = cp.get_utf8(nat->descriptor_index);
                    }
                } else if (const auto* imr = cp.get_if<bytecode::CpInterfaceMethodref>(cp_idx)) {
                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(imr->name_and_type_index)) {
                        desc = cp.get_utf8(nat->descriptor_index);
                    }
                }
                if (!desc.empty()) {
                    int param_slots = 0;
                    size_t pos = 1;
                    while (pos < desc.size() && desc[pos] != ')') {
                        char c = desc[pos];
                        if (c == 'J' || c == 'D') param_slots += 2;
                        else if (c == 'L') {
                            size_t sc = desc.find(';', pos);
                            if (sc != std::string_view::npos) pos = sc;
                            param_slots += 1;
                        } else if (c == '[') {
                            while (pos < desc.size() && desc[pos] == '[') ++pos;
                            if (pos < desc.size() && desc[pos] == 'L') {
                                size_t sc = desc.find(';', pos);
                                if (sc != std::string_view::npos) pos = sc;
                            }
                            param_slots += 1;
                        } else {
                            param_slots += 1;
                        }
                        pos++;
                    }
                    if (op != bytecode::Opcode::invokestatic) {
                        param_slots += 1; // receiver
                    }
                    char ret_char = (pos + 1 < desc.size()) ? desc[pos + 1] : 'V';
                    int ret_slots = (ret_char == 'V') ? 0 : ((ret_char == 'J' || ret_char == 'D') ? 2 : 1);
                    return ret_slots - param_slots;
                }
            }
            return 0;
        }
        case bytecode::Opcode::invokedynamic: {
            if (const auto* id_op = std::get_if<bytecode::InvokeDynamicOperand>(&operand)) {
                if (const auto* indy = cp.get_if<bytecode::CpInvokeDynamic>(id_op->index)) {
                    if (const auto* nat = cp.get_if<bytecode::CpNameAndType>(indy->name_and_type_index)) {
                        std::string_view desc = cp.get_utf8(nat->descriptor_index);
                        int param_slots = 0;
                        size_t pos = 1;
                        while (pos < desc.size() && desc[pos] != ')') {
                            char c = desc[pos];
                            if (c == 'J' || c == 'D') param_slots += 2;
                            else if (c == 'L') {
                                size_t sc = desc.find(';', pos);
                                if (sc != std::string_view::npos) pos = sc;
                                param_slots += 1;
                            } else if (c == '[') {
                                while (pos < desc.size() && desc[pos] == '[') ++pos;
                                if (pos < desc.size() && desc[pos] == 'L') {
                                    size_t sc = desc.find(';', pos);
                                    if (sc != std::string_view::npos) pos = sc;
                                }
                                param_slots += 1;
                            } else {
                                param_slots += 1;
                            }
                            pos++;
                        }
                        char ret_char = (pos + 1 < desc.size()) ? desc[pos + 1] : 'V';
                        int ret_slots = (ret_char == 'V') ? 0 : ((ret_char == 'J' || ret_char == 'D') ? 2 : 1);
                        return ret_slots - param_slots;
                    }
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

static void emit_decoy_throw(std::vector<bytecode::Instruction>& instrs,
                             bytecode::ConstantPool& cp,
                             PassContext& ctx,
                             uint32_t off = static_cast<uint32_t>(-1)) {
    static const char* const kExceptionTypes[] = {
        "java/lang/IllegalStateException",
        "java/lang/SecurityException",
        "java/lang/RuntimeException",
        "java/lang/IllegalArgumentException"
    };

    std::uniform_int_distribution<size_t> ex_dist(0, 3);
    const char* ex_class = kExceptionTypes[ex_dist(ctx.rng)];

    uint32_t hex_tag = static_cast<uint32_t>(ctx.rng() & 0xFFFF);
    std::stringstream ss;
    if (std::string_view(ex_class).find("Security") != std::string_view::npos) {
        ss << "Security integrity verification failure [0x" << std::hex << std::uppercase << hex_tag << "]";
    } else if (std::string_view(ex_class).find("IllegalState") != std::string_view::npos) {
        ss << "Execution state checkpoint mismatch: 0x" << std::hex << std::uppercase << hex_tag;
    } else if (std::string_view(ex_class).find("IllegalArgument") != std::string_view::npos) {
        ss << "Control flow invariant violation [0x" << std::hex << std::uppercase << hex_tag << "]";
    } else {
        ss << "Runtime verification checkpoint fault: 0x" << std::hex << std::uppercase << hex_tag;
    }

    bytecode::u2 ex_cls = cp.find_or_add_class(ex_class);
    bytecode::u2 ex_init = cp.find_or_add_methodref(ex_class, "<init>", "(Ljava/lang/String;)V");
    bytecode::u2 msg_str = cp.find_or_add_string(ss.str());

    auto add = [&](bytecode::Opcode op, bytecode::InstructionOperand operand = bytecode::NoOperand{}) {
        bytecode::Instruction inst;
        inst.offset = (off == static_cast<uint32_t>(-1)) ? static_cast<bytecode::u4>(-1) : off++;
        inst.opcode = op;
        inst.operand = operand;
        instrs.push_back(inst);
    };

    add(bytecode::Opcode::new_op, bytecode::CpIndexOperand{ex_cls});
    add(bytecode::Opcode::dup);
    if (msg_str <= 255) {
        add(bytecode::Opcode::ldc, bytecode::CpIndexOperand{msg_str});
    } else {
        add(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{msg_str});
    }
    add(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{ex_init});
    add(bytecode::Opcode::athrow);
}

static void emit_opaque_branch(std::vector<bytecode::Instruction>& instrs,
                               bytecode::ConstantPool& cp,
                               PassContext& ctx,
                               uint32_t target,
                               bool jump_if_true,
                               uint32_t& off) {
    // Contract: emitted code assumes an empty entry stack, leaves exactly one
    // int for the branch, and peaks at 5 stack slots. Callers needing values
    // underneath must push them after the predicate, not before.
    auto add_i = [&](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = (off == static_cast<uint32_t>(-1)) ? static_cast<bytecode::u4>(-1) : off++;
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        instrs.push_back(inst);
    };

    auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = (off == static_cast<uint32_t>(-1)) ? static_cast<bytecode::u4>(-1) : off++;
        inst.opcode = op;
        inst.operand = operand;
        instrs.push_back(inst);
    };

    bytecode::u2 mref_millis = cp.find_or_add_methodref("java/lang/System", "currentTimeMillis", "()J");
    auto push_runtime_int = [&]() {
        add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{mref_millis});
        add_i(bytecode::Opcode::l2i);
    };

    std::uniform_int_distribution<int> inv_dist(0, 8);
    int inv = inv_dist(ctx.rng);

    auto push_const = [&](int32_t v) {
        if (v >= -128 && v <= 127) {
            add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(v)});
        } else if (v >= -32768 && v <= 32767) {
            add_op(bytecode::Opcode::sipush, bytecode::ShortOperand{static_cast<bytecode::i2>(v)});
        } else {
            bytecode::u2 cidx = cp.find_or_add_integer(v);
            if (cidx <= 255) {
                add_op(bytecode::Opcode::ldc, bytecode::CpIndexOperand{cidx});
            } else {
                add_op(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{cidx});
            }
        }
    };

    if (inv == 0) {
        // Consecutive Parity with runtime invocation: (t * (t + 1)) & 1 == 0
        push_runtime_int();
        add_i(bytecode::Opcode::dup);
        add_i(bytecode::Opcode::iconst_1);
        add_i(bytecode::Opcode::iadd);
        add_i(bytecode::Opcode::imul);
        add_i(bytecode::Opcode::iconst_1);
        add_i(bytecode::Opcode::iand);
        bytecode::Opcode branch_op = jump_if_true ? bytecode::Opcode::ifeq : bytecode::Opcode::ifne;
        add_op(branch_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(target)});
    } else if (inv == 1) {
        // Bitwise Negation with runtime invocation: (t & ~t) == 0
        push_runtime_int();
        add_i(bytecode::Opcode::dup);
        add_i(bytecode::Opcode::iconst_m1);
        add_i(bytecode::Opcode::ixor);
        add_i(bytecode::Opcode::iand);
        bytecode::Opcode branch_op = jump_if_true ? bytecode::Opcode::ifeq : bytecode::Opcode::ifne;
        add_op(branch_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(target)});
    } else if (inv == 2) {
        // Bitwise Inverted OR with runtime invocation: (t | ~t) == -1
        push_runtime_int();
        add_i(bytecode::Opcode::dup);
        add_i(bytecode::Opcode::iconst_m1);
        add_i(bytecode::Opcode::ixor);
        add_i(bytecode::Opcode::ior);
        add_i(bytecode::Opcode::iconst_m1);
        bytecode::Opcode branch_op = jump_if_true ? bytecode::Opcode::if_icmpeq : bytecode::Opcode::if_icmpne;
        add_op(branch_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(target)});
    } else if (inv == 3) {
        // Odd Number LSB with runtime invocation: ((t * 2 + 1) & 1) == 1
        push_runtime_int();
        add_i(bytecode::Opcode::iconst_2);
        add_i(bytecode::Opcode::imul);
        add_i(bytecode::Opcode::iconst_1);
        add_i(bytecode::Opcode::iadd);
        add_i(bytecode::Opcode::iconst_1);
        add_i(bytecode::Opcode::iand);
        add_i(bytecode::Opcode::iconst_1);
        bytecode::Opcode branch_op = jump_if_true ? bytecode::Opcode::if_icmpeq : bytecode::Opcode::if_icmpne;
        add_op(branch_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(target)});
    } else if (inv == 4) {
        // Self XOR with runtime invocation: (t ^ t) == 0
        push_runtime_int();
        add_i(bytecode::Opcode::dup);
        add_i(bytecode::Opcode::ixor);
        bytecode::Opcode branch_op = jump_if_true ? bytecode::Opcode::ifeq : bytecode::Opcode::ifne;
        add_op(branch_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(target)});
    } else if (inv <= 5) {
        // Multi-round parity with runtime invocation: ((t * 4 + 2) & 1) == 0
        push_runtime_int();
        add_i(bytecode::Opcode::iconst_4);
        add_i(bytecode::Opcode::imul);
        add_i(bytecode::Opcode::iconst_2);
        add_i(bytecode::Opcode::iadd);
        add_i(bytecode::Opcode::iconst_1);
        add_i(bytecode::Opcode::iand);
        bytecode::Opcode branch_op = jump_if_true ? bytecode::Opcode::ifeq : bytecode::Opcode::ifne;
        add_op(branch_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(target)});
    } else if (inv == 6) {
        // MBA identity: (t ^ C) + 2 * (t & C) == t + C (holds mod 2^32).
        int32_t c = static_cast<int32_t>(ctx.rng());
        push_runtime_int();
        push_const(c);
        add_i(bytecode::Opcode::iadd); // [B = t + C]
        push_runtime_int();
        add_i(bytecode::Opcode::dup);
        push_const(c);
        add_i(bytecode::Opcode::ixor); // [B, t, t^C]
        add_i(bytecode::Opcode::dup_x1); // [B, t^C, t, t^C]
        add_i(bytecode::Opcode::swap); // [B, t^C, t^C, t]
        push_const(c);
        add_i(bytecode::Opcode::iand); // [B, t^C, t^C, t&C]
        add_i(bytecode::Opcode::iconst_2);
        add_i(bytecode::Opcode::imul);
        add_i(bytecode::Opcode::iadd); // [B, t^C, A]
        add_i(bytecode::Opcode::swap);
        add_i(bytecode::Opcode::pop); // [B, A]
        bytecode::Opcode branch_op = jump_if_true ? bytecode::Opcode::if_icmpeq : bytecode::Opcode::if_icmpne;
        add_op(branch_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(target)});
    } else if (inv == 7) {
        // MBA identity: (t | C) - (t & C) == t ^ C (holds mod 2^32).
        int32_t c = static_cast<int32_t>(ctx.rng());
        push_runtime_int();
        push_const(c);
        add_i(bytecode::Opcode::ixor); // [D = t^C]
        push_runtime_int(); // [D, t]
        add_i(bytecode::Opcode::dup);
        push_const(c);
        add_i(bytecode::Opcode::ior); // [D, t, t|C]
        add_i(bytecode::Opcode::swap); // [D, t|C, t]
        push_const(c);
        add_i(bytecode::Opcode::iand); // [D, t|C, t&C]
        add_i(bytecode::Opcode::isub); // [D, E]
        bytecode::Opcode branch_op = jump_if_true ? bytecode::Opcode::if_icmpeq : bytecode::Opcode::if_icmpne;
        add_op(branch_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(target)});
    } else {
        // MBA identity: (t & C) + (t | C) == t + C (holds mod 2^32).
        int32_t c = static_cast<int32_t>(ctx.rng());
        push_runtime_int();
        push_const(c);
        add_i(bytecode::Opcode::iadd); // [B = t + C]
        push_runtime_int();
        add_i(bytecode::Opcode::dup);
        push_const(c);
        add_i(bytecode::Opcode::iand); // [B, t, t&C]
        add_i(bytecode::Opcode::dup_x1); // [B, t&C, t, t&C]
        add_i(bytecode::Opcode::swap); // [B, t&C, t&C, t]
        push_const(c);
        add_i(bytecode::Opcode::ior); // [B, t&C, t&C, t|C]
        add_i(bytecode::Opcode::iadd); // [B, t&C, E]
        add_i(bytecode::Opcode::swap);
        add_i(bytecode::Opcode::pop); // [B, E]
        bytecode::Opcode branch_op = jump_if_true ? bytecode::Opcode::if_icmpeq : bytecode::Opcode::if_icmpne;
        add_op(branch_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(target)});
    }
}

void ControlFlowPass::insert_opaque_predicate(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx) {
    if (code.code.size() < 4) return;
    auto instructions = bytecode::disassemble(code.code);
    if (instructions.empty()) return;

    bytecode::u4 orig_first_offset = instructions[0].offset;

    std::vector<bytecode::Instruction> pred_instrs;
    uint32_t dummy_off = static_cast<uint32_t>(-1);

    // Emit opaque branch jumping to orig_first_offset when true
    emit_opaque_branch(pred_instrs, cp, ctx, orig_first_offset, true, dummy_off);

    // Deceptive dead branch (never reached at runtime)
    emit_decoy_throw(pred_instrs, cp, ctx);

    std::vector<bytecode::Instruction> new_instructions;
    new_instructions.reserve(pred_instrs.size() + instructions.size());
    for (auto& pi : pred_instrs) {
        new_instructions.push_back(pi);
    }
    for (auto& inst : instructions) {
        new_instructions.push_back(std::move(inst));
    }

    code.max_stack = std::max<bytecode::u2>(code.max_stack, 5);
    bytecode::update_code(code, new_instructions, cp);
    ctx.stats.methods_flattened++;
}

void ControlFlowPass::inject_decompiler_traps(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx) {
    if (code.code.size() < 4) return;
    auto instructions = bytecode::disassemble(code.code);
    if (instructions.empty()) return;

    bytecode::u4 orig_first_offset = instructions[0].offset;

    std::uniform_int_distribution<int> trap_topo_dist(0, 2);
    int topo = trap_topo_dist(ctx.rng);

    uint32_t base_id = 0x71000000;
    uint32_t bci_counter = base_id;

    std::vector<bytecode::Instruction> trap_instrs;

    auto add_i = [&](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = bci_counter++;
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        trap_instrs.push_back(inst);
    };

    auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = bci_counter++;
        inst.opcode = op;
        inst.operand = operand;
        trap_instrs.push_back(inst);
    };

    if (topo == 0) {
        // Architecture 0: Irreducible Loop Trap with Decoy Payload & Dynamic Algebraic Predicates
        uint32_t bci_node_a = base_id + 50;
        uint32_t bci_node_b = base_id + 100;

        // Entry: Evaluate Invariant P1 (always true) -> jump to Node A
        emit_opaque_branch(trap_instrs, cp, ctx, bci_node_a, true, bci_counter);
        // Fallthrough: goto Node B (unreachable)
        add_op(bytecode::Opcode::goto_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_node_b)});

        // Node B (dead): Decoy payload
        bci_counter = bci_node_b;
        std::uniform_int_distribution<int> b_payload_dist(0, 1);
        if (b_payload_dist(ctx.rng) == 0) {
            emit_decoy_throw(trap_instrs, cp, ctx, bci_counter);
        } else {
            std::uniform_int_distribution<int> r_dist(5, 50);
            add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(r_dist(ctx.rng))});
            add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(r_dist(ctx.rng))});
            add_i(bytecode::Opcode::imul);
            add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(r_dist(ctx.rng))});
            add_i(bytecode::Opcode::iadd);
            add_i(bytecode::Opcode::pop);
            add_op(bytecode::Opcode::goto_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(bci_node_a)});
        }

        // Node A: Evaluate Invariant P2 (never true) -> if taken, jump to Node B
        bci_counter = bci_node_a;
        emit_opaque_branch(trap_instrs, cp, ctx, bci_node_b, false, bci_counter);
        // Fallthrough: goto real method body
        add_op(bytecode::Opcode::goto_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});

    } else if (topo == 1) {
        // Architecture 1: Opaque Multi-Way State Switch Trap
        // Expression: ((System.currentTimeMillis() * 2) & 1) + 1 is always 1 at runtime!
        uint32_t bci_decoy_err = base_id + 50;
        uint32_t bci_decoy_calc = base_id + 100;
        uint32_t bci_decoy_def = base_id + 150;

        bytecode::u2 mref_millis = cp.find_or_add_methodref("java/lang/System", "currentTimeMillis", "()J");
        add_op(bytecode::Opcode::invokestatic, bytecode::CpIndexOperand{mref_millis});
        add_i(bytecode::Opcode::l2i);
        add_i(bytecode::Opcode::iconst_2);
        add_i(bytecode::Opcode::imul);
        add_i(bytecode::Opcode::iconst_1);
        add_i(bytecode::Opcode::iand);
        add_i(bytecode::Opcode::iconst_1);
        add_i(bytecode::Opcode::iadd);

        bytecode::TableSwitchData sw;
        sw.low = 1;
        sw.high = 3;
        sw.default_target_abs = static_cast<bytecode::i4>(bci_decoy_def);
        sw.jump_targets_abs.push_back(static_cast<bytecode::i4>(orig_first_offset)); // case 1: REAL
        sw.jump_targets_abs.push_back(static_cast<bytecode::i4>(bci_decoy_err));    // case 2: DECOY ERR
        sw.jump_targets_abs.push_back(static_cast<bytecode::i4>(bci_decoy_calc));   // case 3: DECOY CALC
        sw.jump_offsets.resize(3, 0);
        add_op(bytecode::Opcode::tableswitch, sw);

        // case 2: Decoy error throw
        bci_counter = bci_decoy_err;
        emit_decoy_throw(trap_instrs, cp, ctx, bci_counter);

        // case 3: Decoy calculation and jump to orig
        bci_counter = bci_decoy_calc;
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{37});
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{13});
        add_i(bytecode::Opcode::imul);
        add_i(bytecode::Opcode::pop);
        add_op(bytecode::Opcode::goto_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});

        // default: Decoy throw
        bci_counter = bci_decoy_def;
        emit_decoy_throw(trap_instrs, cp, ctx, bci_counter);

    } else {
        // Architecture 2: Cascaded Guard Checkpoints
        uint32_t bci_trap1 = base_id + 50;
        uint32_t bci_trap2 = base_id + 100;

        // Guard 1: Invariant P1 (always true) -> jump over trap 1 if false
        emit_opaque_branch(trap_instrs, cp, ctx, bci_trap1, false, bci_counter);

        // Guard 2: Invariant P2 (always true) -> jump over trap 2 if false
        emit_opaque_branch(trap_instrs, cp, ctx, bci_trap2, false, bci_counter);

        // All guards passed -> goto real method body
        add_op(bytecode::Opcode::goto_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});

        // Trap 1
        bci_counter = bci_trap1;
        emit_decoy_throw(trap_instrs, cp, ctx, bci_counter);

        // Trap 2
        bci_counter = bci_trap2;
        emit_decoy_throw(trap_instrs, cp, ctx, bci_counter);
    }

    std::vector<bytecode::Instruction> new_instructions;
    new_instructions.reserve(trap_instrs.size() + instructions.size());
    for (auto& ti : trap_instrs) {
        new_instructions.push_back(ti);
    }
    for (auto& inst : instructions) {
        new_instructions.push_back(std::move(inst));
    }

    code.max_stack = std::max<bytecode::u2>(code.max_stack, 5);
    bytecode::update_code(code, new_instructions, cp);
    ctx.stats.methods_flattened++;
}

// Exception-dispatch hardening.
//
// Prepends an opaque-guarded decoy prelude plus catching handler(s) that
// contain dead irreducible loops:
//
//   P0: <opaque predicate, always false> ; ifne TRAMP (dead edge, arrives [])
//       goto REAL
//   TRAMP: aconst_null
//       goto H1                          ; arrives [null]
//   H1: pop                              ; drop exception-or-null
//   LOOP1: nop
//       <stack-neutral junk>
//       goto LOOP1                       ; dead self-loop at empty stack
//   [H2: pop; LOOP2: nop; junk; goto LOOP2]
//   REAL: <original body>
//
// The trampoline exists because emit_opaque_branch bundles computation with
// its branch: the dead edge arrives empty, so it cannot target the handler
// directly (handler frames carry the exception). Trampoline pushes the null
// that makes the goto->H1 edge assignable to [E1].
//
// Exception table (fake bcis < 0xF000 fit the u2 handler fields):
//   [P0, TRAMP) catch E1 -> H1,  [P0, H2) catch E2 -> H2 (if H2).
//
// Why a self-loop instead of rejoining REAL: HotSpot demands an explicit
// frame at any backward-branch target, including offset 0 (which javac
// never emits, so it is untested territory) — a self-contained dead loop
// needs only an auto-generated same_frame and can never execute.
//
// Safety properties (why behavior can't change):
// - The opaque branch is never taken; no athrow is ever executed.
// - Prelude + handlers contain only non-throwing straight-line code, so the
//   handlers are provably dead — any table shape is then semantics-neutral.
// - No locals are defined; all branch targets sit at zero stack, so the
//   auto-generated same_frame entries verify. The dead goto->H1 edge arrives
//   with [null], assignable to the handler's [E1] frame.
// - Original exception table entries are preserved (disjoint ranges).
void ControlFlowPass::inject_exception_dispatch(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx) {
    if (code.code.size() < 4 || code.code.size() >= 0xF000) return;
    auto instructions = bytecode::disassemble(code.code);
    if (instructions.empty()) return;

    bytecode::u4 orig_first_offset = instructions[0].offset;

    static const char* const kCatchTypes[] = {
        "java/lang/IllegalStateException",
        "java/lang/IllegalArgumentException",
        "java/lang/RuntimeException",
        "java/lang/SecurityException"
    };
    std::uniform_int_distribution<size_t> ex_dist(0, 3);
    const char* ex1 = kCatchTypes[ex_dist(ctx.rng)];
    const char* ex2 = kCatchTypes[ex_dist(ctx.rng)];
    if (ex2 == ex1) ex2 = kCatchTypes[(ex_dist(ctx.rng) + 1) % 4];
    bytecode::u2 catch1 = cp.find_or_add_class(ex1);
    bytecode::u2 catch2 = cp.find_or_add_class(ex2);

    // Fake bcis must fit u2 handler fields and avoid real offsets (< size).
    const uint32_t P0 = 0xF000;
    const uint32_t TRAMP = 0xF080;
    const uint32_t H1 = 0xF100;
    const uint32_t H2 = 0xF200;
    uint32_t bci = P0;

    std::vector<bytecode::Instruction> pre;
    auto add_i = [&](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = bci++;
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        pre.push_back(inst);
    };
    auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = bci++;
        inst.opcode = op;
        inst.operand = operand;
        pre.push_back(inst);
    };

    // P0: opaque predicate (dead branch to trampoline), then real flow.
    emit_opaque_branch(pre, cp, ctx, TRAMP, false, bci);
    add_op(bytecode::Opcode::goto_op,
           bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});
    // TRAMP: pad the stack so the hop into the handler verifies.
    bci = TRAMP;
    add_i(bytecode::Opcode::aconst_null);
    add_op(bytecode::Opcode::goto_op,
           bytecode::Branch16Operand{0, static_cast<bytecode::i4>(H1)});

    auto emit_junk = [&]() {
        std::uniform_int_distribution<int> op_dist(0, 2);
        std::uniform_int_distribution<int> v_dist(3, 60);
        int op = op_dist(ctx.rng);
        if (op == 0) {
            add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(v_dist(ctx.rng))});
            add_i(bytecode::Opcode::pop);
        } else if (op == 1) {
            add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(v_dist(ctx.rng))});
            add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(v_dist(ctx.rng))});
            add_i(bytecode::Opcode::iadd);
            add_i(bytecode::Opcode::pop);
        } else {
            add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(v_dist(ctx.rng))});
            add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(v_dist(ctx.rng))});
            add_i(bytecode::Opcode::imul);
            add_i(bytecode::Opcode::pop);
        }
    };

    // H1: drop exception, then a dead self-loop at empty stack.
    bci = H1;
    add_i(bytecode::Opcode::pop);
    uint32_t loop1_bci = bci;
    add_i(bytecode::Opcode::nop);
    emit_junk();
    add_op(bytecode::Opcode::goto_op,
           bytecode::Branch16Operand{0, static_cast<bytecode::i4>(loop1_bci)});

    // Optional H2 with an overlapping table range.
    bool second = (ctx.rng() & 1u) == 0;
    if (second) {
        bci = H2;
        add_i(bytecode::Opcode::pop);
        uint32_t loop2_bci = bci;
        add_i(bytecode::Opcode::nop);
        emit_junk();
        add_op(bytecode::Opcode::goto_op,
               bytecode::Branch16Operand{0, static_cast<bytecode::i4>(loop2_bci)});
    }

    bytecode::ExceptionHandler e1;
    e1.start_pc = static_cast<bytecode::u2>(P0);
    e1.end_pc = static_cast<bytecode::u2>(TRAMP); // prelude only; small fakes fit u2
    e1.handler_pc = static_cast<bytecode::u2>(H1);
    e1.catch_type = catch1;
    code.exception_table.push_back(e1);
    if (second) {
        bytecode::ExceptionHandler e2;
        e2.start_pc = static_cast<bytecode::u2>(P0);
        e2.end_pc = static_cast<bytecode::u2>(H2);
        e2.handler_pc = static_cast<bytecode::u2>(H2);
        e2.catch_type = catch2;
        code.exception_table.push_back(e2);
    }

    std::vector<bytecode::Instruction> new_instructions;
    new_instructions.reserve(pre.size() + instructions.size());
    for (auto& pi : pre) new_instructions.push_back(pi);
    for (auto& inst : instructions) new_instructions.push_back(std::move(inst));

    code.max_stack = std::max<bytecode::u2>(code.max_stack, 5);
    bytecode::update_code(code, new_instructions, cp);
    ctx.stats.methods_exception_dispatched++;
}

bool ControlFlowPass::flatten_control_flow(bytecode::MethodInfo& method, bytecode::ClassFile& cls, PassContext& ctx) {
    auto* code = method.code_mut();
    if (!code || !code->exception_table.empty()) return false;

    auto instructions = bytecode::disassemble(code->code);
    if (instructions.size() < 6 || instructions.size() > 150) return false;

    auto& cp = cls.constant_pool();

    // Verify straight-line code and identify zero-stack points
    int stack_depth = 0;
    std::vector<size_t> zero_points;

    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& inst = instructions[i];
        if (inst.opcode >= bytecode::Opcode::ifeq && inst.opcode <= bytecode::Opcode::jsr_w) {
            return false; // Contains branches - leave to decompiler traps
        }
        stack_depth += get_stack_delta(inst.opcode, inst.operand, cp);
        if (stack_depth < 0) return false;
        if (stack_depth == 0 && i + 1 < instructions.size()) {
            zero_points.push_back(i);
        }
    }

    if (stack_depth != 0 || zero_points.empty()) return false;

    // Pick 1 to 3 split points to partition into 2 to 4 basic blocks
    size_t num_splits = std::min<size_t>(3, zero_points.size());
    std::vector<size_t> chosen_splits;
    if (num_splits == 1) {
        chosen_splits.push_back(zero_points[zero_points.size() / 2]);
    } else {
        size_t step = zero_points.size() / num_splits;
        for (size_t s = 0; s < num_splits; ++s) {
            chosen_splits.push_back(zero_points[s * step]);
        }
    }
    std::sort(chosen_splits.begin(), chosen_splits.end());
    chosen_splits.erase(std::unique(chosen_splits.begin(), chosen_splits.end()), chosen_splits.end());
    if (chosen_splits.empty()) return false;

    struct Block {
        size_t id;
        int32_t state_id;
        std::vector<bytecode::Instruction> instrs;
        uint32_t entry_offset;
    };

    size_t num_blocks = chosen_splits.size() + 1;
    std::vector<Block> blocks(num_blocks);

    std::uniform_int_distribution<int32_t> state_dist(100000, 9999999);
    for (size_t b = 0; b < num_blocks; ++b) {
        blocks[b].id = b;
        blocks[b].state_id = state_dist(ctx.rng);
        blocks[b].entry_offset = static_cast<uint32_t>(0x72000000 + b * 0x1000);
    }

    size_t cur_inst_idx = 0;
    for (size_t b = 0; b < num_blocks; ++b) {
        size_t end_idx = (b < chosen_splits.size()) ? (chosen_splits[b] + 1) : instructions.size();
        for (size_t idx = cur_inst_idx; idx < end_idx; ++idx) {
            blocks[b].instrs.push_back(instructions[idx]);
        }
        cur_inst_idx = end_idx;
    }

    bytecode::u2 state_slot = code->max_locals;
    code->max_locals = state_slot + 1;

    uint32_t dispatcher_offset = 0x73000000;
    uint32_t trap_offset = 0x73000500;

    // Build the method instructions:
    std::vector<bytecode::Instruction> new_instructions;

    auto make_i = [](uint32_t off, bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = off;
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        return inst;
    };
    auto make_op = [](uint32_t off, bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = off;
        inst.opcode = op;
        inst.operand = operand;
        return inst;
    };

    // 1. Entry: Initialize state = S_0, goto DISPATCHER
    uint32_t entry_bci = 0x74000000;
    bytecode::u2 s0_cp = cp.find_or_add_integer(blocks[0].state_id);
    if (s0_cp <= 255) {
        new_instructions.push_back(make_op(entry_bci++, bytecode::Opcode::ldc, bytecode::CpIndexOperand{s0_cp}));
    } else {
        new_instructions.push_back(make_op(entry_bci++, bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{s0_cp}));
    }
    new_instructions.push_back(make_op(entry_bci++, bytecode::Opcode::istore, bytecode::LocalVarOperand{static_cast<bytecode::u1>(state_slot)}));
    new_instructions.push_back(make_op(entry_bci++, bytecode::Opcode::goto_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(dispatcher_offset)}));

    // 2. DISPATCHER: iload state; lookupswitch
    uint32_t disp_bci = dispatcher_offset;
    new_instructions.push_back(make_op(disp_bci++, bytecode::Opcode::iload, bytecode::LocalVarOperand{static_cast<bytecode::u1>(state_slot)}));

    bytecode::LookupSwitchData sw_data;
    sw_data.default_target_abs = static_cast<bytecode::i4>(trap_offset);
    for (size_t b = 0; b < num_blocks; ++b) {
        sw_data.pairs.push_back({blocks[b].state_id, static_cast<bytecode::i4>(blocks[b].entry_offset)});
    }
    std::sort(sw_data.pairs.begin(), sw_data.pairs.end(), [](const auto& a, const auto& b) {
        return a.match < b.match;
    });
    new_instructions.push_back(make_op(disp_bci++, bytecode::Opcode::lookupswitch, sw_data));

    // 3. Emit blocks in shuffled order (Chenxi Wang randomized layout)
    std::vector<size_t> block_order(num_blocks);
    for (size_t b = 0; b < num_blocks; ++b) block_order[b] = b;
    std::shuffle(block_order.begin(), block_order.end(), ctx.rng);

    for (size_t b_idx : block_order) {
        auto& blk = blocks[b_idx];
        uint32_t blk_bci = blk.entry_offset;

        // Block instructions
        for (size_t i = 0; i < blk.instrs.size(); ++i) {
            auto inst = blk.instrs[i];
            inst.offset = blk_bci++;
            new_instructions.push_back(inst);
        }

        // State transition for non-terminal blocks
        if (blk.id < num_blocks - 1) {
            int32_t cur_s = blk.state_id;
            int32_t next_s = blocks[blk.id + 1].state_id;

            // MBA state transition: state = state * M + C mod 2^32
            std::uniform_int_distribution<int32_t> m_dist(1001, 99999);
            int32_t M = m_dist(ctx.rng) | 1; // odd multiplier
            int32_t C = static_cast<int32_t>(static_cast<uint32_t>(next_s) - static_cast<uint32_t>(cur_s) * static_cast<uint32_t>(M));

            bytecode::u2 cp_m = cp.find_or_add_integer(M);
            bytecode::u2 cp_c = cp.find_or_add_integer(C);

            new_instructions.push_back(make_op(blk_bci++, bytecode::Opcode::iload, bytecode::LocalVarOperand{static_cast<bytecode::u1>(state_slot)}));
            if (cp_m <= 255) {
                new_instructions.push_back(make_op(blk_bci++, bytecode::Opcode::ldc, bytecode::CpIndexOperand{cp_m}));
            } else {
                new_instructions.push_back(make_op(blk_bci++, bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{cp_m}));
            }
            new_instructions.push_back(make_i(blk_bci++, bytecode::Opcode::imul));
            if (cp_c <= 255) {
                new_instructions.push_back(make_op(blk_bci++, bytecode::Opcode::ldc, bytecode::CpIndexOperand{cp_c}));
            } else {
                new_instructions.push_back(make_op(blk_bci++, bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{cp_c}));
            }
            new_instructions.push_back(make_i(blk_bci++, bytecode::Opcode::iadd));
            new_instructions.push_back(make_op(blk_bci++, bytecode::Opcode::istore, bytecode::LocalVarOperand{static_cast<bytecode::u1>(state_slot)}));
            new_instructions.push_back(make_op(blk_bci++, bytecode::Opcode::goto_op, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(dispatcher_offset)}));
        }
    }

    // 4. L_TRAP (default target for lookupswitch)
    new_instructions.push_back(make_i(trap_offset, bytecode::Opcode::aconst_null));
    new_instructions.push_back(make_i(trap_offset + 1, bytecode::Opcode::athrow));

    code->max_stack = std::max<bytecode::u2>(code->max_stack, 4);
    bytecode::update_code(*code, new_instructions, cp);
    ctx.stats.methods_flattened++;
    return true;
}

bool ControlFlowPass::run_on_class(bytecode::ClassFile& cls, PassContext& ctx) {
    if (!ctx.config.control_flow.enabled) return true;
    if (!ctx.is_class_in_scope(cls.name())) return true;
    return obfuscate_class(cls, ctx);
}

bool ControlFlowPass::obfuscate_class(bytecode::ClassFile& cls, PassContext& ctx, bool include_synthetic, bool skip_find_class) {
    ControlFlowPass pass;
    for (auto& method : cls.methods()) {
        if (method.is_abstract() || method.is_native()) continue;
        if (method.is_synthetic() && !include_synthetic) continue;
        auto mname = method.name(cls.constant_pool());
        if (mname == "<init>" || mname == "<clinit>") continue;
        if (skip_find_class && mname == "findClass") continue;
        auto* code = method.code_mut();
        if (!code || code->code.size() < 6) continue;

        std::uniform_int_distribution<int> dist(0, 100);
        int roll = dist(ctx.rng);

        if (roll < ctx.config.control_flow.flattening_intensity) {
            bool flattened = false;
            if (ctx.config.control_flow.flatten_blocks) {
                flattened = pass.flatten_control_flow(method, cls, ctx);
            }
            if (!flattened && ctx.config.control_flow.insert_opaque_predicates) {
                pass.insert_opaque_predicate(*code, cls.constant_pool(), ctx);
            }
        }

        if (ctx.config.control_flow.decompiler_traps) {
            pass.inject_decompiler_traps(*code, cls.constant_pool(), ctx);
        }

        // Exception dispatch runs last: it only prepends + appends table
        // entries, so flattened/trapped bodies compose untouched.
        if (ctx.config.control_flow.exception_dispatch) {
            std::uniform_int_distribution<int> edist(0, 100);
            if (edist(ctx.rng) < ctx.config.control_flow.flattening_intensity) {
                pass.inject_exception_dispatch(*code, cls.constant_pool(), ctx);
            }
        }
    }

    return true;
}

} // namespace obf::passes
