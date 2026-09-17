#include "reporter/console_reporter.hpp"
#include <iostream>
#include <iomanip>

namespace obf::reporter {

ConsoleReporter::ConsoleReporter(bool verbose)
    : m_verbose(verbose) {}

void ConsoleReporter::log(LogLevel level, std::string_view message) {
    if (level == LogLevel::Debug && !m_verbose) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    switch (level) {
        case LogLevel::Debug:
            std::cout << "[DEBUG] " << message << "\n";
            break;
        case LogLevel::Info:
            std::cout << "[\033[34mINFO\033[0m]  " << message << "\n";
            break;
        case LogLevel::Warn:
            std::cout << "[\033[33mWARN\033[0m]  " << message << "\n";
            break;
        case LogLevel::Error:
            std::cerr << "[\033[31mERROR\033[0m] " << message << "\n";
            break;
    }
}

void ConsoleReporter::progress(std::string_view stage, size_t current, size_t total, std::string_view current_item) {
    if (!m_verbose) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    double pct = (total > 0) ? (static_cast<double>(current) / static_cast<double>(total)) * 100.0 : 0.0;
    std::cout << " -> [" << stage << "] (" << std::fixed << std::setprecision(1) << pct << "%) "
              << current_item << "\n";
}

void ConsoleReporter::complete(bool success, uint64_t duration_ms, const ObfuscationStats& stats) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cout << "\n=======================================================\n";
    if (success) {
        std::cout << "[\033[32mSUCCESS\033[0m] Obfuscation finished in " << duration_ms << " ms\n";
        std::cout << " - Classes processed:          " << stats.classes_processed << "\n";
        std::cout << " - Classes renamed:            " << stats.classes_renamed << "\n";
        std::cout << " - Methods renamed:            " << stats.methods_renamed << "\n";
        std::cout << " - Fields renamed:             " << stats.fields_renamed << "\n";
        std::cout << " - Strings encrypted:          " << stats.strings_encrypted << "\n";
        std::cout << " - Methods flattened:          " << stats.methods_flattened << "\n";
        std::cout << " - Reflection proxies created: " << stats.reflection_proxies_created << "\n";
        std::cout << " - Numbers obfuscated:         " << stats.numbers_obfuscated << "\n";
        std::cout << " - Members privatized:         " << stats.members_privatized << "\n";
        std::cout << " - Debug attributes stripped:  " << stats.debug_attributes_stripped << "\n";
    } else {
        std::cout << "[\033[31mFAILED\033[0m] Obfuscation failed after " << duration_ms << " ms\n";
    }
    std::cout << "=======================================================\n";
}

void ConsoleReporter::error(std::string_view error_message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::cerr << "[\033[31mFATAL\033[0m] " << error_message << "\n";
}

} // namespace obf::reporter
