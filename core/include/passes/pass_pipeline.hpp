#pragma once

#include "pass.hpp"
#include <vector>
#include <memory>

namespace obf::passes {

class PassPipeline {
public:
    PassPipeline() = default;

    void add_pass(std::unique_ptr<IPass> pass) {
        m_passes.push_back(std::move(pass));
    }

    [[nodiscard]] size_t pass_count() const noexcept { return m_passes.size(); }
    [[nodiscard]] const std::vector<std::unique_ptr<IPass>>& passes() const noexcept { return m_passes; }

    bool execute(PassContext& ctx);

    // Factory to construct standard pipeline based on configuration
    [[nodiscard]] static PassPipeline build_from_config(const config::ObfuscatorConfig& config);

private:
    std::vector<std::unique_ptr<IPass>> m_passes;
};

} // namespace obf::passes
