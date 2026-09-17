#include "bytecode/instructions.hpp"
#include "bytecode/attributes.hpp"
#include "bytecode/constant_pool.hpp"
#include <cstring>
#include <stdexcept>
#include <map>
#include <functional>
#include <algorithm>

namespace obf::bytecode {

bool Instruction::is_branch() const noexcept {
    switch (opcode) {
        case Opcode::ifeq:
        case Opcode::ifne:
        case Opcode::iflt:
        case Opcode::ifge:
        case Opcode::ifgt:
        case Opcode::ifle:
        case Opcode::if_icmpeq:
        case Opcode::if_icmpne:
        case Opcode::if_icmplt:
        case Opcode::if_icmpge:
        case Opcode::if_icmpgt:
        case Opcode::if_icmple:
        case Opcode::if_acmpeq:
        case Opcode::if_acmpne:
        case Opcode::goto_op:
        case Opcode::jsr:
        case Opcode::ifnull:
        case Opcode::ifnonnull:
        case Opcode::goto_w:
        case Opcode::jsr_w:
        case Opcode::tableswitch:
        case Opcode::lookupswitch:
            return true;
        default:
            return false;
    }
}

bool Instruction::is_return() const noexcept {
    switch (opcode) {
        case Opcode::ireturn:
        case Opcode::lreturn:
        case Opcode::freturn:
        case Opcode::dreturn:
        case Opcode::areturn:
        case Opcode::return_op:
        case Opcode::athrow:
            return true;
        default:
            return false;
    }
}

bool Instruction::is_load() const noexcept {
    if (opcode == Opcode::wide) {
        if (const auto* w = std::get_if<WideOperand>(&operand)) {
            u1 mop = static_cast<u1>(w->modified_opcode);
            return (mop >= static_cast<u1>(Opcode::iload) && mop <= static_cast<u1>(Opcode::aload));
        }
    }
    u1 op = static_cast<u1>(opcode);
    return (op >= static_cast<u1>(Opcode::iload) && op <= static_cast<u1>(Opcode::aload_3)) ||
           (op >= static_cast<u1>(Opcode::iaload) && op <= static_cast<u1>(Opcode::saload));
}

bool Instruction::is_store() const noexcept {
    if (opcode == Opcode::wide) {
        if (const auto* w = std::get_if<WideOperand>(&operand)) {
            u1 mop = static_cast<u1>(w->modified_opcode);
            return (mop >= static_cast<u1>(Opcode::istore) && mop <= static_cast<u1>(Opcode::astore));
        }
    }
    u1 op = static_cast<u1>(opcode);
    return (op >= static_cast<u1>(Opcode::istore) && op <= static_cast<u1>(Opcode::astore_3)) ||
           (op >= static_cast<u1>(Opcode::iastore) && op <= static_cast<u1>(Opcode::sastore));
}

bool Instruction::is_invoke() const noexcept {
    switch (opcode) {
        case Opcode::invokevirtual:
        case Opcode::invokespecial:
        case Opcode::invokestatic:
        case Opcode::invokeinterface:
        case Opcode::invokedynamic:
            return true;
        default:
            return false;
    }
}

std::string_view Instruction::name() const noexcept {
    switch (opcode) {
        case Opcode::nop: return "nop";
        case Opcode::aconst_null: return "aconst_null";
        case Opcode::iconst_m1: return "iconst_m1";
        case Opcode::iconst_0: return "iconst_0";
        case Opcode::iconst_1: return "iconst_1";
        case Opcode::iconst_2: return "iconst_2";
        case Opcode::iconst_3: return "iconst_3";
        case Opcode::iconst_4: return "iconst_4";
        case Opcode::iconst_5: return "iconst_5";
        case Opcode::bipush: return "bipush";
        case Opcode::sipush: return "sipush";
        case Opcode::ldc: return "ldc";
        case Opcode::ldc_w: return "ldc_w";
        case Opcode::ldc2_w: return "ldc2_w";
        case Opcode::iload: return "iload";
        case Opcode::aload: return "aload";
        case Opcode::istore: return "istore";
        case Opcode::astore: return "astore";
        case Opcode::pop: return "pop";
        case Opcode::dup: return "dup";
        case Opcode::iadd: return "iadd";
        case Opcode::isub: return "isub";
        case Opcode::imul: return "imul";
        case Opcode::ixor: return "ixor";
        case Opcode::ifeq: return "ifeq";
        case Opcode::ifne: return "ifne";
        case Opcode::goto_op: return "goto";
        case Opcode::ireturn: return "ireturn";
        case Opcode::return_op: return "return";
        case Opcode::getstatic: return "getstatic";
        case Opcode::putstatic: return "putstatic";
        case Opcode::getfield: return "getfield";
        case Opcode::putfield: return "putfield";
        case Opcode::invokevirtual: return "invokevirtual";
        case Opcode::invokespecial: return "invokespecial";
        case Opcode::invokestatic: return "invokestatic";
        case Opcode::invokeinterface: return "invokeinterface";
        case Opcode::invokedynamic: return "invokedynamic";
        case Opcode::new_op: return "new";
        case Opcode::athrow: return "athrow";
        case Opcode::wide: return "wide";
        default: return "opcode";
    }
}

std::vector<Instruction> disassemble(std::span<const u1> code_bytes) {
    BinaryReader reader(code_bytes);
    std::vector<Instruction> result;

    while (reader.remaining() > 0) {
        Instruction instr;
        instr.offset = static_cast<u4>(reader.offset());
        instr.opcode = static_cast<Opcode>(reader.read_u1());

        switch (instr.opcode) {
            case Opcode::bipush:
            case Opcode::newarray:
                instr.operand = ByteOperand{reader.read_i1()};
                break;

            case Opcode::sipush:
                instr.operand = ShortOperand{reader.read_i2()};
                break;

            case Opcode::ldc:
                instr.operand = CpIndexOperand{static_cast<u2>(reader.read_u1())};
                break;

            case Opcode::ldc_w:
            case Opcode::ldc2_w:
            case Opcode::getstatic:
            case Opcode::putstatic:
            case Opcode::getfield:
            case Opcode::putfield:
            case Opcode::invokevirtual:
            case Opcode::invokespecial:
            case Opcode::invokestatic:
            case Opcode::new_op:
            case Opcode::anewarray:
            case Opcode::checkcast:
            case Opcode::instanceof_op:
                instr.operand = CpIndexOperand{reader.read_u2()};
                break;

            case Opcode::iload:
            case Opcode::lload:
            case Opcode::fload:
            case Opcode::dload:
            case Opcode::aload:
            case Opcode::istore:
            case Opcode::lstore:
            case Opcode::fstore:
            case Opcode::dstore:
            case Opcode::astore:
            case Opcode::ret:
                instr.operand = LocalVarOperand{reader.read_u1()};
                break;

            case Opcode::iinc: {
                u1 idx = reader.read_u1();
                i1 val = reader.read_i1();
                instr.operand = IincOperand{idx, val};
                break;
            }

            case Opcode::ifeq:
            case Opcode::ifne:
            case Opcode::iflt:
            case Opcode::ifge:
            case Opcode::ifgt:
            case Opcode::ifle:
            case Opcode::if_icmpeq:
            case Opcode::if_icmpne:
            case Opcode::if_icmplt:
            case Opcode::if_icmpge:
            case Opcode::if_icmpgt:
            case Opcode::if_icmple:
            case Opcode::if_acmpeq:
            case Opcode::if_acmpne:
            case Opcode::goto_op:
            case Opcode::jsr:
            case Opcode::ifnull:
            case Opcode::ifnonnull: {
                i2 rel = reader.read_i2();
                instr.operand = Branch16Operand{rel, static_cast<i4>(instr.offset) + rel};
                break;
            }

            case Opcode::goto_w:
            case Opcode::jsr_w: {
                i4 rel = reader.read_i4();
                instr.operand = Branch32Operand{rel, static_cast<i4>(instr.offset) + rel};
                break;
            }

            case Opcode::invokeinterface: {
                u2 index = reader.read_u2();
                u1 count = reader.read_u1();
                (void)reader.read_u1(); // zero byte
                instr.operand = InterfaceCallOperand{index, count};
                break;
            }

            case Opcode::invokedynamic: {
                u2 index = reader.read_u2();
                (void)reader.read_u2(); // two zero bytes
                instr.operand = InvokeDynamicOperand{index};
                break;
            }

            case Opcode::tableswitch: {
                // Pad to 4-byte boundary relative to start of instruction
                size_t pad = (4 - (instr.offset + 1) % 4) % 4;
                reader.skip(pad);

                TableSwitchData tsd;
                tsd.default_offset = reader.read_i4();
                tsd.default_target_abs = static_cast<i4>(instr.offset) + tsd.default_offset;
                tsd.low = reader.read_i4();
                tsd.high = reader.read_i4();
                i4 num_jumps = tsd.high - tsd.low + 1;
                tsd.jump_offsets.reserve(num_jumps);
                tsd.jump_targets_abs.reserve(num_jumps);
                for (i4 j = 0; j < num_jumps; ++j) {
                    i4 off = reader.read_i4();
                    tsd.jump_offsets.push_back(off);
                    tsd.jump_targets_abs.push_back(static_cast<i4>(instr.offset) + off);
                }
                instr.operand = tsd;
                break;
            }

            case Opcode::lookupswitch: {
                size_t pad = (4 - (instr.offset + 1) % 4) % 4;
                reader.skip(pad);

                LookupSwitchData lsd;
                lsd.default_offset = reader.read_i4();
                lsd.default_target_abs = static_cast<i4>(instr.offset) + lsd.default_offset;
                i4 npairs = reader.read_i4();
                lsd.pairs.reserve(npairs);
                for (i4 p = 0; p < npairs; ++p) {
                    i4 match = reader.read_i4();
                    i4 off = reader.read_i4();
                    lsd.pairs.push_back({match, off, static_cast<i4>(instr.offset) + off});
                }
                instr.operand = lsd;
                break;
            }

            case Opcode::multianewarray: {
                u2 index = reader.read_u2();
                u1 dimensions = reader.read_u1();
                instr.operand = MultiANewArrayOperand{index, dimensions};
                break;
            }

            case Opcode::wide: {
                Opcode mod_op = static_cast<Opcode>(reader.read_u1());
                u2 index = reader.read_u2();
                if (mod_op == Opcode::iinc) {
                    i2 const_val = reader.read_i2();
                    instr.operand = WideOperand{mod_op, index, const_val};
                } else {
                    instr.operand = WideOperand{mod_op, index, 0};
                }
                break;
            }

            default:
                instr.operand = NoOperand{};
                break;
        }

        result.push_back(instr);
    }

    return result;
}

static size_t get_instruction_size(const Instruction& instr, size_t current_offset) {
    size_t size = 1; // opcode
    std::visit([&](const auto& op) {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, ByteOperand> || std::is_same_v<T, LocalVarOperand>) {
            size += 1;
        } else if constexpr (std::is_same_v<T, ShortOperand> || std::is_same_v<T, Branch16Operand> || std::is_same_v<T, CpIndexWideOperand> || std::is_same_v<T, IincOperand>) {
            size += 2;
        } else if constexpr (std::is_same_v<T, IntOperand> || std::is_same_v<T, Branch32Operand>) {
            size += 4;
        } else if constexpr (std::is_same_v<T, CpIndexOperand>) {
            size += (instr.opcode == Opcode::ldc) ? 1 : 2;
        } else if constexpr (std::is_same_v<T, MultiANewArrayOperand>) {
            size += 3;
        } else if constexpr (std::is_same_v<T, WideOperand>) {
            size += (op.modified_opcode == Opcode::iinc) ? 5 : 3;
        } else if constexpr (std::is_same_v<T, InterfaceCallOperand> || std::is_same_v<T, InvokeDynamicOperand>) {
            size += 4;
        } else if constexpr (std::is_same_v<T, TableSwitchData>) {
            size_t pad = (4 - (current_offset + 1) % 4) % 4;
            size += pad + 12 + 4 * op.jump_offsets.size();
        } else if constexpr (std::is_same_v<T, LookupSwitchData>) {
            size_t pad = (4 - (current_offset + 1) % 4) % 4;
            size += pad + 8 + 8 * op.pairs.size();
        } else if constexpr (std::is_same_v<T, NoOperand>) {
            // 0 extra bytes
        }
    }, instr.operand);
    return size;
}

