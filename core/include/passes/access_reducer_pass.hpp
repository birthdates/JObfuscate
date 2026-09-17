#pragma once

#include "pass.hpp"

namespace obf::passes {

class AccessReducerPass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "AccessReducer"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Converts unused public and protected methods/fields into private";
    }

    bool run_on_archive(PassContext& ctx) override;
};

} // namespace obf::passes
