#pragma once

#include "pass.hpp"

namespace obf::passes {

class ControlFlowPass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "ControlFlow"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Confuses decompilers using opaque predicates, Chenxi Wang switch flattening, and decompiler traps";
    }

    bool run_on_class(bytecode::ClassFile& cls, PassContext& ctx) override;

    static bool obfuscate_class(bytecode::ClassFile& cls, PassContext& ctx, bool include_synthetic = false, bool skip_find_class = false);

private:
    void insert_opaque_predicate(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx);
    bool flatten_control_flow(bytecode::MethodInfo& method, bytecode::ClassFile& cls, PassContext& ctx);
    void inject_decompiler_traps(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx);
    void inject_exception_dispatch(bytecode::AttrCode& code, bytecode::ConstantPool& cp, PassContext& ctx);
};

} // namespace obf::passes