std::vector<u1> assemble(const std::vector<Instruction>& instructions) {
    BinaryWriter writer;

    for (const auto& instr : instructions) {
        size_t instr_offset = writer.size();
        writer.write_u1(static_cast<u1>(instr.opcode));

        std::visit([&](const auto& op) {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, ByteOperand>) {
                writer.write_i1(op.value);
            } else if constexpr (std::is_same_v<T, ShortOperand>) {
                writer.write_i2(op.value);
            } else if constexpr (std::is_same_v<T, IntOperand>) {
                writer.write_i4(op.value);
            } else if constexpr (std::is_same_v<T, LocalVarOperand>) {
                writer.write_u1(op.index);
            } else if constexpr (std::is_same_v<T, CpIndexOperand>) {
                if (instr.opcode == Opcode::ldc) {
                    writer.write_u1(static_cast<u1>(op.index));
                } else {
                    writer.write_u2(op.index);
                }
            } else if constexpr (std::is_same_v<T, CpIndexWideOperand>) {
                writer.write_u2(op.index);
            } else if constexpr (std::is_same_v<T, MultiANewArrayOperand>) {
                writer.write_u2(op.index);
                writer.write_u1(op.dimensions);
            } else if constexpr (std::is_same_v<T, Branch16Operand>) {
                writer.write_i2(op.target_offset);
            } else if constexpr (std::is_same_v<T, Branch32Operand>) {
                writer.write_i4(op.target_offset);
            } else if constexpr (std::is_same_v<T, IincOperand>) {
                writer.write_u1(op.index);
                writer.write_i1(op.const_val);
            } else if constexpr (std::is_same_v<T, WideOperand>) {
                writer.write_u1(static_cast<u1>(op.modified_opcode));
                writer.write_u2(op.index);
                if (op.modified_opcode == Opcode::iinc) {
                    writer.write_i2(op.const_val);
                }
            } else if constexpr (std::is_same_v<T, InterfaceCallOperand>) {
                writer.write_u2(op.index);
                writer.write_u1(op.count);
                writer.write_u1(0);
            } else if constexpr (std::is_same_v<T, InvokeDynamicOperand>) {
                writer.write_u2(op.index);
                writer.write_u2(0);
            } else if constexpr (std::is_same_v<T, TableSwitchData>) {
                size_t pad = (4 - (instr_offset + 1) % 4) % 4;
                for (size_t p = 0; p < pad; ++p) writer.write_u1(0);
                writer.write_i4(op.default_offset);
                writer.write_i4(op.low);
                writer.write_i4(op.high);
                for (i4 off : op.jump_offsets) {
                    writer.write_i4(off);
                }
            } else if constexpr (std::is_same_v<T, LookupSwitchData>) {
                size_t pad = (4 - (instr_offset + 1) % 4) % 4;
                for (size_t p = 0; p < pad; ++p) writer.write_u1(0);
                writer.write_i4(op.default_offset);
                writer.write_i4(static_cast<i4>(op.pairs.size()));
                for (const auto& pair : op.pairs) {
                    writer.write_i4(pair.match);
                    writer.write_i4(pair.offset);
                }
            } else if constexpr (std::is_same_v<T, NoOperand>) {
                // No extra bytes
            }
        }, instr.operand);
    }

    return std::move(writer.data());
}

