#pragma once

#include "pass.hpp"

namespace obf::passes {

class ControlFlowPass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "ControlFlow"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Confuses decompilers using opaque predicates and control flow switch flattening";
    }

    bool run_on_class(bytecode::ClassFile& cls, PassContext& ctx) override;

private:
    void obfuscate_method_code(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx);
    void insert_opaque_predicate(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx);
    void flatten_control_flow(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx);
};

} // namespace obf::passes
