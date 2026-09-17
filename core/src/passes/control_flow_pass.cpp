#include "passes/control_flow_pass.hpp"
#include "bytecode/instructions.hpp"
#include <random>

namespace obf::passes {

void ControlFlowPass::insert_opaque_predicate(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx) {
    if (code.code.size() < 4) return; // Skip trivial methods
    auto instructions = bytecode::disassemble(code.code);
    if (instructions.empty()) return;

    bytecode::u4 orig_first_offset = instructions[0].offset;

    std::uniform_int_distribution<int> type_dist(0, 5);
    int template_type = type_dist(ctx.rng);

    std::vector<bytecode::Instruction> pred_instrs;

    auto add_i = [&](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = static_cast<bytecode::u4>(-1);
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        pred_instrs.push_back(inst);
    };

    auto add_op = [&](bytecode::Opcode op, bytecode::InstructionOperand operand) {
        bytecode::Instruction inst;
        inst.offset = static_cast<bytecode::u4>(-1);
        inst.opcode = op;
        inst.operand = operand;
        pred_instrs.push_back(inst);
    };

    if (template_type == 0) {
        // Template 1: Algebraic Bound Invariant (as requested)
        // int a = rand; int k = rand; if (a + a * k < large_bound)
        std::uniform_int_distribution<int> a_dist(8, 35);
        std::uniform_int_distribution<int> k_dist(2, 6);
        std::uniform_int_distribution<int> bound_dist(10000000, 99000000);

        int a = a_dist(ctx.rng);
        int k = k_dist(ctx.rng);
        int sum = a + (a * k);
        int bound = sum + bound_dist(ctx.rng);

        bytecode::u2 bound_cp = cp.find_or_add_integer(bound);
        bytecode::u2 runtime_ex = cp.find_or_add_class("java/lang/RuntimeException");
        bytecode::u2 ex_init = cp.find_or_add_methodref("java/lang/RuntimeException", "<init>", "()V");

        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(a)});
        add_i(bytecode::Opcode::dup);
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(k)});
        add_i(bytecode::Opcode::imul);
        add_i(bytecode::Opcode::iadd);
        if (bound_cp <= 255) {
            add_op(bytecode::Opcode::ldc, bytecode::CpIndexOperand{bound_cp});
        } else {
            add_op(bytecode::Opcode::ldc_w, bytecode::CpIndexOperand{bound_cp});
        }
        add_op(bytecode::Opcode::if_icmplt, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});

        // Deceptive dead branch (never reached)
        add_op(bytecode::Opcode::new_op, bytecode::CpIndexOperand{runtime_ex});
        add_i(bytecode::Opcode::dup);
        add_op(bytecode::Opcode::invokespecial, bytecode::CpIndexOperand{ex_init});
        add_i(bytecode::Opcode::athrow);

    } else if (template_type == 1) {
        // Template 2: Parity Invariant
        // for any n: (n * (n + 1)) & 1 == 0
        std::uniform_int_distribution<int> n_dist(7, 88);
        int n = n_dist(ctx.rng);

        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(n)});
        add_i(bytecode::Opcode::dup);
        add_i(bytecode::Opcode::iconst_1);
        add_i(bytecode::Opcode::iadd);
        add_i(bytecode::Opcode::imul);
        add_i(bytecode::Opcode::iconst_1);
        add_i(bytecode::Opcode::iand);
        add_op(bytecode::Opcode::ifeq, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});

        // Dead branch
        add_i(bytecode::Opcode::aconst_null);
        add_i(bytecode::Opcode::athrow);

    } else if (template_type == 2) {
        // Template 3: Bitwise Dominance Invariant
        // for any x, y: (x | y) >= (x & y)
        std::uniform_int_distribution<int> xy_dist(10, 120);
        int x = xy_dist(ctx.rng);
        int y = xy_dist(ctx.rng);

        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(x)});
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(y)});
        add_i(bytecode::Opcode::ior);
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(x)});
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(y)});
        add_i(bytecode::Opcode::iand);
        add_op(bytecode::Opcode::if_icmpge, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});

        // Dead branch
        add_i(bytecode::Opcode::aconst_null);
        add_i(bytecode::Opcode::athrow);

    } else if (template_type == 3) {
        // Template 4: Modular Congruence Invariant
        // (x * 13 + y) % 7 == r
        std::uniform_int_distribution<int> xy_dist(5, 50);
        int x = xy_dist(ctx.rng);
        int y = xy_dist(ctx.rng);
        int r = (x * 13 + y) % 7;

        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(x)});
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{13});
        add_i(bytecode::Opcode::imul);
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(y)});
        add_i(bytecode::Opcode::iadd);
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{7});
        add_i(bytecode::Opcode::irem);
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(r)});
        add_op(bytecode::Opcode::if_icmpeq, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});

        // Dead branch
        add_i(bytecode::Opcode::aconst_null);
        add_i(bytecode::Opcode::athrow);

    } else if (template_type == 4) {
        // Template 5: XOR Involution Invariant: (x ^ y) ^ y == x
        std::uniform_int_distribution<int> xy_dist(10, 120);
        int x = xy_dist(ctx.rng);
        int y = xy_dist(ctx.rng);

        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(x)});
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(y)});
        add_i(bytecode::Opcode::ixor);
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(y)});
        add_i(bytecode::Opcode::ixor);
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(x)});
        add_op(bytecode::Opcode::if_icmpeq, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});

        // Dead branch
        add_i(bytecode::Opcode::aconst_null);
        add_i(bytecode::Opcode::athrow);

    } else {
        // Template 6: Pythagorean Triple Invariant: a^2 + b^2 == c^2 where a=3k, b=4k, c=5k
        std::uniform_int_distribution<int> k_dist(1, 10);
        int k = k_dist(ctx.rng);
        int a = 3 * k;
        int b = 4 * k;
        int c = 5 * k;

        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(a)});
        add_i(bytecode::Opcode::dup);
        add_i(bytecode::Opcode::imul);
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(b)});
        add_i(bytecode::Opcode::dup);
        add_i(bytecode::Opcode::imul);
        add_i(bytecode::Opcode::iadd);
        add_op(bytecode::Opcode::bipush, bytecode::ByteOperand{static_cast<bytecode::i1>(c)});
        add_i(bytecode::Opcode::dup);
        add_i(bytecode::Opcode::imul);
        add_op(bytecode::Opcode::if_icmpeq, bytecode::Branch16Operand{0, static_cast<bytecode::i4>(orig_first_offset)});

        // Dead branch
        add_i(bytecode::Opcode::aconst_null);
        add_i(bytecode::Opcode::athrow);
    }

    std::vector<bytecode::Instruction> new_instructions;
    new_instructions.reserve(pred_instrs.size() + instructions.size());
    for (auto& pi : pred_instrs) {
        new_instructions.push_back(pi);
    }
    for (auto& inst : instructions) {
        new_instructions.push_back(std::move(inst));
    }

    code.max_stack = std::max<bytecode::u2>(code.max_stack, 4);
    bytecode::update_code(code, new_instructions, cp);
    ctx.stats.methods_flattened++;
}

bool ControlFlowPass::run_on_class(bytecode::ClassFile& cls, PassContext& ctx) {
    if (!ctx.config.control_flow.enabled) return true;
    if (!ctx.is_class_in_scope(cls.name())) return true;

    for (auto& method : cls.methods()) {
        if (method.is_abstract() || method.is_native() || method.is_synthetic()) continue;
        auto mname = method.name(cls.constant_pool());
        if (mname == "<init>" || mname == "<clinit>") continue;
        if (auto* code = method.code_mut()) {
            std::uniform_int_distribution<int> dist(0, 100);
            if (dist(ctx.rng) < ctx.config.control_flow.flattening_intensity) {
                if (ctx.config.control_flow.insert_opaque_predicates) {
                    insert_opaque_predicate(*code, cls.constant_pool(), ctx);
                }
            }
        }
    }

    return true;
}

} // namespace obf::passes