static void recalculate_stack_map_table(AttrCode& code, ConstantPool& cp, const std::vector<Instruction>& new_instructions, const std::vector<u4>& /*new_offsets*/, const std::function<u4(i4)>& lookup_target, u4 new_total_size, const std::function<u2(u2)>& remap_cp_class = nullptr) {
    struct FrameRecord {
        u1 frame_type{0};
        u4 orig_bci{0};
        u4 new_bci{0};
        std::vector<u1> payload;
    };

    std::vector<FrameRecord> frames;
    Attribute* smt_attr = nullptr;

    for (auto& attr : code.attributes) {
        if (attr.name(cp) == "StackMapTable") {
            smt_attr = &attr;
            break;
        }
    }

    auto read_verification_type_info = [&](BinaryReader& r, BinaryWriter& w) {
        if (r.remaining() < 1) return;
        u1 tag = r.read_u1();
        w.write_u1(tag);
        if (tag == 7) { // Object_variable_info
            u2 cp_idx = r.read_u2();
            if (remap_cp_class) {
                cp_idx = remap_cp_class(cp_idx);
            }
            w.write_u2(cp_idx);
        } else if (tag == 8) { // Uninitialized_variable_info
            u2 off = r.read_u2();
            u2 new_off = static_cast<u2>(lookup_target(static_cast<i4>(off)));
            w.write_u2(new_off);
        }
    };

    if (smt_attr) {
        if (auto* raw = smt_attr->get_mut_if<AttrRaw>()) {
            if (!raw->data.empty()) {
                BinaryReader reader(raw->data);
                if (reader.remaining() >= 2) {
                    u2 num_entries = reader.read_u2();
                    frames.reserve(num_entries + 4);

                    u4 prev_orig_bci = 0;
                    bool has_prev = false;

                    for (u2 i = 0; i < num_entries; ++i) {
                        if (reader.remaining() < 1) break;
                        u1 tag = reader.read_u1();
                        FrameRecord rec;
                        rec.frame_type = tag;

                        u4 offset_delta = 0;
                        BinaryWriter payload_writer;

                        if (tag <= 63) { // same_frame
                            offset_delta = tag;
                        } else if (tag >= 64 && tag <= 127) { // same_locals_1_stack_item
                            offset_delta = tag - 64;
                            read_verification_type_info(reader, payload_writer);
                        } else if (tag == 247) { // same_locals_1_stack_item_extended
                            offset_delta = reader.read_u2();
                            read_verification_type_info(reader, payload_writer);
                        } else if (tag >= 248 && tag <= 250) { // chop_frame
                            offset_delta = reader.read_u2();
                        } else if (tag == 251) { // same_frame_extended
                            offset_delta = reader.read_u2();
                        } else if (tag >= 252 && tag <= 254) { // append_frame
                            offset_delta = reader.read_u2();
                            int k = tag - 251;
                            for (int j = 0; j < k; ++j) {
                                read_verification_type_info(reader, payload_writer);
                            }
                        } else if (tag == 255) { // full_frame
                            offset_delta = reader.read_u2();
                            u2 num_locals = reader.read_u2();
                            payload_writer.write_u2(num_locals);
                            for (u2 j = 0; j < num_locals; ++j) {
                                read_verification_type_info(reader, payload_writer);
                            }
                            u2 num_stack = reader.read_u2();
                            payload_writer.write_u2(num_stack);
                            for (u2 j = 0; j < num_stack; ++j) {
                                read_verification_type_info(reader, payload_writer);
                            }
                        }

                        if (!has_prev) {
                            rec.orig_bci = offset_delta;
                            has_prev = true;
                        } else {
                            rec.orig_bci = prev_orig_bci + offset_delta + 1;
                        }
                        prev_orig_bci = rec.orig_bci;
                        rec.new_bci = lookup_target(static_cast<i4>(rec.orig_bci));
                        rec.payload = std::move(payload_writer.data());
                        frames.push_back(std::move(rec));
                    }
                }
            }
        }
    }

    // Collect all branch targets that need frames
    auto add_target_if_missing = [&](u4 tgt) {
        if (tgt <= 0 || tgt >= new_total_size) return;
        for (const auto& fr : frames) {
            if (fr.new_bci == tgt) return;
        }
        FrameRecord rec;
        rec.frame_type = 0; // same_frame
        rec.new_bci = tgt;
        frames.push_back(std::move(rec));
    };

    for (size_t i = 0; i < new_instructions.size(); ++i) {
        const auto& instr = new_instructions[i];
        if (const auto* b16 = std::get_if<Branch16Operand>(&instr.operand)) {
            add_target_if_missing(lookup_target(b16->target_abs));
        } else if (const auto* b32 = std::get_if<Branch32Operand>(&instr.operand)) {
            add_target_if_missing(lookup_target(b32->target_abs));
        } else if (const auto* tsd = std::get_if<TableSwitchData>(&instr.operand)) {
            add_target_if_missing(lookup_target(tsd->default_target_abs));
            for (i4 off : tsd->jump_targets_abs) {
                add_target_if_missing(lookup_target(off));
            }
        } else if (const auto* lsd = std::get_if<LookupSwitchData>(&instr.operand)) {
            add_target_if_missing(lookup_target(lsd->default_target_abs));
            for (const auto& pair : lsd->pairs) {
                add_target_if_missing(lookup_target(pair.target_abs));
            }
        }
    }

    if (frames.empty()) return;

    // Sort frames by new_bci ascending
    std::sort(frames.begin(), frames.end(), [](const auto& a, const auto& b) {
        return a.new_bci < b.new_bci;
    });

    // Deduplicate by new_bci
    std::vector<FrameRecord> unique_frames;
    for (auto& fr : frames) {
        if (unique_frames.empty() || unique_frames.back().new_bci != fr.new_bci) {
            unique_frames.push_back(std::move(fr));
        } else {
            if (unique_frames.back().frame_type == 0 && fr.frame_type != 0) {
                unique_frames.back() = std::move(fr);
            }
        }
    }
    frames = std::move(unique_frames);

    // Re-serialize frames with updated offset_deltas
    BinaryWriter writer;
    writer.write_u2(static_cast<u2>(frames.size()));

    u4 prev_new_bci = 0;
    bool wrote_prev = false;

    for (const auto& fr : frames) {
        u4 new_offset_delta = 0;
        if (!wrote_prev) {
            new_offset_delta = fr.new_bci;
            wrote_prev = true;
        } else {
            new_offset_delta = (fr.new_bci > prev_new_bci) ? (fr.new_bci - prev_new_bci - 1) : 0;
        }
        prev_new_bci = fr.new_bci;

        u1 orig_tag = fr.frame_type;
        if (orig_tag <= 63) { // same_frame
            if (new_offset_delta <= 63) {
                writer.write_u1(static_cast<u1>(new_offset_delta));
            } else {
                writer.write_u1(251); // same_frame_extended
                writer.write_u2(static_cast<u2>(new_offset_delta));
            }
        } else if (orig_tag >= 64 && orig_tag <= 127) { // same_locals_1_stack_item
            if (new_offset_delta <= 63) {
                writer.write_u1(static_cast<u1>(64 + new_offset_delta));
                writer.write_bytes(fr.payload);
            } else {
                writer.write_u1(247); // same_locals_1_stack_item_extended
                writer.write_u2(static_cast<u2>(new_offset_delta));
                writer.write_bytes(fr.payload);
            }
        } else if (orig_tag == 247) { // same_locals_1_stack_item_extended
            writer.write_u1(247);
            writer.write_u2(static_cast<u2>(new_offset_delta));
            writer.write_bytes(fr.payload);
        } else if (orig_tag >= 248 && orig_tag <= 250) { // chop_frame
            writer.write_u1(orig_tag);
            writer.write_u2(static_cast<u2>(new_offset_delta));
        } else if (orig_tag == 251) { // same_frame_extended
            writer.write_u1(251);
            writer.write_u2(static_cast<u2>(new_offset_delta));
        } else if (orig_tag >= 252 && orig_tag <= 254) { // append_frame
            writer.write_u1(orig_tag);
            writer.write_u2(static_cast<u2>(new_offset_delta));
            writer.write_bytes(fr.payload);
        } else if (orig_tag == 255) { // full_frame
            writer.write_u1(255);
            writer.write_u2(static_cast<u2>(new_offset_delta));
            writer.write_bytes(fr.payload);
        }
    }

    if (!smt_attr) {
        Attribute attr;
        attr.name_index = cp.find_or_add_utf8("StackMapTable");
        AttrRaw raw;
        raw.data = std::move(writer.data());
        attr.data = std::move(raw);
        code.attributes.push_back(std::move(attr));
    } else {
        auto* raw = smt_attr->get_mut_if<AttrRaw>();
        if (raw) raw->data = std::move(writer.data());
    }
}

