#pragma once

#include "pass.hpp"

namespace obf::passes {

class IndyCallSitePass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "IndyCallSites"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Obfuscates method invocations into invokedynamic call sites with dynamic BSM resolution";
    }

    bool run_on_class(bytecode::ClassFile& cls, PassContext& ctx) override;

private:
    void transform_method_invocations(bytecode::AttrCode& code, bytecode::ClassFile& cls, PassContext& ctx, size_t& call_counter);
};

} // namespace obf::passes
