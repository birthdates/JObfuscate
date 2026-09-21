#include "passes/pass_pipeline.hpp"
#include "passes/strip_debug_pass.hpp"
#include "passes/rename_pass.hpp"
#include "passes/string_encrypt_pass.hpp"
#include "passes/control_flow_pass.hpp"
#include "passes/reflection_proxy_pass.hpp"
#include "passes/number_obfuscation_pass.hpp"
#include "passes/access_reducer_pass.hpp"
#include "passes/indy_call_site_pass.hpp"
#include "passes/shrinker_pass.hpp"
#include "passes/condy_pass.hpp"
#include "passes/decompiler_deterrence_pass.hpp"
#include "passes/class_encryption_pass.hpp"
#include <chrono>

namespace obf::passes {

bool PassPipeline::execute(PassContext& ctx) {
    if (ctx.config.reflection_proxy.enabled || ctx.config.string_encryption.enabled) {
        ctx.ensure_global_cache_name();
    }
    for (const auto& pass : m_passes) {
        if (pass->name() == "ClassEncryption") {
            synthesize_global_cache_class(ctx);
        }
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
    synthesize_global_cache_class(ctx);
    return true;
}

PassPipeline PassPipeline::build_from_config(const config::ObfuscatorConfig& config) {
    PassPipeline pipeline;

    // 1. Debug Stripping Pass (Lines, local variable tables, source filenames)
    if (config.strip.strip_line_numbers || config.strip.strip_local_variables ||
        config.strip.strip_source_file || config.strip.strip_source_debug ||
        config.strip.strip_kotlin_metadata) {
        pipeline.add_pass(std::make_unique<StripDebugPass>());
    }

    // 2. Code Shrinking & Tree Shaking Pass (detects and removes dead classes, methods, fields, attributes)
    if (config.shrink.enabled) {
        pipeline.add_pass(std::make_unique<ShrinkerPass>());
    }

    // 3. Access Reduction Pass (auto-privatizes unused public/protected members)
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

    // 6. Dynamic InvokeDynamic Call Site Pass (hides static/virtual/interface calls via indy BSM resolution)
    if (config.indy_call_sites.enabled) {
        pipeline.add_pass(std::make_unique<IndyCallSitePass>());
    }

    // 7. Dynamic ConstantDynamic (Condy) Call Site Pass (replaces constants with CONSTANT_Dynamic resolution, Java 11+)
    if (config.condy.enabled) {
        pipeline.add_pass(std::make_unique<CondyPass>());
    }

    // 8. Control Flow Obfuscation Pass (Opaque predicates, switch flattening & decompiler traps)
    if (config.control_flow.enabled) {
        pipeline.add_pass(std::make_unique<ControlFlowPass>());
    }

    // 9. String Encryption Pass (encrypts original literals, reflection strings, trap strings, and indy recipes)
    if (config.string_encryption.enabled) {
        pipeline.add_pass(std::make_unique<StringEncryptPass>());
    }

    // 10. Standards-Compliant Decompiler Deterrence Pass (overloaded return types, synthetic flags, phantom inner classes, scramble methods)
    if (config.decompiler_deterrence.enabled ||
        config.rename.scramble_methods ||
        config.decompiler_deterrence.scramble_method_order) {
        pipeline.add_pass(std::make_unique<DecompilerDeterrencePass>());
    }

    // 11. Class Encryption & In-Memory ClassLoader Pass (packs & encrypts all application classes)
    if (config.class_encryption.enabled) {
        pipeline.add_pass(std::make_unique<ClassEncryptionPass>());
    }

    return pipeline;
}

} // namespace obf::passes