void update_code(AttrCode& code, const std::vector<Instruction>& new_instructions, ConstantPool& cp,
                 const std::function<u2(u2)>& remap_cp_class) {
    if (new_instructions.empty()) {
        code.code.clear();
        return;
    }

    size_t num_instr = new_instructions.size();
    std::vector<u4> new_offsets(num_instr, 0);
    std::map<u4, u4> orig_to_new;
    u4 cur_offset = 0;

    for (size_t i = 0; i < num_instr; ++i) {
        new_offsets[i] = cur_offset;
        if (new_instructions[i].offset != static_cast<u4>(-1)) {
            if (orig_to_new.find(new_instructions[i].offset) == orig_to_new.end()) {
                orig_to_new[new_instructions[i].offset] = cur_offset;
            }
        }
        cur_offset += static_cast<u4>(get_instruction_size(new_instructions[i], cur_offset));
    }
    u4 new_total_size = cur_offset;
    u4 orig_total_size = static_cast<u4>(code.code.size());
    orig_to_new[orig_total_size] = new_total_size;

    auto lookup_target = [&](i4 orig_target) -> u4 {
        if (orig_target < 0) return 0;
        u4 target = static_cast<u4>(orig_target);
        auto it = orig_to_new.find(target);
        if (it != orig_to_new.end()) return it->second;
        auto lb = orig_to_new.lower_bound(target);
        if (lb != orig_to_new.end()) return lb->second;
        return new_total_size;
    };

    BinaryWriter writer;
    for (size_t i = 0; i < num_instr; ++i) {
        const auto& instr = new_instructions[i];
        u4 instr_new_off = new_offsets[i];
        writer.write_u1(static_cast<u1>(instr.opcode));

        std::visit([&](const auto& op) {
            using T = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<T, ByteOperand>) {
                writer.write_i1(op.value);
            } else if constexpr (std::is_same_v<T, ShortOperand>) {
                writer.write_i2(op.value);
            } else if constexpr (std::is_same_v<T, IntOperand>) {
                writer.write_i4(op.value);
            } else if constexpr (std::is_same_v<T, LocalVarOperand>) {
                writer.write_u1(op.index);
            } else if constexpr (std::is_same_v<T, CpIndexOperand>) {
                if (instr.opcode == Opcode::ldc) {
                    writer.write_u1(static_cast<u1>(op.index));
                } else {
                    writer.write_u2(op.index);
                }
            } else if constexpr (std::is_same_v<T, CpIndexWideOperand>) {
                writer.write_u2(op.index);
            } else if constexpr (std::is_same_v<T, MultiANewArrayOperand>) {
                writer.write_u2(op.index);
                writer.write_u1(op.dimensions);
            } else if constexpr (std::is_same_v<T, Branch16Operand>) {
                u4 target_new = lookup_target(op.target_abs);
                i2 rel = static_cast<i2>(static_cast<i4>(target_new) - static_cast<i4>(instr_new_off));
                writer.write_i2(rel);
            } else if constexpr (std::is_same_v<T, Branch32Operand>) {
                u4 target_new = lookup_target(op.target_abs);
                i4 rel = static_cast<i4>(target_new) - static_cast<i4>(instr_new_off);
                writer.write_i4(rel);
            } else if constexpr (std::is_same_v<T, IincOperand>) {
                writer.write_u1(op.index);
                writer.write_i1(op.const_val);
            } else if constexpr (std::is_same_v<T, WideOperand>) {
                writer.write_u1(static_cast<u1>(op.modified_opcode));
                writer.write_u2(op.index);
                if (op.modified_opcode == Opcode::iinc) {
                    writer.write_i2(op.const_val);
                }
            } else if constexpr (std::is_same_v<T, InterfaceCallOperand>) {
                writer.write_u2(op.index);
                writer.write_u1(op.count);
                writer.write_u1(0);
            } else if constexpr (std::is_same_v<T, InvokeDynamicOperand>) {
                writer.write_u2(op.index);
                writer.write_u2(0);
            } else if constexpr (std::is_same_v<T, TableSwitchData>) {
                size_t pad = (4 - (instr_new_off + 1) % 4) % 4;
                for (size_t p = 0; p < pad; ++p) writer.write_u1(0);
                i4 def_rel = static_cast<i4>(lookup_target(op.default_target_abs)) - static_cast<i4>(instr_new_off);
                writer.write_i4(def_rel);
                writer.write_i4(op.low);
                writer.write_i4(op.high);
                for (i4 tgt : op.jump_targets_abs) {
                    i4 j_rel = static_cast<i4>(lookup_target(tgt)) - static_cast<i4>(instr_new_off);
                    writer.write_i4(j_rel);
                }
            } else if constexpr (std::is_same_v<T, LookupSwitchData>) {
                size_t pad = (4 - (instr_new_off + 1) % 4) % 4;
                for (size_t p = 0; p < pad; ++p) writer.write_u1(0);
                i4 def_rel = static_cast<i4>(lookup_target(op.default_target_abs)) - static_cast<i4>(instr_new_off);
                writer.write_i4(def_rel);
                writer.write_i4(static_cast<i4>(op.pairs.size()));
                for (const auto& pair : op.pairs) {
                    writer.write_i4(pair.match);
                    i4 p_rel = static_cast<i4>(lookup_target(pair.target_abs)) - static_cast<i4>(instr_new_off);
                    writer.write_i4(p_rel);
                }
            } else if constexpr (std::is_same_v<T, NoOperand>) {
                // No extra bytes
            }
        }, instr.operand);
    }
    code.code = std::move(writer.data());

    // Update exception handlers
    for (auto& ex : code.exception_table) {
        ex.start_pc = static_cast<u2>(lookup_target(static_cast<i4>(ex.start_pc)));
        ex.end_pc = static_cast<u2>(lookup_target(static_cast<i4>(ex.end_pc)));
        ex.handler_pc = static_cast<u2>(lookup_target(static_cast<i4>(ex.handler_pc)));
    }

    // Recalculate StackMapTable
    recalculate_stack_map_table(code, cp, new_instructions, new_offsets, lookup_target, new_total_size, remap_cp_class);
}

} // namespace obf::bytecode
