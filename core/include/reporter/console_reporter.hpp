#pragma once

#include "reporter.hpp"
#include <iostream>
#include <mutex>

namespace obf::reporter {

class ConsoleReporter final : public IReporter {
public:
    explicit ConsoleReporter(bool verbose = false);

    void log(LogLevel level, std::string_view message) override;
    void progress(std::string_view stage, size_t current, size_t total, std::string_view current_item) override;
    void complete(bool success, uint64_t duration_ms, const ObfuscationStats& stats) override;
    void error(std::string_view error_message) override;

private:
    bool m_verbose{false};
    std::mutex m_mutex;
};

} // namespace obf::reporter
