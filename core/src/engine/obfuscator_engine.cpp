#include "engine/obfuscator_engine.hpp"
#include "reporter/ndjson_reporter.hpp"
#include "reporter/console_reporter.hpp"
#include <chrono>

namespace obf::engine {

ObfuscatorEngine::ObfuscatorEngine(config::ObfuscatorConfig config)
    : m_config(std::move(config)) {}

std::unique_ptr<reporter::IReporter> ObfuscatorEngine::create_reporter() {
    if (m_config.json_stream_mode) {
        return std::make_unique<reporter::NdJsonReporter>();
    }
    return std::make_unique<reporter::ConsoleReporter>(m_config.verbose);
}

void ObfuscatorEngine::build_hierarchy(passes::ClassHierarchy& hierarchy, const jar::JarArchive& archive) {
    for (const auto& [name, cf] : archive.classes()) {
        std::string class_name(cf.name());
        std::string super_name(cf.super_name());
        std::vector<std::string> ifaces;
        ifaces.reserve(cf.interfaces().size());
        for (bytecode::u2 iface_idx : cf.interfaces()) {
            ifaces.emplace_back(cf.constant_pool().get_class_name(iface_idx));
        }
        hierarchy.add_class(class_name, super_name, ifaces);
    }
}

bool ObfuscatorEngine::run() {
    auto reporter = create_reporter();
    auto start_time = std::chrono::steady_clock::now();

    reporter->log(reporter::LogLevel::Info, "Starting Java Obfuscator for: " + m_config.input_jar.string());

    if (!std::filesystem::exists(m_config.input_jar)) {
        reporter->error("Input JAR file does not exist: " + m_config.input_jar.string());
        return false;
    }

    jar::JarArchive archive;
    std::string err;
    if (!archive.load_from_file(m_config.input_jar, err)) {
        reporter->error("Failed to load JAR archive: " + err);
        return false;
    }

    m_stats.classes_processed = archive.class_count();
    m_stats.original_size_bytes = std::filesystem::file_size(m_config.input_jar);
    reporter->log(reporter::LogLevel::Info, "Loaded " + std::to_string(archive.class_count()) + " classes and " +
                  std::to_string(archive.resource_count()) + " resources from archive.");

    passes::PassContext ctx(archive, m_config, *reporter, m_stats);
    build_hierarchy(ctx.hierarchy, archive);

    auto pipeline = passes::PassPipeline::build_from_config(m_config);
    if (!pipeline.execute(ctx)) {
        reporter->error("Obfuscation pipeline failed during execution.");
        return false;
    }

    reporter->log(reporter::LogLevel::Info, "Saving obfuscated JAR to: " + m_config.output_jar.string());
    if (!archive.save_to_file(m_config.output_jar, err)) {
        reporter->error("Failed to save output JAR: " + err);
        return false;
    }

    if (std::filesystem::exists(m_config.output_jar)) {
        m_stats.output_size_bytes = std::filesystem::file_size(m_config.output_jar);
    }

    auto end_time = std::chrono::steady_clock::now();
    uint64_t duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    reporter->complete(true, duration_ms, m_stats);

    return true;
}

} // namespace obf::engine
