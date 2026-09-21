#pragma once

#include "passes/pass.hpp"
#include <string>
#include <vector>

namespace obf::passes {

class DecompilerDeterrencePass : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "DecompilerDeterrence";
    }

    [[nodiscard]] std::string_view description() const noexcept override {
        return "Injects JVMS-compliant return-type overloads, synthetic bridge flags, and phantom metadata";
    }

    bool run_on_archive(PassContext& ctx) override;

private:
    void apply_return_type_overloads(bytecode::ClassFile& cf, PassContext& ctx);
    void apply_synthetic_bridge_tags(bytecode::ClassFile& cf, PassContext& ctx);
    void apply_phantom_inner_classes(bytecode::ClassFile& cf, PassContext& ctx);
    void apply_custom_attributes(bytecode::ClassFile& cf, PassContext& ctx);
    void scramble_methods(bytecode::ClassFile& cf, PassContext& ctx);
};

} // namespace obf::passes
