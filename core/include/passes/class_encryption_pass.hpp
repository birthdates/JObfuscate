#pragma once

#include "passes/pass.hpp"
#include <string>
#include <vector>

namespace obf::passes {

class ClassEncryptionPass : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override {
        return "ClassEncryption";
    }

    [[nodiscard]] std::string_view description() const noexcept override {
        return "Encrypts application classes into protected resource bundle with in-memory ClassLoader";
    }

    bool run_on_archive(PassContext& ctx) override;
};

} // namespace obf::passes
