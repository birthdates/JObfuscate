#pragma once

#include "passes/pass.hpp"
#include <string>
#include <vector>

namespace obf::passes {

class CondyPass : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "Condy";
    }

    [[nodiscard]] std::string_view description() const noexcept override {
        return "Hides constants and static initializers via Java 11+ CONSTANT_Dynamic BSM call sites";
    }

    bool run_on_archive(PassContext& ctx) override;

private:
    bool transform_class(bytecode::ClassFile& cf, PassContext& ctx);
};

} // namespace obf::passes
