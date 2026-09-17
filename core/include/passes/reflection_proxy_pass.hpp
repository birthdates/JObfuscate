#pragma once

#include "pass.hpp"

namespace obf::passes {

class ReflectionProxyPass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "ReflectionProxy"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Converts external class and method invocations into runtime dynamic reflection calls";
    }

    bool run_on_class(bytecode::ClassFile& cls, PassContext& ctx) override;

private:
    void transform_invocations(bytecode::AttrCode& code, bytecode::ClassFile& cls, PassContext& ctx);
};

} // namespace obf::passes
