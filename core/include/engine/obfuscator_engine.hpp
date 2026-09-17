#pragma once

#include "../config/obfuscator_config.hpp"
#include "../jar/jar_archive.hpp"
#include "../passes/pass_pipeline.hpp"
#include "../passes/pass_context.hpp"
#include "../reporter/reporter.hpp"
#include <memory>
#include <filesystem>

namespace obf::engine {

class ObfuscatorEngine {
public:
    explicit ObfuscatorEngine(config::ObfuscatorConfig config);

    [[nodiscard]] const config::ObfuscatorConfig& config() const noexcept { return m_config; }
    [[nodiscard]] const reporter::ObfuscationStats& stats() const noexcept { return m_stats; }

    // Execute the full obfuscation workflow
    [[nodiscard]] bool run();

private:
    void build_hierarchy(passes::ClassHierarchy& hierarchy, const jar::JarArchive& archive);
    [[nodiscard]] std::unique_ptr<reporter::IReporter> create_reporter();

    config::ObfuscatorConfig m_config;
    reporter::ObfuscationStats m_stats;
};

} // namespace obf::engine
