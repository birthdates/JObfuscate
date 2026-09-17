#include "passes/pass_pipeline.hpp"
#include "passes/strip_debug_pass.hpp"
#include "passes/rename_pass.hpp"
#include "passes/string_encrypt_pass.hpp"
#include "passes/control_flow_pass.hpp"
#include "passes/reflection_proxy_pass.hpp"
#include "passes/number_obfuscation_pass.hpp"
#include "passes/access_reducer_pass.hpp"
#include <chrono>

namespace obf::passes {

bool PassPipeline::execute(PassContext& ctx) {
    for (auto& pass : m_passes) {
        ctx.reporter.log(reporter::LogLevel::Info, "Executing pass: " + std::string(pass->name()) + " - " + std::string(pass->description()));
        ctx.reporter.pass_start(pass->name(), pass->description());
        auto start = std::chrono::steady_clock::now();
        bool ok = pass->run_on_archive(ctx);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        if (!ok) {
            ctx.reporter.log(reporter::LogLevel::Error, "Pass failed: " + std::string(pass->name()));
            return false;
        }
        ctx.reporter.pass_complete(pass->name(), static_cast<uint64_t>(elapsed));
    }
    return true;
}

PassPipeline PassPipeline::build_from_config(const config::ObfuscatorConfig& config) {
    PassPipeline pipeline;

    // 1. Debug Stripping Pass (Lines, local variable tables, source filenames)
    if (config.strip.strip_line_numbers || config.strip.strip_local_variables ||
        config.strip.strip_source_file || config.strip.strip_source_debug) {
        pipeline.add_pass(std::make_unique<StripDebugPass>());
    }

    // 2. Access Reduction Pass (auto-privatizes unused public/protected members)
    if (config.access_reducer.enabled) {
        pipeline.add_pass(std::make_unique<AccessReducerPass>());
    }

    // 3. Dynamic Reflection / Import Obfuscation Pass (runs early so generated strings are encrypted)
    if (config.reflection_proxy.enabled) {
        pipeline.add_pass(std::make_unique<ReflectionProxyPass>());
    }

    // 4. Number / Constant Obfuscation Pass
    if (config.number_obfuscation.enabled) {
        pipeline.add_pass(std::make_unique<NumberObfuscationPass>());
    }

    // 5. Name Scrambling Pass (Classes, Packages, Fields, Methods)
    if (config.rename.enabled) {
        pipeline.add_pass(std::make_unique<RenamePass>());
    }

    // 5. String Encryption Pass (encrypts original literals, reflection strings, and indy recipes)
    if (config.string_encryption.enabled) {
        pipeline.add_pass(std::make_unique<StringEncryptPass>());
    }

    // 6. Control Flow Obfuscation Pass (Opaque predicates & dead branches)
    if (config.control_flow.enabled) {
        pipeline.add_pass(std::make_unique<ControlFlowPass>());
    }

    return pipeline;
}

} // namespace obf::passes
