#pragma once

#include "reporter.hpp"
#include <iostream>
#include <mutex>

namespace obf::reporter {

class NdJsonReporter final : public IReporter {
public:
    NdJsonReporter() = default;

    void log(LogLevel level, std::string_view message) override;
    void progress(std::string_view stage, size_t current, size_t total, std::string_view current_item) override;
    void complete(bool success, uint64_t duration_ms, const ObfuscationStats& stats) override;
    void error(std::string_view error_message) override;
    void pass_start(std::string_view pass, std::string_view description) override;
    void pass_complete(std::string_view pass, uint64_t duration_ms) override;

private:
    void emit_json_line(std::string_view json_str);
    std::mutex m_mutex;
};

} // namespace obf::reporter
