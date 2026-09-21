#pragma once

#include "pass.hpp"
#include "../bytecode/instructions.hpp"
#include <cstdint>
#include <random>
#include <vector>

namespace obf::passes {

class StringEncryptPass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "StringEncrypt"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Encrypts string literals and replaces them with dynamic decryption calls";
    }

    bool run_on_class(bytecode::ClassFile& cls, PassContext& ctx) override;

    static std::string encrypt_string_variant(std::string_view input, bytecode::u4 key, size_t variant_idx);
    static void inject_decryption_method_v0(bytecode::ClassFile& cls, const std::string& decrypt_name);

    // Push a 32-bit decryption key onto the stack as a split expression
    // (K = A ^ B  or  K = (A ^ B) + C) so no single key constant appears
    // at the call site. Straight-line ixor/iadd only: verifier-safe.
    static void emit_split_key(std::vector<bytecode::Instruction>& out,
                               bytecode::ConstantPool& cp,
                               uint32_t key,
                               std::mt19937_64& rng);

private:
    void inject_decryption_method(bytecode::ClassFile& cls, const std::string& decrypt_name, size_t variant_idx);
    void transform_indy_string_concat(bytecode::ClassFile& cls, PassContext& ctx);
};

} // namespace obf::passes
