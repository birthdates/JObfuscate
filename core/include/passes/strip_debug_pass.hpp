#pragma once

#include "pass.hpp"

namespace obf::passes {

class StripDebugPass final : public IPass {
public:
    [[nodiscard]] std::string_view name() const noexcept override { return "StripDebug"; }
    [[nodiscard]] std::string_view description() const noexcept override {
        return "Strips LineNumberTable, LocalVariableTable, SourceFile, and debug metadata";
    }

    bool run_on_class(bytecode::ClassFile& cls, PassContext& ctx) override;
};

} // namespace obf::passes
