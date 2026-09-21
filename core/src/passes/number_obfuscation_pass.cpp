#include "passes/number_obfuscation_pass.hpp"
#include "bytecode/instructions.hpp"
#include <random>
#include <cstring>
#include <limits>

namespace obf::passes {

static void push_int_raw(std::vector<bytecode::Instruction>& instrs, int32_t val, bytecode::ConstantPool& cp, bytecode::u4 offset) {
    bytecode::Instruction inst;
    inst.offset = offset;
    if (val >= -1 && val <= 5) {
        switch (val) {
            case -1: inst.opcode = bytecode::Opcode::iconst_m1; break;
            case 0:  inst.opcode = bytecode::Opcode::iconst_0; break;
            case 1:  inst.opcode = bytecode::Opcode::iconst_1; break;
            case 2:  inst.opcode = bytecode::Opcode::iconst_2; break;
            case 3:  inst.opcode = bytecode::Opcode::iconst_3; break;
            case 4:  inst.opcode = bytecode::Opcode::iconst_4; break;
            case 5:  inst.opcode = bytecode::Opcode::iconst_5; break;
        }
        inst.operand = bytecode::NoOperand{};
    } else if (val >= -128 && val <= 127) {
        inst.opcode = bytecode::Opcode::bipush;
        inst.operand = bytecode::ByteOperand{static_cast<bytecode::i1>(val)};
    } else if (val >= -32768 && val <= 32767) {
        inst.opcode = bytecode::Opcode::sipush;
        inst.operand = bytecode::ShortOperand{static_cast<bytecode::i2>(val)};
    } else {
        bytecode::u2 idx = cp.find_or_add_integer(val);
        if (idx <= 255) {
            inst.opcode = bytecode::Opcode::ldc;
            inst.operand = bytecode::CpIndexOperand{idx};
        } else {
            inst.opcode = bytecode::Opcode::ldc_w;
            inst.operand = bytecode::CpIndexOperand{idx};
        }
    }
    instrs.push_back(inst);
}

static void push_long_raw(std::vector<bytecode::Instruction>& instrs, int64_t val, bytecode::ConstantPool& cp, bytecode::u4 offset) {
    bytecode::Instruction inst;
    inst.offset = offset;
    if (val == 0) {
        inst.opcode = bytecode::Opcode::lconst_0;
        inst.operand = bytecode::NoOperand{};
        instrs.push_back(inst);
    } else if (val == 1) {
        inst.opcode = bytecode::Opcode::lconst_1;
        inst.operand = bytecode::NoOperand{};
        instrs.push_back(inst);
    } else if (val >= -128 && val <= 127) {
        push_int_raw(instrs, static_cast<int32_t>(val), cp, offset);
        bytecode::Instruction i2l_inst;
        i2l_inst.offset = static_cast<bytecode::u4>(-1);
        i2l_inst.opcode = bytecode::Opcode::i2l;
        i2l_inst.operand = bytecode::NoOperand{};
        instrs.push_back(i2l_inst);
    } else if (val >= -32768 && val <= 32767) {
        push_int_raw(instrs, static_cast<int32_t>(val), cp, offset);
        bytecode::Instruction i2l_inst;
        i2l_inst.offset = static_cast<bytecode::u4>(-1);
        i2l_inst.opcode = bytecode::Opcode::i2l;
        i2l_inst.operand = bytecode::NoOperand{};
        instrs.push_back(i2l_inst);
    } else {
        bytecode::u2 idx = cp.find_or_add_long(val);
        inst.opcode = bytecode::Opcode::ldc2_w;
        inst.operand = bytecode::CpIndexOperand{idx};
        instrs.push_back(inst);
    }
}

static void emit_obfuscated_int(std::vector<bytecode::Instruction>& instrs, int32_t val, bytecode::ConstantPool& cp, bytecode::u4 orig_offset, std::mt19937_64& rng) {
    int strategy = std::uniform_int_distribution<int>(0, 4)(rng);
    auto make_op = [](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = static_cast<bytecode::u4>(-1);
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        return inst;
    };

    if (strategy == 0) {
        // Strategy 0: XOR split (val ^ k) ^ k
        int32_t k = static_cast<int32_t>(rng() & 0x7FFFFFFF);
        int32_t part1 = val ^ k;
        push_int_raw(instrs, part1, cp, orig_offset);
        push_int_raw(instrs, k, cp, static_cast<bytecode::u4>(-1));
        instrs.push_back(make_op(bytecode::Opcode::ixor));
    } else if (strategy == 1) {
        // Strategy 1: Add/Sub split (val + k) - k
        int32_t k = static_cast<int32_t>(rng() & 0x00FFFFFF) + 1;
        int32_t part1 = val + k;
        push_int_raw(instrs, part1, cp, orig_offset);
        push_int_raw(instrs, k, cp, static_cast<bytecode::u4>(-1));
        instrs.push_back(make_op(bytecode::Opcode::isub));
    } else if (strategy == 2) {
        // Strategy 2: Sub/Add split (val - k) + k
        int32_t k = static_cast<int32_t>(rng() & 0x00FFFFFF) + 1;
        int32_t part1 = val - k;
        push_int_raw(instrs, part1, cp, orig_offset);
        push_int_raw(instrs, k, cp, static_cast<bytecode::u4>(-1));
        instrs.push_back(make_op(bytecode::Opcode::iadd));
    } else if (strategy == 3 && val != std::numeric_limits<int32_t>::min()) {
        // Strategy 3: Negation -(-val)
        push_int_raw(instrs, -val, cp, orig_offset);
        instrs.push_back(make_op(bytecode::Opcode::ineg));
    } else {
        // Strategy 4: 3-way XOR chain ((val ^ k1) ^ k2) ^ (k1 ^ k2)
        int32_t k1 = static_cast<int32_t>(rng() & 0x0000FFFF) + 1;
        int32_t k2 = static_cast<int32_t>(rng() & 0x0000FFFF) + 1;
        int32_t part1 = val ^ k1 ^ k2;
        int32_t part2 = k1 ^ k2;
        push_int_raw(instrs, part1, cp, orig_offset);
        push_int_raw(instrs, part2, cp, static_cast<bytecode::u4>(-1));
        instrs.push_back(make_op(bytecode::Opcode::ixor));
    }
}

static void emit_obfuscated_long(std::vector<bytecode::Instruction>& instrs, int64_t val, bytecode::ConstantPool& cp, bytecode::u4 orig_offset, std::mt19937_64& rng) {
    int strategy = std::uniform_int_distribution<int>(0, 3)(rng);
    auto make_op = [](bytecode::Opcode op) {
        bytecode::Instruction inst;
        inst.offset = static_cast<bytecode::u4>(-1);
        inst.opcode = op;
        inst.operand = bytecode::NoOperand{};
        return inst;
    };

    if (strategy == 0) {
        // Strategy 0: XOR split (val ^ k) ^ k
        int64_t k = static_cast<int64_t>(rng() & 0x7FFFFFFFFFFFFFFFULL);
        int64_t part1 = val ^ k;
        push_long_raw(instrs, part1, cp, orig_offset);
        push_long_raw(instrs, k, cp, static_cast<bytecode::u4>(-1));
        instrs.push_back(make_op(bytecode::Opcode::lxor));
    } else if (strategy == 1) {
        // Strategy 1: Add/Sub split (val + k) - k
        int64_t k = static_cast<int64_t>(rng() & 0x0000FFFFFFFFFFFFULL) + 1;
        int64_t part1 = val + k;
        push_long_raw(instrs, part1, cp, orig_offset);
        push_long_raw(instrs, k, cp, static_cast<bytecode::u4>(-1));
        instrs.push_back(make_op(bytecode::Opcode::lsub));
    } else if (strategy == 2) {
        // Strategy 2: Sub/Add split (val - k) + k
        int64_t k = static_cast<int64_t>(rng() & 0x0000FFFFFFFFFFFFULL) + 1;
        int64_t part1 = val - k;
        push_long_raw(instrs, part1, cp, orig_offset);
        push_long_raw(instrs, k, cp, static_cast<bytecode::u4>(-1));
        instrs.push_back(make_op(bytecode::Opcode::ladd));
    } else if (strategy == 3 && val != std::numeric_limits<int64_t>::min()) {
        // Strategy 3: Negation -(-val)
        push_long_raw(instrs, -val, cp, orig_offset);
        instrs.push_back(make_op(bytecode::Opcode::lneg));
    } else {
        int64_t k = static_cast<int64_t>(rng() & 0x7FFFFFFFFFFFFFFFULL);
        push_long_raw(instrs, val ^ k, cp, orig_offset);
        push_long_raw(instrs, k, cp, static_cast<bytecode::u4>(-1));
        instrs.push_back(make_op(bytecode::Opcode::lxor));
    }
}

static void emit_obfuscated_float(std::vector<bytecode::Instruction>& instrs, float val, bytecode::ConstantPool& cp, bytecode::u4 orig_offset, std::mt19937_64& rng) {
    uint32_t bits = 0;
    std::memcpy(&bits, &val, sizeof(bits));
    emit_obfuscated_int(instrs, static_cast<int32_t>(bits), cp, orig_offset, rng);

    bytecode::u2 mref = cp.find_or_add_methodref("java/lang/Float", "intBitsToFloat", "(I)F");
    bytecode::Instruction call_inst;
    call_inst.offset = static_cast<bytecode::u4>(-1);
    call_inst.opcode = bytecode::Opcode::invokestatic;
    call_inst.operand = bytecode::CpIndexOperand{mref};
    instrs.push_back(call_inst);
}

static void emit_obfuscated_double(std::vector<bytecode::Instruction>& instrs, double val, bytecode::ConstantPool& cp, bytecode::u4 orig_offset, std::mt19937_64& rng) {
    uint64_t bits = 0;
    std::memcpy(&bits, &val, sizeof(bits));
    emit_obfuscated_long(instrs, static_cast<int64_t>(bits), cp, orig_offset, rng);

    bytecode::u2 mref = cp.find_or_add_methodref("java/lang/Double", "longBitsToDouble", "(J)D");
    bytecode::Instruction call_inst;
    call_inst.offset = static_cast<bytecode::u4>(-1);
    call_inst.opcode = bytecode::Opcode::invokestatic;
    call_inst.operand = bytecode::CpIndexOperand{mref};
    instrs.push_back(call_inst);
}

void NumberObfuscationPass::transform_numbers_in_code(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx) {
    auto instructions = bytecode::disassemble(code.code);
    std::vector<bytecode::Instruction> new_instructions;
    new_instructions.reserve(instructions.size() * 3);

    bool modified = false;
    std::uniform_int_distribution<int> pct_dist(0, 100);

    for (const auto& instr : instructions) {
        bool is_int = false;
        int32_t int_val = 0;

        bool is_long = false;
        int64_t long_val = 0;

        bool is_float = false;
        float float_val = 0.0f;

        bool is_double = false;
        double double_val = 0.0;

        if (instr.opcode == bytecode::Opcode::iconst_m1) { int_val = -1; is_int = true; }
        else if (instr.opcode == bytecode::Opcode::iconst_0) { int_val = 0; is_int = true; }
        else if (instr.opcode == bytecode::Opcode::iconst_1) { int_val = 1; is_int = true; }
        else if (instr.opcode == bytecode::Opcode::iconst_2) { int_val = 2; is_int = true; }
        else if (instr.opcode == bytecode::Opcode::iconst_3) { int_val = 3; is_int = true; }
        else if (instr.opcode == bytecode::Opcode::iconst_4) { int_val = 4; is_int = true; }
        else if (instr.opcode == bytecode::Opcode::iconst_5) { int_val = 5; is_int = true; }
        else if (instr.opcode == bytecode::Opcode::bipush) {
            if (const auto* b = std::get_if<bytecode::ByteOperand>(&instr.operand)) {
                int_val = b->value;
                is_int = true;
            }
        } else if (instr.opcode == bytecode::Opcode::sipush) {
            if (const auto* s = std::get_if<bytecode::ShortOperand>(&instr.operand)) {
                int_val = s->value;
                is_int = true;
            }
        } else if (instr.opcode == bytecode::Opcode::ldc || instr.opcode == bytecode::Opcode::ldc_w) {
            bytecode::u2 cp_idx = 0;
            if (const auto* op = std::get_if<bytecode::CpIndexOperand>(&instr.operand)) {
                cp_idx = op->index;
            }
            if (cp_idx != 0) {
                if (const auto* ci = cp.get_if<bytecode::CpInteger>(cp_idx)) {
                    int_val = ci->value;
                    is_int = true;
                } else if (const auto* cf = cp.get_if<bytecode::CpFloat>(cp_idx)) {
                    float_val = cf->value;
                    is_float = true;
                }
            }
        } else if (instr.opcode == bytecode::Opcode::lconst_0) {
            long_val = 0;
            is_long = true;
        } else if (instr.opcode == bytecode::Opcode::lconst_1) {
            long_val = 1;
            is_long = true;
        } else if (instr.opcode == bytecode::Opcode::fconst_0) {
            float_val = 0.0f;
            is_float = true;
        } else if (instr.opcode == bytecode::Opcode::fconst_1) {
            float_val = 1.0f;
            is_float = true;
        } else if (instr.opcode == bytecode::Opcode::fconst_2) {
            float_val = 2.0f;
            is_float = true;
        } else if (instr.opcode == bytecode::Opcode::dconst_0) {
            double_val = 0.0;
            is_double = true;
        } else if (instr.opcode == bytecode::Opcode::dconst_1) {
            double_val = 1.0;
            is_double = true;
        } else if (instr.opcode == bytecode::Opcode::ldc2_w) {
            bytecode::u2 cp_idx = 0;
            if (const auto* op = std::get_if<bytecode::CpIndexOperand>(&instr.operand)) {
                cp_idx = op->index;
            }
            if (cp_idx != 0) {
                if (const auto* cl = cp.get_if<bytecode::CpLong>(cp_idx)) {
                    long_val = cl->value;
                    is_long = true;
                } else if (const auto* cd = cp.get_if<bytecode::CpDouble>(cp_idx)) {
                    double_val = cd->value;
                    is_double = true;
                }
            }
        }

        bool is_any_num = is_int || is_long || is_float || is_double;

        if (is_any_num && pct_dist(ctx.rng) < ctx.config.number_obfuscation.intensity) {
            if (is_int) {
                emit_obfuscated_int(new_instructions, int_val, cp, instr.offset, ctx.rng);
            } else if (is_long) {
                emit_obfuscated_long(new_instructions, long_val, cp, instr.offset, ctx.rng);
            } else if (is_float) {
                emit_obfuscated_float(new_instructions, float_val, cp, instr.offset, ctx.rng);
            } else if (is_double) {
                emit_obfuscated_double(new_instructions, double_val, cp, instr.offset, ctx.rng);
            }
            ctx.stats.numbers_obfuscated++;
            modified = true;
        } else {
            new_instructions.push_back(instr);
        }
    }

    if (modified) {
        code.max_stack = std::max<bytecode::u2>(code.max_stack, code.max_stack + 4);
        bytecode::update_code(code, new_instructions, cp);
    }
}

bool NumberObfuscationPass::run_on_class(bytecode::ClassFile& cls, PassContext& ctx) {
    if (!ctx.config.number_obfuscation.enabled) return true;
    if (!ctx.is_class_in_scope(cls.name())) return true;
    return obfuscate_class(cls, ctx);
}

bool NumberObfuscationPass::obfuscate_class(bytecode::ClassFile& cls, PassContext& ctx) {
    NumberObfuscationPass pass;
    for (auto& method : cls.methods()) {
        if (auto* code = method.code_mut()) {
            pass.transform_numbers_in_code(*code, cls.constant_pool(), ctx);
        }
    }
    return true;
}

} // namespace obf::passes
