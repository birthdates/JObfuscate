#include "reporter/ndjson_reporter.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace obf::reporter {

void NdJsonReporter::emit_json_line(std::string_view json_str) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << json_str << std::endl;
}

void NdJsonReporter::log(LogLevel level, std::string_view message) {
    std::string lvl_str;
    switch (level) {
        case LogLevel::Debug: lvl_str = "debug"; break;
        case LogLevel::Info:  lvl_str = "info"; break;
        case LogLevel::Warn:  lvl_str = "warn"; break;
        case LogLevel::Error: lvl_str = "error"; break;
    }

    json j = {
        {"type", "log"},
        {"level", lvl_str},
        {"message", std::string(message)}
    };
    emit_json_line(j.dump());
}

void NdJsonReporter::progress(std::string_view stage, size_t current, size_t total, std::string_view current_item) {
    double pct = (total > 0) ? (static_cast<double>(current) / static_cast<double>(total)) * 100.0 : 0.0;
    json j = {
        {"type", "progress"},
        {"stage", std::string(stage)},
        {"current", current},
        {"total", total},
        {"percent", pct},
        {"item", std::string(current_item)}
    };
    emit_json_line(j.dump());
}

void NdJsonReporter::complete(bool success, uint64_t duration_ms, const ObfuscationStats& stats) {
    json j = {
        {"type", "complete"},
        {"status", success ? "success" : "failed"},
        {"duration_ms", duration_ms},
        {"stats", {
            {"classes_processed", stats.classes_processed},
            {"strings_encrypted", stats.strings_encrypted},
            {"methods_flattened", stats.methods_flattened},
            {"fields_renamed", stats.fields_renamed},
            {"methods_renamed", stats.methods_renamed},
            {"classes_renamed", stats.classes_renamed},
            {"debug_attributes_stripped", stats.debug_attributes_stripped},
            {"reflection_proxies_created", stats.reflection_proxies_created},
            {"numbers_obfuscated", stats.numbers_obfuscated},
            {"members_privatized", stats.members_privatized},
            {"original_size_bytes", stats.original_size_bytes},
            {"output_size_bytes", stats.output_size_bytes}
        }}
    };
    emit_json_line(j.dump());
}

void NdJsonReporter::error(std::string_view error_message) {
    json j = {
        {"type", "error"},
        {"message", std::string(error_message)}
    };
    emit_json_line(j.dump());
}

void NdJsonReporter::pass_start(std::string_view pass, std::string_view description) {
    json j = {
        {"type", "pass_start"},
        {"pass", std::string(pass)},
        {"description", std::string(description)}
    };
    emit_json_line(j.dump());
}

void NdJsonReporter::pass_complete(std::string_view pass, uint64_t duration_ms) {
    json j = {
        {"type", "pass_complete"},
        {"pass", std::string(pass)},
        {"duration_ms", duration_ms}
    };
    emit_json_line(j.dump());
}

} // namespace obf::reporter
