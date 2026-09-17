#pragma once

#include "pass.hpp"

namespace obf::passes {

class StringEncryptPass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "StringEncrypt"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Encrypts string literals and replaces them with dynamic decryption calls";
    }

    bool run_on_class(bytecode::ClassFile& cls, PassContext& ctx) override;

private:
    void inject_decryption_method(bytecode::ClassFile& cls, const std::string& decrypt_name, size_t variant_idx);
    void transform_indy_string_concat(bytecode::ClassFile& cls, PassContext& ctx);
};

} // namespace obf::passes
