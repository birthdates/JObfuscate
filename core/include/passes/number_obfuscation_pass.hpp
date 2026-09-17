#pragma once

#include "pass.hpp"

namespace obf::passes {

class NumberObfuscationPass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "NumberObfuscation"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Splits integer constants into equivalent bitwise and arithmetic instruction sequences";
    }

    bool run_on_class(bytecode::ClassFile& cls, PassContext& ctx) override;

private:
    void transform_numbers_in_code(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx);
};

} // namespace obf::passes
