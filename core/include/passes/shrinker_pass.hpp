#pragma once

#include "pass.hpp"

namespace obf::passes {

class ShrinkerPass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "CodeShrinking"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Detects and removes unused classes, methods, fields, and non-essential attributes";
    }

    bool run_on_archive(PassContext& ctx) override;
};

} // namespace obf::passes
